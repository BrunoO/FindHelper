#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "path/PathPatternMatcher.h"

// Simple benchmark utility
class Benchmark {
 public:
  template<typename Func>
  static void Run(std::string_view name, int iterations, Func&& func) {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
      func();
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    double avg_us = static_cast<double>(duration) / iterations;
    double ops_sec = 1000000.0 / avg_us;

    std::cout << std::left << std::setw(40) << name << ": " << std::right << std::setw(10)
              << duration << " us total"
              << ", " << std::setw(10) << std::fixed << std::setprecision(3) << avg_us << " us/op"
              << ", " << std::fixed << std::setprecision(2) << ops_sec << " ops/sec"
              << std::defaultfloat << std::endl;
  }
};

std::vector<std::string> GeneratePaths(size_t count) {
  std::vector<std::string> paths;
  paths.reserve(count);

  // Mix of paths
  std::vector<std::string> prefixes = {"src/", "include/", "tests/", "docs/", "build/", "data/"};
  std::vector<std::string> dirs = {"utils/", "core/", "ui/", "network/", "database/", "legacy/"};
  std::vector<std::string> files = {"main", "helper", "view", "controller", "model", "config"};
  std::vector<std::string> exts = {".cpp", ".h", ".txt", ".md", ".json", ".xml"};

  std::mt19937 gen(42);  // NOSONAR(cpp:S2245) - Fixed seed for benchmark reproducibility (not used for security)
  std::uniform_int_distribution<size_t> dis_prefix(0, prefixes.size() - 1);
  std::uniform_int_distribution<size_t> dis_dir(0, dirs.size() - 1);
  std::uniform_int_distribution<size_t> dis_file(0, files.size() - 1);
  std::uniform_int_distribution<size_t> dis_ext(0, exts.size() - 1);

  for (size_t i = 0; i < count; ++i) {
    std::string path;
    // 20% deep paths
    if (i % 5 == 0) {
      path += prefixes[dis_prefix(gen)];
      path += dirs[dis_dir(gen)];
      path += dirs[dis_dir(gen)];  // deeper
    } else {
      path += prefixes[dis_prefix(gen)];
      path += dirs[dis_dir(gen)];
    }
    path += files[dis_file(gen)];
    // Add random suffix to make unique
    path += std::to_string(i);
    path += exts[dis_ext(gen)];
    paths.push_back(path);
  }

  return paths;
}

int main() {
  constexpr size_t kPathCount = 100000;
  std::cout << "Generating " << kPathCount << " paths..." << std::endl;
  auto paths = GeneratePaths(kPathCount);

  std::cout << "Running benchmarks..." << std::endl;
  std::cout << "--------------------------------------------------------------------------------"
            << std::endl;

  using namespace path_pattern;

  // 1. Literal match (should ideally be fast if exact match optimized, currently likely DFA/NFA)
  {
    auto pattern = CompilePathPattern("src/core/main0.cpp", MatchOptions::kNone);
    Benchmark::Run("Literal Match (Exact)", 100, [&paths, &pattern]() {
      int matches = 0;
      for (const auto& path : paths) {
        if (PathPatternMatches(pattern, path))
          matches++;
      }
      volatile int keep = matches;  // NOSONAR(cpp:S3687) - volatile prevents compiler from optimizing away matches computation in benchmark
      (void)keep;
    });
  }

  // 2. Simple Wildcard (DFA optimized)
  {
    auto pattern = CompilePathPattern("src/**/*.cpp", MatchOptions::kNone);
    Benchmark::Run("Simple Wildcard (src/**/*.cpp)", 100, [&paths, &pattern]() {
      int matches = 0;
      for (const auto& path : paths) {
        if (PathPatternMatches(pattern, path))
          matches++;
      }
      volatile int keep = matches;  // NOSONAR(cpp:S3687) - volatile prevents compiler from optimizing away matches computation in benchmark
      (void)keep;
    });
  }

  // 3. Simple Wildcard Suffix (DFA optimized)
  {
    auto pattern = CompilePathPattern("**/*.txt", MatchOptions::kNone);
    Benchmark::Run("Extension Suffix (**/*.txt)", 100, [&paths, &pattern]() {
      int matches = 0;
      for (const auto& path : paths) {
        if (PathPatternMatches(pattern, path))
          matches++;
      }
      volatile int keep = matches;  // NOSONAR(cpp:S3687) - volatile prevents compiler from optimizing away matches computation in benchmark
      (void)keep;
    });
  }

  // 4. Advanced Pattern (NFA/Backtracking - no DFA)
  // [0-9] makes it 'advanced' in current implementation, using MatchFrom (backtracking)
  {
    auto pattern = CompilePathPattern("src/**/*[0-9].cpp", MatchOptions::kNone);
    Benchmark::Run("Advanced Pattern (src/**/*[0-9].cpp)", 20,
                   [&paths, &pattern]() {  // Less iterations as it might be slow
                     int matches = 0;
                     for (const auto& path : paths) {
                       if (PathPatternMatches(pattern, path))
                         matches++;
                     }
                     volatile int keep = matches;  // NOSONAR(cpp:S3687) - volatile prevents compiler from optimizing away matches computation in benchmark
                     (void)keep;
                   });
  }

  // 5. Literal within wildcards (Target for optimization)
  // "controller" is a literal that could be extracted
  {
    auto pattern = CompilePathPattern("**/*controller*.*", MatchOptions::kNone);
    Benchmark::Run("Literal embedded (**/*controller*.*)", 50, [&paths, &pattern]() {
      int matches = 0;
      for (const auto& path : paths) {
        if (PathPatternMatches(pattern, path))
          matches++;
      }
      volatile int keep = matches;  // NOSONAR(cpp:S3687) - volatile prevents compiler from optimizing away matches computation in benchmark
      (void)keep;
    });
  }

  return 0;
}
