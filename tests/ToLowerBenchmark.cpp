#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "utils/StringSearch.h"

// Microbenchmark: legacy std::tolower-per-byte vs table vs branchless lowering.
//
// Compares three ways to lowercase a byte, on two workloads:
//   A. RawLower: lowercase a large buffer (isolates per-char cost).
//   B. InsensitiveContains: scalar case-insensitive substring search over
//      generated mixed-case paths (mirrors ShortPatternSearch / FullCompare
//      usage in the insensitive search path).
//
// Methodology: dispatch is via `if constexpr` on a tag, so the lowering
// operation is direct inline code in each loop — no per-byte callable
// indirection (which at -O0 would dominate and invalidate the comparison).
// The table pointer is hoisted out of the loops. "Std" is the frozen
// pre-table baseline (LowerLegacy); "Table" reads the production
// kAsciiLowerTable, exactly as production code does.
//
// Exits nonzero on any result mismatch between variants (validates the
// table/branchless replacements before real work starts). Timing is
// relative; the decision number comes from Release (-O3, inlined).

namespace {

using string_search::kAsciiLowerTable;

// Frozen pre-table baseline: the old ToLowerChar body. Kept so the
// benchmark remains a before/after guard after production moved to the table.
inline char LowerLegacy(unsigned char ch_value) {
  return static_cast<char>(std::tolower(ch_value));
}

enum class LowerKind : std::uint8_t { Std, Table, Branchless };

// table_or_null is dereferenced only when Kind == Table (discarded
// otherwise), so callers pass nullptr for other variants.
template <LowerKind Kind>
inline char LowerByte(char ch_value,
                      const std::array<char, 256>* table_or_null) {
  if constexpr (Kind == LowerKind::Std) {
    (void)table_or_null;
    return LowerLegacy(ch_value);
  } else if constexpr (Kind == LowerKind::Table) {
    return (*table_or_null)[static_cast<unsigned char>(ch_value)];
  } else {
    (void)table_or_null;
    const auto unsigned_value = static_cast<unsigned char>(ch_value);
    if (unsigned_value >= 'A' && unsigned_value <= 'Z') {
      return static_cast<char>(unsigned_value + ('a' - 'A'));
    }
    return ch_value;
  }
}

template <LowerKind Kind>
uint64_t LowerBuffer(const std::string& input, std::string& output,
                     const std::array<char, 256>* table_or_null) {
  output.resize(input.size());
  uint64_t checksum = 0;
  for (size_t i = 0; i < input.size(); ++i) {
    const char lowered = LowerByte<Kind>(input[i], table_or_null);
    output[i] = lowered;
    checksum += static_cast<unsigned char>(lowered);
  }
  return checksum;
}

// Scalar case-insensitive contains with compile-time lowering policy (same
// shape as ShortPatternSearch<CaseInsensitive> in StringSearch.h).
template <LowerKind Kind>
bool ContainsInsensitive(const std::string_view text,
                         const std::string_view pattern,
                         const std::array<char, 256>* table_or_null) {
  if (pattern.empty()) {
    return true;
  }
  if (text.size() < pattern.size()) {
    return false;
  }
  for (size_t i = 0; i <= text.size() - pattern.size(); ++i) {
    bool is_match = true;
    for (size_t j = 0; j < pattern.size(); ++j) {
      if (LowerByte<Kind>(text[i + j], table_or_null) !=
          LowerByte<Kind>(pattern[j], table_or_null)) {
        is_match = false;
        break;
      }
    }
    if (is_match) {
      return true;
    }
  }
  return false;
}

template <LowerKind Kind>
uint64_t CountSearchHits(const std::vector<std::string>& paths,
                         const std::vector<std::string>& patterns,
                         const std::array<char, 256>* table_or_null) {
  uint64_t hits = 0;
  for (const auto& path : paths) {
    for (const auto& pattern : patterns) {
      if (ContainsInsensitive<Kind>(path, pattern, table_or_null)) {
        ++hits;
      }
    }
  }
  return hits;
}

void AppendMixedCasePath(std::string& path, std::mt19937& gen) {
  static constexpr std::string_view kAlphabet =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_-.";
  std::uniform_int_distribution<size_t> dis_char(0, kAlphabet.size() - 1);
  std::uniform_int_distribution<size_t> dis_len(12, 96);
  const size_t len = dis_len(gen);
  for (size_t i = 0; i < len; ++i) {
    path.push_back(kAlphabet[dis_char(gen)]);
  }
}

struct TimedResult {
  std::int64_t total_us = 0;
  uint64_t sink = 0;
};

template <typename Func>
TimedResult TimeIterations(std::string_view name, int iterations,
                           const Func& func) {
  // Warmup (page in memory, settle branch predictors).
  const uint64_t warm = func();
  (void)warm;
  const auto start = std::chrono::high_resolution_clock::now();
  uint64_t sink = 0;
  for (int i = 0; i < iterations; ++i) {
    sink += func();
  }
  const auto end = std::chrono::high_resolution_clock::now();
  const std::int64_t total_us =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start)
          .count();
  const auto avg_us = static_cast<double>(total_us) / iterations;  // NOLINT(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions) - microsecond counts below 2^53 are exactly representable
  std::cout << std::left << std::setw(34) << name << ": " << std::right
            << std::setw(10) << total_us << " us total"
            << ", " << std::setw(10) << std::fixed << std::setprecision(2)
            << avg_us << " us/op" << std::defaultfloat << '\n';
  return TimedResult{total_us, sink};
}

void PrintSpeedup(std::string_view label, const TimedResult& base,
                  const TimedResult& table, const TimedResult& branchless) {
  // NOLINTs below: microsecond counts below 2^53 are exactly representable.
  const auto base_us = static_cast<double>(base.total_us);  // NOLINT(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions)
  const auto table_us = static_cast<double>(  // NOLINT(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions)
      (std::max)(std::int64_t{1}, table.total_us));
  const auto branchless_us = static_cast<double>(  // NOLINT(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions)
      (std::max)(std::int64_t{1}, branchless.total_us));
  std::cout << label << " speedup vs current: table x" << base_us / table_us
            << ", branchless x" << base_us / branchless_us << "\n";
}

}  // namespace

int main() {
  constexpr size_t kPathCount = 20000;
  constexpr int kRawIterations = 10;
  constexpr int kSearchIterations = 10;

  // Production table (validates the real code path); built once like production.
  const std::array<char, 256>* table_ptr = &kAsciiLowerTable;

  std::mt19937 gen(42);  // NOLINT(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp) - Fixed seed for reproducible benchmark corpus (not security)
  std::vector<std::string> paths;
  paths.reserve(kPathCount);
  std::string corpus;
  corpus.reserve(kPathCount * 72U);
  for (size_t i = 0; i < kPathCount; ++i) {
    std::string path = (i % 5 == 0) ? "C:/Users/Dev/Documents/" : "src/utils/";
    AppendMixedCasePath(path, gen);
    corpus += path;
    paths.push_back(path);
  }
  std::cout << "Corpus: " << paths.size() << " paths, " << corpus.size()
            << " bytes\n";

  // Mixed-case patterns: some hit often (force full compares), one misses.
  const std::vector<std::string> patterns = {"Main",   "CONTROLLER",
                                             "config", "SrC/UtIlS",
                                             "zzzqqq",};
  std::cout << "--------------------------------------------------------------------------------\n";

  // Workload A: raw lowering.
  std::cout << "[A] Lower " << corpus.size() << " bytes x" << kRawIterations
            << ":\n";
  std::string lowered_a;
  std::string lowered_b;
  std::string lowered_c;
  const TimedResult std_result = TimeIterations(
      "std::tolower (legacy)", kRawIterations,
      [&] { return LowerBuffer<LowerKind::Std>(corpus, lowered_a, nullptr); });
  const TimedResult table_result = TimeIterations(
      "256-entry table", kRawIterations, [&] {
        return LowerBuffer<LowerKind::Table>(corpus, lowered_b, table_ptr);
      });
  const TimedResult branchless_result = TimeIterations(
      "branchless ASCII", kRawIterations, [&] {
        return LowerBuffer<LowerKind::Branchless>(corpus, lowered_c, nullptr);
      });
  if (lowered_a != lowered_b || lowered_a != lowered_c) {
    std::cerr << "MISMATCH: lowered buffers differ between variants\n";
    return 1;
  }
  PrintSpeedup("Raw", std_result, table_result, branchless_result);

  // Workload B: insensitive substring search over all paths x patterns.
  std::cout << "--------------------------------------------------------------------------------\n";
  std::cout << "[B] Insensitive search " << paths.size() << " paths x "
            << patterns.size() << " patterns x" << kSearchIterations << ":\n";
  // Note: policy selected at compile time via explicit template args.
  const TimedResult search_std = TimeIterations(
      "search legacy", kSearchIterations,
      [&] { return CountSearchHits<LowerKind::Std>(paths, patterns, nullptr); });
  const TimedResult search_table = TimeIterations(
      "search table", kSearchIterations, [&] {
        return CountSearchHits<LowerKind::Table>(paths, patterns, table_ptr);
      });
  const TimedResult search_branchless = TimeIterations(
      "search branchless", kSearchIterations, [&] {
        return CountSearchHits<LowerKind::Branchless>(paths, patterns, nullptr);
      });
  if (search_std.sink != search_table.sink ||
      search_std.sink != search_branchless.sink) {
    std::cerr << "MISMATCH: hit counts differ between variants ("
              << search_std.sink << " vs " << search_table.sink << " vs "
              << search_branchless.sink << ")\n";
    return 1;
  }
  std::cout << "Hits: " << search_std.sink << " (identical across variants). ";
  PrintSpeedup("Search", search_std, search_table, search_branchless);

  // Keep the compiler honest: consume all sinks.
  volatile uint64_t keep = std_result.sink + table_result.sink +
                           branchless_result.sink + search_std.sink +
                           search_table.sink + search_branchless.sink;
  (void)keep;
  std::cout << "OK: all variants agree\n";
  return 0;
}
