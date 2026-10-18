/**
 * @file SearchBenchmark.cpp
 * @brief Standalone search benchmark: load index from file, run search from JSON config, report timing.
 *
 * Reuses: populate_index_from_file, ParseSearchConfigJson, FileIndex::SearchAsyncWithData.
 *
 * Usage:
 *   search_benchmark --index <paths.txt> --config <config.json or inline JSON> [--iterations N] [--warmup M]
 *
 * Index file: one path per line (e.g. from --dump-index or generated list).
 * Config JSON: { "version": "1.0", "search_config": { "path": "pp:src/...", "extensions": ["cpp"], ... } }
 * Large index + filename-only stress: tests/data/std-linux-filesystem.txt with
 * tests/benchmark_config_std_linux_filename_hotpath.json (see scripts/build_tests_macos.sh).
 */

#if defined(_WIN32)
#include <windows.h>  // NOSONAR(cpp:S3806) - Must be first: provides types for PSAPI header
#include <psapi.h>    // NOLINT(llvm-include-order) - must follow windows.h on MSVC
#elif defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

#include "api/GeminiApiUtils.h"
#include "core/Settings.h"
#include "index/FileIndex.h"
#include "index/IndexFromFilePopulator.h"
#include "search/SearchResultUtils.h"
#include "search/SearchTypes.h"
#include "utils/CpuFeatures.h"
#include "utils/Logger.h"
#include "utils/ThreadPool.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <numeric>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace search_benchmark_detail {

constexpr int kDefaultIterations = 5;
constexpr int kDefaultWarmup = 1;
constexpr size_t kJsonExtensionLength = 5;

// Parses base-10 integer; returns on_failure if the string is not a single integer in int range.
[[nodiscard]] inline int ParseCliInt(const char* val, int on_failure) {
  errno = 0;
  char* end = nullptr;
  const std::int64_t n = std::strtoll(val, &end, 10);
  if (val[0] == '\0' || end == val || *end != '\0' || errno == ERANGE) {
    return on_failure;
  }
  if (n > std::numeric_limits<int>::max() || n < std::numeric_limits<int>::min()) {
    return on_failure;
  }
  return static_cast<int>(n);
}

void PrintEnvironmentInfo() {
  std::cout << "=== Environment ===\n";
#ifdef NDEBUG
  std::cout << "Build: Release\n";
#else
  std::cout << "Build: Debug\n";
#endif  // NDEBUG
  std::cout << "Parallel search schedule: hybrid (fixed)\n";
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

// One benchmark query in engine-operational form (mirrors the
// FileIndex::SearchAsyncWithData parameter set, grouped for S107).
struct BenchmarkQuery {
  std::string_view primary_query{};
  std::string_view path_query{};
  const std::vector<std::string>* extensions = nullptr;
  bool folders_only = false;
  bool files_only = false;
  bool case_sensitive = false;
};

[[nodiscard]] constexpr ItemTypeFilter ResolveItemTypeFilter(bool folders_only, bool files_only) noexcept {
  if (folders_only) {
    return ItemTypeFilter::FoldersOnly;
  }
  if (files_only) {
    return ItemTypeFilter::FilesOnly;
  }
  return ItemTypeFilter::All;
}

// Groups time/size filter params for PrintSearchParameters to satisfy cpp:S107 (max 7 params).
struct FilterStringsView {
  std::string_view time_filter;
  std::string_view size_filter;
};

void PrintSearchParameters(const BenchmarkQuery& query,
                           const FilterStringsView& filters) {
  std::cout << "=== Search parameters ===\n";
  std::cout << "filename query: \"" << (query.primary_query.empty() ? "(empty)" : query.primary_query) << "\"\n";
  std::cout << "path query: \"" << (query.path_query.empty() ? "(empty)" : query.path_query) << "\"\n";
  if (query.extensions != nullptr && !query.extensions->empty()) {
    std::cout << "extensions: ";
    for (size_t i = 0; i < query.extensions->size(); ++i) {
      if (i > 0) {
        std::cout << ", ";
      }
      std::cout << (*query.extensions)[i];
    }
    std::cout << "\n";
  } else {
    std::cout << "extensions: (none)\n";
  }
  std::cout << "folders_only: " << (query.folders_only ? "true" : "false") << "\n";
  std::cout << "files_only: " << (query.files_only ? "true" : "false") << "\n";
  std::cout << "case_sensitive: " << (query.case_sensitive ? "true" : "false") << "\n";
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
  std::cerr << "Usage: " << program_name
            << " --index <path> (--config <path|inline JSON> | --config-dir <dir>)... [options]\n"
            << "  --index <path>     Text file with one file path per line\n"
            << "  --config <path|JSON>  JSON config file path or inline JSON (version + search_config).\n"
            << "                     Repeatable: each --config runs against the same built index.\n"
            << "  --config-dir <dir> Directory containing *.json configs (loaded in alphabetical order).\n"
            << "  --compare <file>   Compare results against baseline JSON output from a prior run.\n"
            << "  --max-regression-pct P Exit 1 if avg latency regressed by more than P% vs baseline.\n"
            << "  --fail-on-hit-mismatch Exit 1 if candidate hit count differs from baseline.\n"
            << "  --iterations N      Number of timed runs (default: " << kDefaultIterations << ")\n"
            << "  --warmup M          Number of warmup runs before timing (default: " << kDefaultWarmup
            << ")\n"
            << "  --measure-merge-sort  After the match loop, run one fresh search and time\n"
            << "                        merge (MergeAndConvertToSearchResults) + sort (Filename\n"
            << "                        ascending, the synchronous column with no attribute I/O).\n"
            << "                        Reports the merge+sort share of the full pipeline.\n"
            << "  --verify            Force the scan path (both indexes not-ready) and the\n"
            << "                      normal path, then diff the ID sets. Exit 1 on mismatch.\n"
            << "  --expect N          Require exactly N matches (single --config only).\n"
            << "                      Exit 1 on mismatch. Turns a preset into a CI gate.\n"
            << "  --format MODE       Output mode: text (default prose), csv (one row per\n"
            << "                      config plus a header), or json (one object per line).\n"
            << "  --threads N         Search thread count: -1/0 = engine auto-detect\n"
            << "                      (default -1). Positive values used as-is.\n"
            << "  --config-inline     Treat every --config as literal JSON instead of\n"
            << "                      sniffing file-vs-inline (see IsLikelyFilePath).\n"
            << "  --quiet, -q         Suppress human banner blocks and progress lines so stdout\n"
            << "                      emits only clean machine rows (CSV/JSON) or minimal output.\n"
            << "Exit codes: 0 all green; 1 config/parse error, unstable counts across\n"
            << "  iterations, --expect mismatch, --verify diff, hit count mismatch, or regression.\n";
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

std::string LoadConfigString(std::string_view config_arg, bool force_inline) {
  if (force_inline) {
    return std::string(config_arg);
  }
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

// Attempts to extract expected hit count from the JSON config, checking either
// top-level "expected_count" or inside "search_config". Returns nullopt if missing.
[[nodiscard]] inline std::optional<size_t> ParseExpectedCountFromJson(std::string_view json_str) {
  try {
    const auto j = nlohmann::json::parse(json_str);
    if (j.contains("expected_count") && j["expected_count"].is_number_unsigned()) {
      return j["expected_count"].get<size_t>();
    }
    if (j.contains("search_config") && j["search_config"].is_object() &&
        j["search_config"].contains("expected_count") &&
        j["search_config"]["expected_count"].is_number_unsigned()) {
      return j["search_config"]["expected_count"].get<size_t>();
    }
  } catch (const std::exception& e) {
    (void)e;
    // Malformed JSON is handled by ParseSearchConfigJson.
  }
  return std::nullopt;
}

struct TimedSearchResult {
  std::vector<uint64_t> sorted_ids;  // sorted: order-insensitive compare across runs
  double elapsed_ms = 0.0;
};

// Runs one search and returns sorted hit ids with the elapsed wall time.
// The search execution (dispatch to thread pool + waiting for all worker
// futures) is timed; extracting hit IDs and sorting them is done outside the
// timed window so harness memory allocation does not pollute latency.
[[nodiscard]] inline TimedSearchResult RunTimedSearch(
    FileIndex& file_index, const BenchmarkQuery& query, int thread_count,
    std::vector<ThreadTiming>* thread_timings) {
  const auto start = std::chrono::high_resolution_clock::now();
  auto futures = file_index.SearchAsyncWithData(
      query.primary_query, thread_count, nullptr, query.path_query, query.extensions,
      ResolveItemTypeFilter(query.folders_only, query.files_only),
      query.case_sensitive, thread_timings, nullptr, nullptr);
  std::vector<SearchResultBatch> batches;
  batches.reserve(futures.size());
  for (auto& f : futures) {
    batches.push_back(f.get());
  }
  const auto end = std::chrono::high_resolution_clock::now();

  size_t total_hits = 0;
  for (const auto& batch : batches) {
    total_hits += batch.Size();
  }
  std::vector<uint64_t> ids;
  ids.reserve(total_hits);
  for (const auto& batch : batches) {
    for (const SearchResultHit& hit : batch.hits) {
      ids.push_back(hit.id);
    }
  }
  std::sort(ids.begin(), ids.end());  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
  return {std::move(ids),
          std::chrono::duration<double, std::milli>(end - start).count()};
}

// Prints sample missing and extra file paths when filter-vs-scan ID sets diverge.
void PrintVerifyDiff(const FileIndex& file_index,
                     const std::vector<uint64_t>& normal_ids,
                     const std::vector<uint64_t>& forced_scan_ids,
                     std::ostream& os = std::cout) {
  std::vector<uint64_t> missing_ids;
  std::set_difference(forced_scan_ids.begin(), forced_scan_ids.end(),
                      normal_ids.begin(), normal_ids.end(),
                      std::back_inserter(missing_ids));

  std::vector<uint64_t> extra_ids;
  std::set_difference(normal_ids.begin(), normal_ids.end(),
                      forced_scan_ids.begin(), forced_scan_ids.end(),
                      std::back_inserter(extra_ids));

  const PathStorage::SoAView soa_view = file_index.GetSearchableView();
  const size_t storage_size = file_index.GetStorageSize();

  const auto format_id_path = [&](uint64_t id) -> std::string {
    if (id < soa_view.size && soa_view.path_offsets != nullptr && soa_view.path_storage != nullptr) {
      const size_t len = soa_view.GetPathLength(id, storage_size);
      return {soa_view.path_storage + soa_view.path_offsets[id], len};
    }
    return "<unknown path>";
  };

  constexpr size_t kMaxSamples = 5;
  if (!missing_ids.empty()) {
    os << "  Missing in normal path (" << missing_ids.size()
       << " false negatives):\n";
    const size_t print_count = (std::min)(missing_ids.size(), kMaxSamples);
    for (size_t i = 0; i < print_count; ++i) {
      os << "    ID " << missing_ids[i] << ": \""
         << format_id_path(missing_ids[i]) << "\"\n";
    }
    if (missing_ids.size() > print_count) {
      os << "    ... and " << (missing_ids.size() - print_count) << " more\n";
    }
  }
  if (!extra_ids.empty()) {
    os << "  Extra in normal path (" << extra_ids.size()
       << " false positives):\n";
    const size_t print_count = (std::min)(extra_ids.size(), kMaxSamples);
    for (size_t i = 0; i < print_count; ++i) {
      os << "    ID " << extra_ids[i] << ": \""
         << format_id_path(extra_ids[i]) << "\"\n";
    }
    if (extra_ids.size() > print_count) {
      os << "    ... and " << (extra_ids.size() - print_count) << " more\n";
    }
  }
}

// Forces the production scan fallback by marking both indexes non-ready,
// runs the query, then restores via AbortBuild and runs it normally; diffs
// the ID sets. Returns 0 on identical sets, 1 on mismatch. The normal run
// carries timings: exactly one entry proves the filter path was taken (a
// scan resizes to thread_count — ambiguous only for single-thread scans,
// which is labeled, not failed).
int VerifyFilterVsScan(FileIndex& file_index, const BenchmarkQuery& query, int thread_count,
                       const std::string& config_label, bool quiet = false) {
  std::vector<ThreadTiming> timings;
  const TimedSearchResult normal = RunTimedSearch(file_index, query, thread_count, &timings);
  const bool normal_took_filter = (timings.size() == 1);

  // Benchmark owns file_index; drives index lifecycle directly to force the
  // scan path. Mirrors the unit-test precedent for Mark/Abort pairing.
  // (Suppression comments live on the const_cast lines: NOLINTNEXTLINE does
  // not reach continuation lines.)
  auto* trigram_idx =
      const_cast<filename_trigram::FilenameTrigramIndex*>(file_index.GetTrigramIndex());  // NOLINT(cppcoreguidelines-pro-type-const-cast) - benchmark-owned index; forces the production scan-fallback path for --verify
  auto* extension_idx =
      const_cast<extension_index::ExtensionIndex*>(file_index.GetExtensionIndex());  // NOLINT(cppcoreguidelines-pro-type-const-cast) - see above
  const uint64_t trigram_gen = (trigram_idx != nullptr) ? trigram_idx->MarkBuilding() : 0;
  const uint64_t extension_gen = (extension_idx != nullptr) ? extension_idx->MarkBuilding() : 0;

  const TimedSearchResult forced_scan = RunTimedSearch(file_index, query, thread_count, nullptr);

  if (trigram_idx != nullptr) {
    trigram_idx->AbortBuild(trigram_gen);
  }
  if (extension_idx != nullptr) {
    extension_idx->AbortBuild(extension_gen);
  }

  if (!quiet) {
    std::cout << "=== Verify (" << config_label << ") ===\n";
    std::cout << "Normal path: " << (normal_took_filter ? "filter" : "scan-or-ambiguous")
              << ", " << normal.sorted_ids.size() << " hits in " << normal.elapsed_ms << " ms\n";
    std::cout << "Forced scan: " << forced_scan.sorted_ids.size() << " hits in "
              << forced_scan.elapsed_ms << " ms\n";
  }
  if (normal.sorted_ids == forced_scan.sorted_ids) {
    if (!quiet) {
      std::cout << "ID sets identical\n";
    }
    return 0;
  }
  std::ostream& err_os = quiet ? std::cerr : std::cout;
  err_os << "MISMATCH: filter-path and scan-path ID sets differ ("
         << normal.sorted_ids.size() << " vs " << forced_scan.sorted_ids.size() << ")\n";
  PrintVerifyDiff(file_index, normal.sorted_ids, forced_scan.sorted_ids, err_os);
  return 1;
}

// Both candidate-filter indexes build asynchronously (FilenameTrigramIndex::
// RebuildAsync and its extension twin) while searches fall back to full
// scans. Wait for readiness so timings reflect the steady-state indexed path
// rather than the startup race.
[[nodiscard]] bool WaitForIndexesReady(FileIndex& file_index) {
  constexpr int kPollIntervalMs = 50;
  constexpr int kTimeoutMs = 30000;
  for (int waited_ms = 0; waited_ms < kTimeoutMs; waited_ms += kPollIntervalMs) {
    {
      const std::shared_lock lock(file_index.GetMutex());
      const auto* trigram_idx = file_index.GetTrigramIndex();
      const auto* extension_idx = file_index.GetExtensionIndex();
      if ((trigram_idx == nullptr || trigram_idx->IsReady()) &&
          (extension_idx == nullptr || extension_idx->IsReady())) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
  }
  return false;
}

struct ParsedArgs {
  std::string index_path;
  std::vector<std::string> config_args;  // Repeatable --config / --config-dir entries
  std::string compare_path;              // Optional baseline JSON file to compare against
  int iterations = kDefaultIterations;
  int warmup = kDefaultWarmup;
  bool measure_merge_sort = false;
  bool verify = false;
  bool has_expect = false;
  size_t expect_count = 0;
  std::string format = "text";  // text | csv | json (machine modes print rows, not prose)
  int thread_count = -1;  // -1/0 = engine auto-detect; >0 explicit (see DetermineThreadCount)
  bool config_inline = false;  // treat every --config as literal JSON, skip file sniffing
  bool quiet = false;  // suppress prose banners and progress for clean machine stdout
  double max_regression_pct = -1.0;  // negative = disabled; positive = max allowed % regression vs baseline
  bool fail_on_hit_mismatch = false;  // fail if hits differ from baseline
  bool help = false;
};

// Advances i and returns pointer to next argv slot, or nullptr if missing (prints err_msg).
const char* RequireNextArg(int argc, char** argv, int& i, const char* err_msg) {
  if (i + 1 >= argc) {
    std::cerr << err_msg << "\n";
    return nullptr;
  }

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
  out.config_args.emplace_back(val);
  return 0;
}

// Parses base-10 size_t into *out; returns false (and prints err_msg) for
// empty input, leading '-', non-digit trailers, or overflow. strtoull
// saturation plus ERANGE covers overflow (size_t is 64-bit on all
// supported targets); the explicit '-' check closes strtoull's wraparound.
[[nodiscard]] inline bool ParseCliSizeT(const char* val, size_t& out, const char* err_msg) {
  static_assert(sizeof(size_t) >= 8, "size_t must hold the full strtoull range on 64-bit targets");
  if (val[0] == '-') {
    std::cerr << err_msg << "\n";
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const auto n = std::strtoull(val, &end, 10);
  if (end == val || *end != '\0' || errno == ERANGE) {
    std::cerr << err_msg << "\n";
    return false;
  }
  out = static_cast<size_t>(n);
  return true;
}

int HandleIterations(int argc, char** argv, int& i, ParsedArgs& out) {
  const char* val = RequireNextArg(argc, argv, i, "Error: --iterations requires a number");
  if (val == nullptr) {
    return 1;
  }
  out.iterations = (std::max)(ParseCliInt(val, 0), 1);
  return 0;
}

int HandleWarmup(int argc, char** argv, int& i, ParsedArgs& out) {
  const char* val = RequireNextArg(argc, argv, i, "Error: --warmup requires a number");
  if (val == nullptr) {
    return 1;
  }
  out.warmup = (std::max)(ParseCliInt(val, 0), 0);
  return 0;
}

int HandleVerify(ParsedArgs& out) {
  out.verify = true;
  return 0;
}

int HandleExpect(int argc, char** argv, int& i, ParsedArgs& out) {
  const char* val = RequireNextArg(argc, argv, i, "Error: --expect requires a number");
  if (val == nullptr) {
    return 1;
  }
  // expect=0 is legitimate (e.g. nomatch queries).
  if (!ParseCliSizeT(val, out.expect_count, "Error: --expect requires a non-negative integer")) {
    return 1;
  }
  out.has_expect = true;
  return 0;
}

int HandleFormat(int argc, char** argv, int& i, ParsedArgs& out) {
  const char* val = RequireNextArg(argc, argv, i, "Error: --format requires text, csv, or json");
  if (val == nullptr) {
    return 1;
  }
  const std::string_view mode(val);
  if (mode != "text" && mode != "csv" && mode != "json") {
    std::cerr << "Error: --format must be text, csv, or json\n";
    return 1;
  }
  out.format = std::string(mode);
  return 0;
}

int HandleThreads(int argc, char** argv, int& i, ParsedArgs& out) {
  const char* val = RequireNextArg(argc, argv, i, "Error: --threads requires a number");
  if (val == nullptr) {
    return 1;
  }
  const int n = ParseCliInt(val, -2);
  if (n < -1) {
    std::cerr << "Error: --threads requires -1 (auto) or a positive count\n";
    return 1;
  }
  out.thread_count = n;
  return 0;
}

int HandleConfigInline(ParsedArgs& out) {
  out.config_inline = true;
  return 0;
}

int HandleConfigDir(int argc, char** argv, int& i, ParsedArgs& out) {
  const char* val = RequireNextArg(argc, argv, i, "Error: --config-dir requires a directory path");
  if (val == nullptr) {
    return 1;
  }
  const std::filesystem::path dir_path(val);
  std::error_code ec;
  if (!std::filesystem::is_directory(dir_path, ec)) {
    std::cerr << "Error: --config-dir '" << val << "' is not a valid directory\n";
    return 1;
  }
  std::vector<std::string> collected_files;
  for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec)) {
    if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
      collected_files.push_back(entry.path().string());
    }
  }
  if (collected_files.empty()) {
    std::cerr << "Error: no .json configuration files found in directory: " << val << "\n";
    return 1;
  }
  std::sort(collected_files.begin(), collected_files.end());  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
  for (auto& path_str : collected_files) {
    out.config_args.push_back(std::move(path_str));
  }
  return 0;
}

int HandleCompare(int argc, char** argv, int& i, ParsedArgs& out) {
  const char* val =
      RequireNextArg(argc, argv, i, "Error: --compare requires a baseline JSON file path");
  if (val == nullptr) {
    return 1;
  }
  out.compare_path = val;
  return 0;
}

int HandleMaxRegression(int argc, char** argv, int& i, ParsedArgs& out) {
  const char* val =
      RequireNextArg(argc, argv, i, "Error: --max-regression-pct requires a percentage");
  if (val == nullptr) {
    return 1;
  }
  char* end = nullptr;
  errno = 0;
  const double pct = std::strtod(val, &end);
  if (val[0] == '\0' || end == val || *end != '\0' || errno == ERANGE || pct < 0.0) {
    std::cerr << "Error: --max-regression-pct requires a non-negative number\n";
    return 1;
  }
  out.max_regression_pct = pct;
  return 0;
}

// Dispatches one option. Returns: -2 = help (set out.help, exit 0), -1 = unknown, 0 = consumed, 1 = error.
int DispatchOption(std::string_view arg, int argc, char** argv, int& i, ParsedArgs& out) {
  if (arg == "--help" || arg == "-h") {
    out.help = true;
    return -2;
  }
  if (arg == "--measure-merge-sort") {
    out.measure_merge_sort = true;
    return 0;
  }
  if (arg == "--verify") {
    return HandleVerify(out);
  }
  if (arg == "--expect") {
    return HandleExpect(argc, argv, i, out);
  }
  if (arg == "--format") {
    return HandleFormat(argc, argv, i, out);
  }
  if (arg == "--threads") {
    return HandleThreads(argc, argv, i, out);
  }
  if (arg == "--config-inline") {
    return HandleConfigInline(out);
  }
  if (arg == "--config-dir") {
    return HandleConfigDir(argc, argv, i, out);
  }
  if (arg == "--compare") {
    return HandleCompare(argc, argv, i, out);
  }
  if (arg == "--max-regression-pct") {
    return HandleMaxRegression(argc, argv, i, out);
  }
  if (arg == "--fail-on-hit-mismatch") {
    out.fail_on_hit_mismatch = true;
    return 0;
  }
  if (arg == "--quiet" || arg == "-q") {
    out.quiet = true;
    return 0;
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
  return -1;
}

// Returns 0 if --help (caller should exit 0), 1 on parse error, 2 on success.
int ParseArgs(int argc, char** argv, ParsedArgs& out) {
  for (int i = 1; i < argc; ++i) {  // NOSONAR(cpp:S924) - Standard argv iteration; single loop

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
  double p50_ms = 0.0;
  double p95_ms = 0.0;
  double stddev_ms = 0.0;
  double cv_percent = 0.0;
  size_t result_count = 0;
};

struct BaselineEntry {
  std::string config;
  BenchmarkStats stats;
};

struct ComparisonDelta {
  double speedup_avg = 0.0;
  double delta_avg_pct = 0.0;
  double speedup_p50 = 0.0;
  double delta_p50_pct = 0.0;
};

[[nodiscard]] inline ComparisonDelta ComputeComparison(
    const BenchmarkStats& current, const BenchmarkStats& baseline) {
  ComparisonDelta d;
  if (current.avg_ms > 0.0) {
    d.speedup_avg = baseline.avg_ms / current.avg_ms;
  }
  if (baseline.avg_ms > 0.0) {
    d.delta_avg_pct = ((current.avg_ms - baseline.avg_ms) / baseline.avg_ms) * 100.0;
  }
  if (current.p50_ms > 0.0) {
    d.speedup_p50 = baseline.p50_ms / current.p50_ms;
  }
  if (baseline.p50_ms > 0.0) {
    d.delta_p50_pct = ((current.p50_ms - baseline.p50_ms) / baseline.p50_ms) * 100.0;
  }
  return d;
}

[[nodiscard]] inline std::vector<BaselineEntry> LoadBaselineEntries(const std::string& path) {
  const std::string content = ReadFileToString(path);
  if (content.empty()) {
    std::cerr << "Error: could not read baseline file: " << path << "\n";
    return {};
  }
  std::vector<BaselineEntry> entries;
  const auto parse_entry = [](const nlohmann::json& obj, BaselineEntry& out) -> bool {
    if (!obj.is_object()) {
      return false;
    }
    out.config = obj.value("config", "");
    out.stats.result_count = obj.value("hits", size_t{0});
    out.stats.min_ms = obj.value("min_ms", 0.0);
    out.stats.avg_ms = obj.value("avg_ms", 0.0);
    out.stats.max_ms = obj.value("max_ms", 0.0);
    out.stats.p50_ms = obj.value("p50_ms", 0.0);
    out.stats.p95_ms = obj.value("p95_ms", 0.0);
    out.stats.stddev_ms = obj.value("stddev_ms", 0.0);
    out.stats.cv_percent = obj.value("cv_percent", 0.0);
    return true;
  };

  try {
    if (const auto j = nlohmann::json::parse(content, nullptr, false); !j.is_discarded()) {
      if (j.is_array()) {
        for (const auto& item : j) {
          BaselineEntry entry;
          if (parse_entry(item, entry)) {
            entries.push_back(std::move(entry));
          }
        }
        return entries;
      }
      if (j.is_object() && j.value("type", "") == "config") {
        BaselineEntry entry;
        if (parse_entry(j, entry)) {
          entries.push_back(std::move(entry));
        }
        return entries;
      }
    }
  } catch (const std::exception& e) {
    (void)e;
  }

  // Fallback: parse NDJSON (one JSON object per line, as produced by --format json)
  std::istringstream iss(content);
  std::string line;
  while (std::getline(iss, line)) {
    if (line.empty() || line.front() != '{') {
      continue;
    }
    try {
      if (const auto obj = nlohmann::json::parse(line, nullptr, false);
          !obj.is_discarded() && obj.is_object() && obj.value("type", "") == "config") {
        BaselineEntry entry;
        if (parse_entry(obj, entry)) {
          entries.push_back(std::move(entry));
        }
      }
    } catch (const std::exception& e) {
      (void)e;
    }
  }
  return entries;
}

[[nodiscard]] inline const BaselineEntry* FindMatchingBaseline(
    const std::vector<BaselineEntry>& entries,
    std::string_view candidate_label,
    size_t candidate_index) {
  if (entries.empty()) {
    return nullptr;
  }
  for (const auto& entry : entries) {
    if (entry.config == candidate_label) {
      return &entry;
    }
  }
  const std::filesystem::path cand_path(candidate_label);
  if (const std::string cand_filename = cand_path.filename().string();
      !cand_filename.empty() && cand_filename != "." && cand_filename != "/") {
    for (const auto& entry : entries) {
      if (std::filesystem::path(entry.config).filename().string() == cand_filename) {
        return &entry;
      }
    }
  }
  if (candidate_index < entries.size()) {
    return &entries[candidate_index];
  }
  return nullptr;
}

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

  if (times_ms.size() > 1) {
    double sum_sq_diff = 0.0;
    for (const double t : times_ms) {
      const double diff = t - stats.avg_ms;
      sum_sq_diff += diff * diff;
    }
    const double variance = sum_sq_diff / static_cast<double>(times_ms.size() - 1);
    stats.stddev_ms = std::sqrt(variance);
    if (stats.avg_ms > 0.0) {
      stats.cv_percent = (stats.stddev_ms / stats.avg_ms) * 100.0;
    }
  }

  // Nearest-rank percentiles over a sorted copy (n is tiny: iteration count).
  // Integer arithmetic: rank = ceil(pct*n/100), index = rank-1.
  std::vector<double> sorted = times_ms;
  std::sort(sorted.begin(), sorted.end());  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
  const auto rank_at = [&sorted](uint64_t pct) {
    const size_t r = (pct * sorted.size() + 99) / 100;
    return sorted[(r == 0) ? 0 : r - 1];
  };
  stats.p50_ms = rank_at(50);
  stats.p95_ms = rank_at(95);
  return stats;
}

void PrintStats(const BenchmarkStats& stats, int iterations,
                const BaselineEntry* baseline = nullptr) {
  std::cout << "Results: " << stats.result_count << " matches\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Time (ms): min=" << stats.min_ms << " avg=" << stats.avg_ms << " max=" << stats.max_ms;
  if (iterations > 1) {
    std::cout << " stddev=" << stats.stddev_ms << " (cv=" << stats.cv_percent << "%) ("
              << iterations << " runs)";
  }
  std::cout << "\n";
  std::cout << "Percentiles (ms): p50=" << stats.p50_ms << " p95=" << stats.p95_ms
            << " (nearest-rank, n=" << iterations << ")\n";

  if (baseline != nullptr) {
    const ComparisonDelta d = ComputeComparison(stats, baseline->stats);
    std::cout << "Comparison vs baseline (" << baseline->config << "):\n"
              << "  avg: " << baseline->stats.avg_ms << " -> " << stats.avg_ms << " ms ("
              << (d.delta_avg_pct >= 0.0 ? "+" : "") << d.delta_avg_pct << "%, "
              << d.speedup_avg << "x speedup)\n"
              << "  p50: " << baseline->stats.p50_ms << " -> " << stats.p50_ms << " ms ("
              << (d.delta_p50_pct >= 0.0 ? "+" : "") << d.delta_p50_pct << "%, "
              << d.speedup_p50 << "x speedup)\n";
    if (baseline->stats.result_count != stats.result_count) {
      std::cout << "  WARNING: hit count difference: baseline=" << baseline->stats.result_count
                << " current=" << stats.result_count << "\n";
    }
  }
}

void PrintConfigCsv(const std::string& label, const BenchmarkStats& stats, int iterations,
                    const BaselineEntry* baseline = nullptr) {
  std::cout << "\"";
  for (const char c : label) {
    if (c == '"') {
      std::cout << '"';
    }
    std::cout << c;
  }
  std::cout << "\"," << stats.result_count
            << std::fixed << std::setprecision(2)
            << "," << stats.min_ms << "," << stats.avg_ms << "," << stats.max_ms
            << "," << stats.p50_ms << "," << stats.p95_ms
            << "," << stats.stddev_ms << "," << stats.cv_percent
            << "," << iterations;
  if (baseline != nullptr) {
    const ComparisonDelta d = ComputeComparison(stats, baseline->stats);
    std::cout << "," << baseline->stats.avg_ms << "," << d.delta_avg_pct << "," << d.speedup_avg;
  }
  std::cout << "\n";
}

// Minimal JSON string escaping for labels (paths, inline JSON).
[[nodiscard]] inline std::string JsonEscape(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 2);
  for (const char c : value) {
    switch (c) {
      case '"': out.append("\\\""); break;
      case '\\': out.append("\\\\"); break;
      case '\b': out.append("\\b"); break;
      case '\f': out.append("\\f"); break;
      case '\n': out.append("\\n"); break;
      case '\r': out.append("\\r"); break;
      case '\t': out.append("\\t"); break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

void PrintConfigJson(const std::string& label, const BenchmarkStats& stats,
                     const std::vector<double>& times_ms,
                     const BaselineEntry* baseline = nullptr) {
  std::cout << "{\"type\":\"config\",\"config\":\"" << JsonEscape(label) << "\""
            << ",\"hits\":" << stats.result_count
            << std::fixed << std::setprecision(2)
            << ",\"min_ms\":" << stats.min_ms << ",\"avg_ms\":" << stats.avg_ms
            << ",\"max_ms\":" << stats.max_ms << ",\"p50_ms\":" << stats.p50_ms
            << ",\"p95_ms\":" << stats.p95_ms
            << ",\"stddev_ms\":" << stats.stddev_ms
            << ",\"cv_percent\":" << stats.cv_percent;
  if (baseline != nullptr) {
    const ComparisonDelta d = ComputeComparison(stats, baseline->stats);
    std::cout << ",\"baseline\":{\"config\":\"" << JsonEscape(baseline->config) << "\""
              << ",\"hits\":" << baseline->stats.result_count
              << ",\"avg_ms\":" << baseline->stats.avg_ms
              << ",\"p50_ms\":" << baseline->stats.p50_ms << "}"
              << ",\"speedup_avg\":" << d.speedup_avg
              << ",\"delta_avg_percent\":" << d.delta_avg_pct
              << ",\"speedup_p50\":" << d.speedup_p50
              << ",\"delta_p50_percent\":" << d.delta_p50_pct;
  }
  std::cout << ",\"iterations_ms\":[";
  for (size_t k = 0; k < times_ms.size(); ++k) {
    if (k > 0) {
      std::cout << ",";
    }
    std::cout << times_ms[k];
  }
  std::cout << "]}\n";
}

// Process peak RSS in bytes (ru_maxrss units differ: bytes on macOS,
// kilobytes on Linux; Windows uses PeakWorkingSetSize). Returns 0 when
// unavailable.
[[nodiscard]] inline uint64_t PeakRssBytes() noexcept {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS_EX pmc = {0};
  pmc.cb = sizeof(pmc);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),  // NOSONAR(cpp:S3630)
                           sizeof(pmc))) {
    return static_cast<uint64_t>(pmc.PeakWorkingSetSize);
  }
  return 0;
#elif defined(__APPLE__) || defined(__linux__)
  if (struct rusage usage = {}; ::getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
    return static_cast<uint64_t>(usage.ru_maxrss);
#else
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024U;
#endif
  }
  return 0;
#else
  return 0;
#endif
}

constexpr int kMergeSortSortIterations = 3;

// Full-pipeline timing for one fresh search: match (collect batches) + merge
// (MergeAndConvertToSearchResults) + sort (Filename ascending — the
// synchronous column with no attribute I/O, so this isolates row-move cost
// from filesystem latency). Prints the phase breakdown and the merge+sort
// share of the pipeline. Returns 0 on success, 1 on row-count/order mismatch.
int MeasureMergeAndSort(FileIndex& file_index, const BenchmarkQuery& query,
                        int thread_count) {
  const auto match_start = std::chrono::high_resolution_clock::now();
  auto futures = file_index.SearchAsyncWithData(
      query.primary_query, thread_count, nullptr, query.path_query, query.extensions,
      ResolveItemTypeFilter(query.folders_only, query.files_only), query.case_sensitive,
      nullptr, nullptr, nullptr);
  std::vector<SearchResultBatch> batches;
  batches.reserve(futures.size());
  size_t hit_count = 0;
  for (auto& f : futures) {
    batches.push_back(f.get());
    hit_count += batches.back().Size();
  }
  const auto match_end = std::chrono::high_resolution_clock::now();

  SearchResultMergeScratch scratch;
  std::vector<char> pool;
  const auto merge_start = std::chrono::high_resolution_clock::now();
  std::vector<SearchResult> results =
      MergeAndConvertToSearchResults(pool, batches, file_index, &scratch);
  const auto merge_end = std::chrono::high_resolution_clock::now();
  if (results.size() != hit_count) {
    std::cerr << "MISMATCH: merged " << results.size() << " rows from "
              << hit_count << " batch hits\n";
    return 1;
  }

  const auto comparator = CreateSearchResultComparator(
      ResultColumn::Filename, ImGuiSortDirection_Ascending, false);
  std::vector<double> sort_times_ms;
  sort_times_ms.reserve(static_cast<size_t>(kMergeSortSortIterations));
  uint64_t checksum = 0;
  for (int i = 0; i < kMergeSortSortIterations; ++i) {
    std::vector<SearchResult> work = results;  // Untimed setup: fresh unsorted copy
    const auto sort_start = std::chrono::high_resolution_clock::now();
    std::sort(work.begin(), work.end(), comparator);
    const auto sort_end = std::chrono::high_resolution_clock::now();
    if (!std::is_sorted(work.begin(), work.end(), comparator)) {
      std::cerr << "MISMATCH: sort iteration " << i << " is not sorted\n";
      return 1;
    }
    uint64_t iter_sum = 0;
    for (const SearchResult& r : work) {
      iter_sum += r.fileId + r.fullPath.size();
    }
    if (i == 0) {
      checksum = iter_sum;
    } else if (iter_sum != checksum) {
      std::cerr << "MISMATCH: sort checksum differs across iterations\n";
      return 1;
    }
    sort_times_ms.push_back(
        std::chrono::duration<double, std::milli>(sort_end - sort_start).count());
  }

  // Index-sort probe (design B): sort 4-byte indices with the same comparator,
  // then one out-of-place permute pass. Isolates row-move cost: if this matches
  // plain sort, moves are not the bottleneck and a hot/cold split cannot help.
  // Production would permute in place (cycle-chase, 1 temp row); the extra copy
  // pass here (~one memmove over the rows) is disclosed noise, ~10-20ms.
  std::vector<double> index_sort_times_ms;
  index_sort_times_ms.reserve(static_cast<size_t>(kMergeSortSortIterations));
  if (results.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
    std::cerr << "MISMATCH: row count exceeds uint32_t index range\n";
    return 1;
  }
  for (int i = 0; i < kMergeSortSortIterations; ++i) {
    std::vector<uint32_t> idx(results.size());
    std::iota(idx.begin(), idx.end(), uint32_t{0});
    const auto isort_start = std::chrono::high_resolution_clock::now();
    std::sort(idx.begin(), idx.end(), [&results, &comparator](uint32_t a, uint32_t b) {
      return comparator(results[a], results[b]);
    });
    std::vector<SearchResult> permuted;
    permuted.reserve(results.size());
    for (const uint32_t src : idx) {
      permuted.push_back(results[src]);
    }
    const auto isort_end = std::chrono::high_resolution_clock::now();
    if (!std::is_sorted(permuted.begin(), permuted.end(), comparator)) {
      std::cerr << "MISMATCH: index-sort iteration " << i << " is not sorted\n";
      return 1;
    }
    uint64_t iter_sum = 0;
    for (const SearchResult& r : permuted) {
      iter_sum += r.fileId + r.fullPath.size();
    }
    if (iter_sum != checksum) {
      std::cerr << "MISMATCH: index-sort checksum differs from plain sort\n";
      return 1;
    }
    index_sort_times_ms.push_back(
        std::chrono::duration<double, std::milli>(isort_end - isort_start).count());
  }

  const double match_ms =
      std::chrono::duration<double, std::milli>(match_end - match_start).count();
  const double merge_ms =
      std::chrono::duration<double, std::milli>(merge_end - merge_start).count();
  const BenchmarkStats sort_stats = ComputeStats(sort_times_ms, results.size());
  const BenchmarkStats index_sort_stats = ComputeStats(index_sort_times_ms, results.size());
  const double total_ms = match_ms + merge_ms + sort_stats.avg_ms;
  const double merge_sort_share =
      (total_ms > 0.0) ? (100.0 * (merge_ms + sort_stats.avg_ms) / total_ms) : 0.0;
  std::cout << "=== Merge+sort (1 merge, " << kMergeSortSortIterations
            << " sorts, Filename asc) ===\n";
  std::cout << "Rows: " << results.size() << " (checksum " << checksum << ")\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Match: " << match_ms << " ms\n";
  std::cout << "Merge: " << merge_ms << " ms\n";
  std::cout << "Sort: min=" << sort_stats.min_ms << " avg=" << sort_stats.avg_ms
            << " max=" << sort_stats.max_ms << " ms\n";
  std::cout << "Index-sort (moves isolated): min=" << index_sort_stats.min_ms
            << " avg=" << index_sort_stats.avg_ms
            << " max=" << index_sort_stats.max_ms << " ms\n";
  std::cout << "Total pipeline: " << total_ms << " ms\n";
  std::cout << "Merge+sort share: " << std::setprecision(1) << merge_sort_share << "%\n";
  return 0;
}

// Per-config run options, grouped for S107.
struct BenchmarkRunOptions {
  int iterations = search_benchmark_detail::kDefaultIterations;
  int warmup = search_benchmark_detail::kDefaultWarmup;
  bool measure_merge_sort = false;
  bool verify = false;
  bool has_expect = false;
  size_t expect_count = 0;
  int thread_count = -1;
  std::string format = "text";  // text | csv | json
  bool config_inline = false;  // treat --config args as literal JSON
  bool quiet = false;          // suppress prose banners and progress for clean machine stdout
  const std::vector<search_benchmark_detail::BaselineEntry>* baseline_entries = nullptr;
  double max_regression_pct = -1.0;  // negative = disabled; positive = max allowed % regression vs baseline
  bool fail_on_hit_mismatch = false;  // fail if hits differ from baseline
};

// Runs one --config entry against the already-built index: warmup, timed
// iterations with cross-iteration ID-set stability, optional --expect and
// --verify, optional merge+sort measurement. Returns 0 on success.
int RunOneConfig(FileIndex& file_index, std::string_view config_arg, size_t config_index,
                 size_t config_total, const BenchmarkRunOptions& options) {
  using search_benchmark_detail::BenchmarkQuery;
  using search_benchmark_detail::ComputeStats;
  using search_benchmark_detail::LoadConfigString;
  using search_benchmark_detail::MeasureMergeAndSort;
  using search_benchmark_detail::PrintConfigCsv;
  using search_benchmark_detail::PrintConfigJson;
  using search_benchmark_detail::PrintSearchParameters;
  using search_benchmark_detail::PrintStats;
  using search_benchmark_detail::RunTimedSearch;
  using search_benchmark_detail::TimedSearchResult;
  using search_benchmark_detail::VerifyFilterVsScan;

  const std::string config_str = LoadConfigString(config_arg, options.config_inline);
  if (config_str.empty()) {
    return 1;
  }

  const gemini_api_utils::GeminiApiResult api_result = gemini_api_utils::ParseSearchConfigJson(config_str);
  if (!api_result.success) {
    std::cerr << "Error: failed to parse config: " << api_result.error_message << "\n";
    return 1;
  }

  const gemini_api_utils::SearchConfig& config = api_result.search_config;
  const BenchmarkQuery query{config.filename,
                             config.path,
                             config.extensions.empty() ? nullptr : &config.extensions,
                             config.folders_only,
                             config.files_only,
                             config.case_sensitive};
  std::string config_source_label{"(inline JSON)"};
  if (!options.config_inline && search_benchmark_detail::IsLikelyFilePath(config_arg)) {
    config_source_label.assign(config_arg);
  }
  if (!options.quiet) {
    std::cout << "=== Config " << (config_index + 1) << "/" << config_total << ": "
              << config_source_label << " ===\n";
    PrintSearchParameters(query, {config.time_filter, config.size_filter});
  }

  for (int w = 0; w < options.warmup; ++w) {
    (void)RunTimedSearch(file_index, query, options.thread_count, nullptr);
  }

  std::vector<double> times_ms;
  times_ms.reserve(static_cast<size_t>(options.iterations));
  std::vector<uint64_t> reference_ids;
  bool reference_set = false;
  bool stable = true;
  for (int i = 0; i < options.iterations; ++i) {
    TimedSearchResult run = RunTimedSearch(file_index, query, options.thread_count, nullptr);
    times_ms.push_back(run.elapsed_ms);
    if (!reference_set) {
      reference_ids = std::move(run.sorted_ids);
      reference_set = true;
    } else if (run.sorted_ids != reference_ids) {
      stable = false;
    }
  }

  if (!options.quiet) {
    std::cout << "Iterations (ms):";
    std::cout << std::fixed << std::setprecision(2);
    for (const double t : times_ms) {
      std::cout << " " << t;
    }
    std::cout << "\n";
  }
  if (!stable) {
    std::ostream& err_os = options.quiet ? std::cerr : std::cout;
    err_os << "MISMATCH: ID sets differ across iterations (flaky index state?)\n";
    return 1;
  }

  const search_benchmark_detail::BaselineEntry* baseline = nullptr;
  if (options.baseline_entries != nullptr && !options.baseline_entries->empty()) {
    baseline = search_benchmark_detail::FindMatchingBaseline(
        *options.baseline_entries, config_source_label, config_index);
  }

  const search_benchmark_detail::BenchmarkStats stats =
      ComputeStats(times_ms, reference_ids.size());
  if (options.format == "csv") {
    PrintConfigCsv(config_source_label, stats, options.iterations, baseline);
  } else if (options.format == "json") {
    PrintConfigJson(config_source_label, stats, times_ms, baseline);
  } else if (!options.quiet) {
    PrintStats(stats, options.iterations, baseline);
  }

  if (baseline != nullptr) {
    if (options.fail_on_hit_mismatch && baseline->stats.result_count != stats.result_count) {
      std::ostream& err_os = options.quiet ? std::cerr : std::cout;
      err_os << "MISMATCH: hit count differs from baseline (expected "
             << baseline->stats.result_count << ", got " << stats.result_count << ")\n";
      return 1;
    }
    if (options.max_regression_pct >= 0.0) {
      if (const ComparisonDelta d = ComputeComparison(stats, baseline->stats);
          d.delta_avg_pct > options.max_regression_pct) {
        std::ostream& err_os = options.quiet ? std::cerr : std::cout;
        err_os << "REGRESSION: latency regressed by " << std::fixed << std::setprecision(1)
               << d.delta_avg_pct << "% (allowed threshold: " << options.max_regression_pct << "%)\n";
        return 1;
      }
    }
  }

  const std::optional<size_t> config_expected =
      search_benchmark_detail::ParseExpectedCountFromJson(config_str);
  if (const bool has_expect = options.has_expect || config_expected.has_value(); has_expect) {
    if (const size_t expect_count =
            options.has_expect ? options.expect_count : config_expected.value_or(0);
        reference_ids.size() != expect_count) {
      std::ostream& err_os = options.quiet ? std::cerr : std::cout;
      err_os << "MISMATCH: expected " << expect_count << " matches, got "
             << reference_ids.size() << "\n";
      return 1;
    }
  }

  if (options.verify) {
    const int verify_rc =
        VerifyFilterVsScan(file_index, query, options.thread_count, config_source_label, options.quiet);
    if (verify_rc != 0) {
      return verify_rc;
    }
  }

  if (options.measure_merge_sort) {
    return MeasureMergeAndSort(file_index, query, options.thread_count);
  }
  return 0;
}

}  // namespace search_benchmark_detail

// NOLINTNEXTLINE(bugprone-exception-escape) - Benchmark tool; exceptions from ParseSearchConfigJson etc. are acceptable
int main(int argc, char** argv) {
  using search_benchmark_detail::BenchmarkRunOptions;
  using search_benchmark_detail::ParsedArgs;
  using search_benchmark_detail::PrintEnvironmentInfo;
  using search_benchmark_detail::PrintRunParameters;
  using search_benchmark_detail::PrintUsage;
  using search_benchmark_detail::RunOneConfig;

  ParsedArgs parsed;

  const int parse_result = ParseArgs(argc, argv, parsed);
  if (parse_result == 0) {

    PrintUsage(argv[0]);
    return 0;
  }
  if (parse_result == 1) {

    PrintUsage(argv[0]);
    return 1;
  }
  if (parsed.index_path.empty() || parsed.config_args.empty()) {
    std::cerr << "Error: --index and at least one --config are required\n";

    PrintUsage(argv[0]);
    return 1;
  }
  if (parsed.has_expect && parsed.config_args.size() != 1) {
    std::cerr << "Error: --expect requires exactly one --config\n";

    PrintUsage(argv[0]);
    return 1;
  }

  const AppSettings benchmark_settings;
  test_settings::SetInMemorySettings(benchmark_settings);

  if (!parsed.quiet) {
    PrintEnvironmentInfo();
    PrintRunParameters(parsed.index_path, "(see per-config labels below)", parsed.iterations,
                       parsed.warmup);
    if (parsed.measure_merge_sort) {
      std::cout << "merge+sort measurement: ON (Filename asc)\n";
    }
    if (parsed.verify) {
      std::cout << "verify mode: ON (scan-vs-filter ID-set diff per config)\n";
    }
  }

  auto thread_pool = std::make_shared<ThreadPool>(
      static_cast<int>(std::thread::hardware_concurrency()));
  if (thread_pool->GetThreadCount() == 0) {
    thread_pool = std::make_shared<ThreadPool>(4);
  }
  FileIndex file_index(thread_pool);

  if (!populate_index_from_file(parsed.index_path, file_index)) {
    std::cerr << "Error: failed to load index from: " << parsed.index_path << "\n";
    return 1;
  }
  const auto recompute_start = std::chrono::steady_clock::now();
  file_index.RecomputeAllPaths();
  const double recompute_s =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - recompute_start).count();
  const bool indexes_ready = search_benchmark_detail::WaitForIndexesReady(file_index);
  const auto format_mb = [](size_t bytes) {
    return static_cast<double>(bytes) / static_cast<double>(logger_constants::kBytesPerMB);
  };
  double trigram_mb = 0.0;
  double extension_mb = 0.0;
  if (indexes_ready) {
    trigram_mb = format_mb(file_index.GetTrigramIndexMemoryBytes());
    extension_mb = format_mb(file_index.GetExtensionIndexMemoryBytes());
  }
  if (!parsed.quiet) {
    std::cout << "Index size: " << file_index.Size() << " paths\n";
    std::cout << "Candidate indexes ready: "
              << (indexes_ready ? "yes" : "no (timed out, scans run unindexed)") << "\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Index build summary: recompute " << recompute_s << " s (phase split in log)";
    if (indexes_ready) {
      std::cout << std::setprecision(2);
      std::cout << ", trigram " << trigram_mb << " MB, ";
      std::cout << "extension " << extension_mb << " MB";
    } else {
      std::cout << " (index memory unknown: build still in flight)";
    }
    std::cout << "\n";
  }

  std::vector<search_benchmark_detail::BaselineEntry> baseline_entries;
  if (!parsed.compare_path.empty()) {
    baseline_entries = search_benchmark_detail::LoadBaselineEntries(parsed.compare_path);
    if (baseline_entries.empty()) {
      std::cerr << "Error: no valid benchmark entries loaded from baseline file: "
                << parsed.compare_path << "\n";
      return 1;
    }
    if (!parsed.quiet) {
      std::cout << "Baseline comparison: " << parsed.compare_path << " ("
                << baseline_entries.size() << " entries loaded)\n";
    }
  }

  const BenchmarkRunOptions options{parsed.iterations,
                                    parsed.warmup,
                                    parsed.measure_merge_sort,
                                    parsed.verify,
                                    parsed.has_expect,
                                    parsed.expect_count,
                                    parsed.thread_count,
                                    parsed.format,
                                    parsed.config_inline,
                                    parsed.quiet,
                                    &baseline_entries,
                                    parsed.max_regression_pct,
                                    parsed.fail_on_hit_mismatch};
  if (parsed.format == "csv") {
    std::cout << "config,hits,min_ms,avg_ms,max_ms,p50_ms,p95_ms,stddev_ms,cv_percent,iterations";
    if (!baseline_entries.empty()) {
      std::cout << ",baseline_avg_ms,delta_avg_percent,speedup_avg";
    }
    std::cout << "\n";
  }
  if (parsed.format == "json") {
    std::cout << "{\"type\":\"build\",\"index_size\":" << file_index.Size()
#ifdef NDEBUG
              << ",\"build\":\"Release\""
#else
              << ",\"build\":\"Debug\""
#endif
              << ",\"hw_threads\":" << std::thread::hardware_concurrency()
              << std::fixed << std::setprecision(1)
              << ",\"recompute_s\":" << recompute_s
              << std::setprecision(2)
              << ",\"trigram_mb\":" << trigram_mb
              << ",\"extension_mb\":" << extension_mb
              << "}\n";
  }
  int overall_rc = 0;
  for (size_t c = 0; c < parsed.config_args.size(); ++c) {
    if (const int rc = RunOneConfig(file_index, parsed.config_args[c], c,
                                    parsed.config_args.size(), options);
        rc != 0) {
      overall_rc = rc;
    }
  }
  // Machine contract: rows parse (^" for csv, ^{ for json); === blocks and
  // MISMATCH lines are human diagnostics (failures also set the exit code).
  const double peak_rss_mb =
      static_cast<double>(search_benchmark_detail::PeakRssBytes()) /
      static_cast<double>(logger_constants::kBytesPerMB);
  if (parsed.format == "json") {
    std::cout << "{\"type\":\"summary\",\"peak_rss_mb\":" << std::fixed << std::setprecision(2)
              << peak_rss_mb << "}\n";
  } else if (parsed.format == "csv") {
    if (!parsed.quiet) {
      std::cout << "# Peak RSS (process): " << std::fixed << std::setprecision(2) << peak_rss_mb
                << " MB\n";
    }
  } else {
    std::cout << "Peak RSS (process): " << std::fixed << std::setprecision(2) << peak_rss_mb
              << " MB\n";
  }
  return overall_rc;
}
