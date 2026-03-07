#include "search/LoadBalancingStrategyUtils.h"

#include "core/Settings.h"
#include "utils/LoadBalancingStrategy.h"

LoadBalancingStrategyType GetLoadBalancingStrategyFromSettings() {
  AppSettings settings;
  LoadSettings(settings);

  const std::string validated_name =
      ValidateAndNormalizeStrategyName(settings.loadBalancingStrategy);
  if (validated_name == "static") {
    return LoadBalancingStrategyType::Static;
  }
  if (validated_name == "hybrid") {
    return LoadBalancingStrategyType::Hybrid;
  }
  if (validated_name == "dynamic") {
    return LoadBalancingStrategyType::Dynamic;
  }
  if (validated_name == "interleaved") {
    return LoadBalancingStrategyType::Interleaved;
  }
  if (validated_name == "work_stealing") {
    return LoadBalancingStrategyType::WorkStealing;
  }
  // Invalid or unknown strategy (ValidateAndNormalizeStrategyName returns "hybrid" as default)
  return LoadBalancingStrategyType::Hybrid;
}
