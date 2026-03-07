#include <algorithm>
#include <string>
#include <vector>

#include "core/Settings.h"
#include "doctest/doctest.h"
#include "search/LoadBalancingStrategyUtils.h"
#include "search/SearchTypes.h"
#include "utils/LoadBalancingStrategy.h"

namespace {

constexpr std::size_t kMinimumStrategyCount = 4U;

}  // namespace

TEST_SUITE("LoadBalancingStrategy factory and validation") {
  TEST_CASE("ValidateAndNormalizeStrategyName returns same name for valid strategies") {
    CHECK(ValidateAndNormalizeStrategyName("static") == "static");
    CHECK(ValidateAndNormalizeStrategyName("hybrid") == "hybrid");
    CHECK(ValidateAndNormalizeStrategyName("dynamic") == "dynamic");
    CHECK(ValidateAndNormalizeStrategyName("interleaved") == "interleaved");
  }

  TEST_CASE("ValidateAndNormalizeStrategyName returns default for invalid name") {
    const std::string default_name = GetDefaultStrategyName();
    CHECK(ValidateAndNormalizeStrategyName("invalid") == default_name);
    CHECK(ValidateAndNormalizeStrategyName("") == default_name);
    CHECK(ValidateAndNormalizeStrategyName("Unknown") == default_name);
  }

  TEST_CASE("GetDefaultStrategyName returns hybrid") {
    CHECK(GetDefaultStrategyName() == "hybrid");
  }

  TEST_CASE("GetAvailableStrategyNames returns expected strategies") {
    const auto names = GetAvailableStrategyNames();
    REQUIRE(names.size() >= kMinimumStrategyCount);
    CHECK(std::find(names.begin(), names.end(), "static") != names.end());
    CHECK(std::find(names.begin(), names.end(), "hybrid") != names.end());
    CHECK(std::find(names.begin(), names.end(), "dynamic") != names.end());
    CHECK(std::find(names.begin(), names.end(), "interleaved") != names.end());
#ifdef FAST_LIBS_BOOST
    CHECK(std::find(names.begin(), names.end(), "work_stealing") != names.end());
#endif  // FAST_LIBS_BOOST
  }
}  // TEST_SUITE("LoadBalancingStrategy factory and validation")

TEST_SUITE("LoadBalancingStrategyUtils") {
  TEST_CASE("GetLoadBalancingStrategyFromSettings returns enum from in-memory settings") {
    AppSettings settings;
    settings.loadBalancingStrategy = "static";
    test_settings::SetInMemorySettings(settings);
    CHECK(GetLoadBalancingStrategyFromSettings() == LoadBalancingStrategyType::Static);
    test_settings::ClearInMemorySettings();
  }

  TEST_CASE("GetLoadBalancingStrategyFromSettings returns Hybrid for hybrid setting") {
    AppSettings settings;
    settings.loadBalancingStrategy = "hybrid";
    test_settings::SetInMemorySettings(settings);
    CHECK(GetLoadBalancingStrategyFromSettings() == LoadBalancingStrategyType::Hybrid);
    test_settings::ClearInMemorySettings();
  }

  TEST_CASE("GetLoadBalancingStrategyFromSettings returns Dynamic for dynamic setting") {
    AppSettings settings;
    settings.loadBalancingStrategy = "dynamic";
    test_settings::SetInMemorySettings(settings);
    CHECK(GetLoadBalancingStrategyFromSettings() == LoadBalancingStrategyType::Dynamic);
    test_settings::ClearInMemorySettings();
  }

  TEST_CASE("GetLoadBalancingStrategyFromSettings returns Interleaved for interleaved setting") {
    AppSettings settings;
    settings.loadBalancingStrategy = "interleaved";
    test_settings::SetInMemorySettings(settings);
    CHECK(GetLoadBalancingStrategyFromSettings() == LoadBalancingStrategyType::Interleaved);
    test_settings::ClearInMemorySettings();
  }

  TEST_CASE("GetLoadBalancingStrategyFromSettings returns default for invalid setting") {
    AppSettings settings;
    settings.loadBalancingStrategy = "invalid";
    test_settings::SetInMemorySettings(settings);
    CHECK(GetLoadBalancingStrategyFromSettings() == LoadBalancingStrategyType::Hybrid);
    test_settings::ClearInMemorySettings();
  }

#ifdef FAST_LIBS_BOOST
  TEST_CASE("GetLoadBalancingStrategyFromSettings returns WorkStealing for work_stealing setting") {
    AppSettings settings;
    settings.loadBalancingStrategy = "work_stealing";
    test_settings::SetInMemorySettings(settings);
    CHECK(GetLoadBalancingStrategyFromSettings() == LoadBalancingStrategyType::WorkStealing);
    test_settings::ClearInMemorySettings();
  }
#endif  // FAST_LIBS_BOOST
}  // TEST_SUITE("LoadBalancingStrategyUtils")

