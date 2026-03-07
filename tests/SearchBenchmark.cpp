/**
 * @file SearchBenchmark.cpp
 * @brief Standalone search benchmark: load index from file, run search from JSON config, report timing.
 *
 * Reuses: populate_index_from_file, ParseSearchConfigJson, FileIndex::SearchAsyncWithData.
 *
 * Usage:
 *   search_benchmark --index <paths.txt> --config <config.json or inline JSON> [--iterations N] [--warmup M] [--strategy NAME]
 *
 * Index file: one path per line (e.g. from --dump-index or generated list).
 * Config JSON: { "version": "1.0", "search_config": { "path": "pp:src/...", "extensions": ["cpp"], ... } }
 * Strategy: static, hybrid, dynamic, interleaved (and work_stealing if built with Boost).
 */

#include "api/GeminiApiUtils.h"
#include "core/Settings.h"
#include "index/FileIndex.h"
#include "index/IndexFromFilePopulator.h"
#include "search/SearchThreadPool.h"
#include "utils/CpuFeatures.h"
#include "utils/LoadBalancingStrategy.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace search_benchmark_detail {

constexpr int kDefaultIterations = 5;
constexpr int kDefaultWarmup = 1;
constexpr size_t kJsonExtensionLength = 5;

void PrintEnvironmentInfo(std::string_view load_balancing_strategy) {
  std::cout << "=== Environment ===\n";
#ifdef NDEBUG
  std::cout << "Build: Release\n";
#else
  std::cout << "Build: Debug\n";
#endif  // NDEBUG
  std::cout << "Load balancing: " << load_balancing_strategy << "\n";
  std::cout << "hardware_concurrency(): " << std::thread::hardware_concurrency() << "\n";
  const bool ht_enabled = cpu_features::IsHyperThreadingEnabled();
  std::cout << "Hyper-Threading: " << (ht_enabled ? "Yes" : "No") << "\n";
  if (const auto [physical_cores, logical_cores] = cpu_features::GetCoreCounts();
      physical_cores > 0 && logical_cores > 0) {
    std::cout << "Physical cores: " << physical_cores << ", Logical cores: " << logical_cores << "\n";
  } else {
    std::cout << "Core count: detection unavailable\n";
  }
  const bool avx2 = cpu_features::SupportsAVX2();
  std::cout << "AVX2 support: " << (avx2 ? "Yes" : "No") << "\n";
  std::cout << "Build features: " << cpu_features::GetBuildFeatureString() << "\n";
  std::cout << "==================\n";
}

// Groups time/size filter params for PrintSearchParameters to satisfy cpp:S107 (max 7 params).
struct FilterStringsView {
  std::string_view time_filter;
  std::string_view size_filter;
};

void PrintSearchParameters(std::string_view primary_query,
                           std::string_view path_query,
                           const std::vector<std::string>* extensions,
                           bool folders_only,
                           bool case_sensitive,
                           const FilterStringsView& filters) {
  std::cout << "=== Search parameters ===\n";
  std::cout << "filename query: \"" << (primary_query.empty() ? "(empty)" : primary_query) << "\"\n";
  std::cout << "path query: \"" << (path_query.empty() ? "(empty)" : path_query) << "\"\n";
  if (extensions != nullptr && !extensions->empty()) {
    std::cout << "extensions: ";
    for (size_t i = 0; i < extensions->size(); ++i) {
      if (i > 0) {
        std::cout << ", ";
      }
      std::cout << (*extensions)[i];
    }
    std::cout << "\n";
  } else {
    std::cout << "extensions: (none)\n";
  }
  std::cout << "folders_only: " << (folders_only ? "true" : "false") << "\n";
  std::cout << "case_sensitive: " << (case_sensitive ? "true" : "false") << "\n";
  std::cout << "time_filter: "
            << (filters.time_filter.empty() ? "(none)" : std::string(filters.time_filter)) << "\n";
  std::cout << "size_filter: "
            << (filters.size_filter.empty() ? "(none)" : std::string(filters.size_filter)) << "\n";
  std::cout << "========================\n";
}

void PrintRunParameters(std::string_view index_path,
                       std::string_view config_source,
                       int iterations,
                       int warmup) {
  std::cout << "=== Run parameters ===\n";
  std::cout << "index: " << (index_path.empty() ? "(none)" : index_path) << "\n";
  std::cout << "config: " << (config_source.empty() ? "(inline)" : config_source) << "\n";
  std::cout << "iterations: " << iterations << "\n";
  std::cout << "warmup: " << warmup << "\n";
  std::cout << "======================\n";
}

void PrintUsage(const char* program_name) {
  const std::vector<std::string> strategies = GetAvailableStrategyNames();
  std::ostringstream strategy_list;
  for (size_t i = 0; i < strategies.size(); ++i) {
    if (i > 0) {
      strategy_list << ", ";
    }
    strategy_list << strategies[i];
  }
  std::cerr << "Usage: " << program_name
            << " --index <path> --config <path|inline JSON> [--iterations N] [--warmup M] [--strategy NAME]\n"
            << "  --index <path>     Text file with one file path per line\n"
            << "  --config <path|JSON>  JSON config file path or inline JSON (version + search_config)\n"
            << "  --iterations N      Number of timed runs (default: " << kDefaultIterations << ")\n"
            << "  --warmup M          Number of warmup runs before timing (default: " << kDefaultWarmup
            << ")\n"
            << "  --strategy NAME     Load balancing strategy (default: " << GetDefaultStrategyName()
            << "). Options: " << strategy_list.str() << "\n";
}

bool IsLikelyFilePath(std::string_view arg) {
  if (arg.empty()) {
    return false;
  }
  if (arg.find('{') != std::string_view::npos && arg.find("search_config") != std::string_view::npos) {
    return false;  // Inline JSON
  }
  if (arg.size() >= kJsonExtensionLength &&
      arg.substr(arg.size() - kJsonExtensionLength) == ".json") {
    return true;
  }
  if (arg.find('/') != std::string_view::npos || arg.find('\\') != std::string_view::npos) {
    return true;
  }
  return false;
}

std::string ReadFileToString(const std::string& path) {
  const std::ifstream f(path);
  if (!f.is_open()) {
    return {};
  }
  std::ostringstream oss;
  oss << f.rdbuf();
  return oss.str();
}

std::string LoadConfigString(std::string_view config_arg) {
  std::string config_str(config_arg);
  if (IsLikelyFilePath(config_arg)) {
    std::string from_file = ReadFileToString(config_str);
    if (from_file.empty()) {
      std::cerr << "Error: could not read config file: " << config_str << "\n";
      return {};
    }
    return from_file;
  }
  return config_str;
}

size_t CollectFuturesAndCount(  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - PascalCase per CXX17_NAMING_CONVENTIONS
    std::vector<std::future<std::vector<SearchResultData>>>& futures) {
  size_t total = 0;
  for (auto& f : futures) {
    total += f.get().size();
  }
  return total;
}

struct ParsedArgs {
  std::string index_path;
  std::string config_arg;
  int iterations = kDefaultIterations;
  int warmup = kDefaultWarmup;
  std::string strategy;  // Empty = use default (from GetDefaultStrategyName)
  bool help = false;
};

// Advances i and returns pointer to next argv slot, or nullptr if missing (prints err_msg).
const char* RequireNextArg(int argc, char** argv, int& i, const char* err_msg) {
  if (i + 1 >= argc) {
    std::cerr << err_msg << "\n";
    return nullptr;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Standard C main argv access
  return argv[++i];
}

// Handler return: 0 = consumed (i advanced), 1 = parse error, 2 = success (for --help: caller exits 0).
int HandleIndex(int argc, char** argv, int& i, ParsedArgs& out) {
  const char* val = RequireNextArg(argc, argv, i, "Error: --index requires a path");
  if (val == nullptr) {
    return 1;
  }
  out.index_path = val;
  return 0;
}

int HandleConfig(int argc, char** argv, int& i, ParsedArgs& out) {
  const char* val =
      RequireNextArg(argc, argv, i, "Error: --config requires a path or JSON string");
  if (val == nullptr) {
    return 1;
  }
  out.config_arg = val;
  return 0;
}

int HandleIterations(int argc, char** argv, int& i, ParsedArgs& out) {
  const char* val = RequireNextArg(argc, argv, i, "Error: --iterations requires a number");
  if (val == nullptr) {
    return 1;
  }
  // NOLINTNEXTLINE(cert-err34-c) - CLI parsing
  out.iterations = (std::max)(std::atoi(val), 1);
  return 0;
}

int HandleWarmup(int argc, char** argv, int& i, ParsedArgs& out) {
  const char* val = RequireNextArg(argc, argv, i, "Error: --warmup requires a number");
  if (val == nullptr) {
    return 1;
  }
  // NOLINTNEXTLINE(cert-err34-c) - CLI parsing
  out.warmup = (std::max)(std::atoi(val), 0);
  return 0;
}

int HandleStrategy(int argc, char** argv, int& i, ParsedArgs& out) {
  const char* val =
      RequireNextArg(argc, argv, i, "Error: --strategy requires a strategy name");
  if (val == nullptr) {
    return 1;
  }
  out.strategy = val;
  return 0;
}

// Dispatches one option. Returns: -2 = help (set out.help, exit 0), -1 = unknown, 0 = consumed, 1 = error.
int DispatchOption(std::string_view arg, int argc, char** argv, int& i, ParsedArgs& out) {
  if (arg == "--help" || arg == "-h") {
    out.help = true;
    return -2;
  }
  if (arg == "--index") {
    return HandleIndex(argc, argv, i, out);
  }
  if (arg == "--config") {
    return HandleConfig(argc, argv, i, out);
  }
  if (arg == "--iterations") {
    return HandleIterations(argc, argv, i, out);
  }
  if (arg == "--warmup") {
    return HandleWarmup(argc, argv, i, out);
  }
  if (arg == "--strategy") {
    return HandleStrategy(argc, argv, i, out);
  }
  return -1;
}

// Returns 0 if --help (caller should exit 0), 1 on parse error, 2 on success.
int ParseArgs(int argc, char** argv, ParsedArgs& out) {
  for (int i = 1; i < argc; ++i) {  // NOSONAR(cpp:S924) - Standard argv iteration; single loop
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Standard C main argv iteration
    const std::string_view arg(argv[i]);
    const int r = DispatchOption(arg, argc, argv, i, out);
    if (r == -2) {
      return 0;  // Help
    }
    if (r == 1) {
      return 1;  // Parse error
    }
    if (r == 0) {
      continue;  // Consumed (i already advanced)
    }
    // r == -1: unknown option, skip
  }
  return 2;
}

struct BenchmarkStats {
  double min_ms = 0.0;
  double avg_ms = 0.0;
  double max_ms = 0.0;
  size_t result_count = 0;
};

BenchmarkStats ComputeStats(const std::vector<double>& times_ms, size_t result_count) {
  BenchmarkStats stats;
  stats.result_count = result_count;
  if (times_ms.empty()) {
    return stats;
  }
  stats.min_ms = times_ms[0];
  stats.max_ms = times_ms[0];
  double sum_ms = 0.0;
  for (const double t : times_ms) {
    stats.min_ms = (std::min)(stats.min_ms, t);
    stats.max_ms = (std::max)(stats.max_ms, t);
    sum_ms += t;
  }
  stats.avg_ms = sum_ms / static_cast<double>(times_ms.size());
  return stats;
}

void PrintStats(const BenchmarkStats& stats, int iterations) {
  std::cout << "Results: " << stats.result_count << " matches\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Time (ms): min=" << stats.min_ms << " avg=" << stats.avg_ms << " max=" << stats.max_ms;
  if (iterations > 1) {
    std::cout << " (" << iterations << " runs)";
  }
  std::cout << "\n";
}

}  // namespace search_benchmark_detail

// NOLINTNEXTLINE(bugprone-exception-escape) - Benchmark tool; exceptions from ParseSearchConfigJson etc. are acceptable
int main(int argc, char** argv) {
  using search_benchmark_detail::BenchmarkStats;
  using search_benchmark_detail::CollectFuturesAndCount;
  using search_benchmark_detail::ComputeStats;
  using search_benchmark_detail::LoadConfigString;
  using search_benchmark_detail::ParseArgs;
  using search_benchmark_detail::ParsedArgs;
  using search_benchmark_detail::PrintEnvironmentInfo;
  using search_benchmark_detail::PrintRunParameters;
  using search_benchmark_detail::PrintSearchParameters;
  using search_benchmark_detail::PrintStats;
  using search_benchmark_detail::PrintUsage;

  ParsedArgs parsed;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Standard C main argv
  const int parse_result = ParseArgs(argc, argv, parsed);
  if (parse_result == 0) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Standard C main argv[0]
    PrintUsage(argv[0]);
    return 0;
  }
  if (parse_result == 1) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Standard C main argv[0]
    PrintUsage(argv[0]);
    return 1;
  }
  if (parsed.index_path.empty() || parsed.config_arg.empty()) {
    std::cerr << "Error: --index and --config are required\n";
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Standard C main argv[0]
    PrintUsage(argv[0]);
    return 1;
  }

  const std::string config_str = LoadConfigString(parsed.config_arg);
  if (config_str.empty()) {
    return 1;
  }

  const gemini_api_utils::GeminiApiResult api_result = gemini_api_utils::ParseSearchConfigJson(config_str);
  if (!api_result.success) {
    std::cerr << "Error: failed to parse config: " << api_result.error_message << "\n";
    return 1;
  }

  const gemini_api_utils::SearchConfig& config = api_result.search_config;
  const std::string primary_query = config.filename;
  const std::string path_query = config.path;
  const std::vector<std::string>* extensions_ptr =
      config.extensions.empty() ? nullptr : &config.extensions;
  const bool folders_only = config.folders_only;
  const bool case_sensitive = config.case_sensitive;
  const int thread_count = -1;

  const std::string effective_strategy =
      parsed.strategy.empty() ? GetDefaultStrategyName()
                              : ValidateAndNormalizeStrategyName(parsed.strategy);
  AppSettings benchmark_settings;
  benchmark_settings.loadBalancingStrategy = effective_strategy;
  test_settings::SetInMemorySettings(benchmark_settings);

  const std::string config_source_label =
      search_benchmark_detail::IsLikelyFilePath(parsed.config_arg)
          ? std::string(parsed.config_arg)
          : "(inline JSON)";
  PrintEnvironmentInfo(effective_strategy);
  PrintRunParameters(parsed.index_path, config_source_label, parsed.iterations, parsed.warmup);
  PrintSearchParameters(primary_query, path_query, extensions_ptr,
                       folders_only, case_sensitive,
                       {config.time_filter, config.size_filter});

  auto thread_pool = std::make_shared<SearchThreadPool>(
      static_cast<int>(std::thread::hardware_concurrency()));
  if (thread_pool->GetThreadCount() == 0) {
    thread_pool = std::make_shared<SearchThreadPool>(4);
  }
  FileIndex file_index(thread_pool);

  if (!populate_index_from_file(parsed.index_path, file_index)) {
    std::cerr << "Error: failed to load index from: " << parsed.index_path << "\n";
    return 1;
  }
  file_index.RecomputeAllPaths();
  std::cout << "Index size: " << file_index.Size() << " paths\n";

  for (int w = 0; w < parsed.warmup; ++w) {
    auto futures = file_index.SearchAsyncWithData(
        primary_query, thread_count, nullptr, path_query, extensions_ptr,
        folders_only, case_sensitive, nullptr, nullptr);
    (void)CollectFuturesAndCount(futures);
  }

  std::vector<double> times_ms;
  times_ms.reserve(static_cast<size_t>(parsed.iterations));
  size_t result_count = 0;
  for (int i = 0; i < parsed.iterations; ++i) {
    auto start = std::chrono::high_resolution_clock::now();
    auto futures = file_index.SearchAsyncWithData(
        primary_query, thread_count, nullptr, path_query, extensions_ptr,
        folders_only, case_sensitive, nullptr, nullptr);
    result_count = CollectFuturesAndCount(futures);
    auto end = std::chrono::high_resolution_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    times_ms.push_back(ms);
  }

  const BenchmarkStats stats = ComputeStats(times_ms, result_count);
  PrintStats(stats, parsed.iterations);
  return 0;
}
