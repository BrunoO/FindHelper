#pragma once

#include "search/SearchTypes.h"

/**
 * @file LoadBalancingStrategyUtils.h
 * @brief Free function to get load balancing strategy from application settings
 *
 * Extracted from FileIndex (Option C) so ParallelSearchEngine and other
 * search code can obtain the strategy without depending on FileIndex.
 *
 * @see docs/2026-01-31_FILEINDEX_REFACTORING_PLAN.md Option C
 */

/** Returns the load balancing strategy from AppSettings (LoadSettings). Default: Hybrid. */
LoadBalancingStrategyType GetLoadBalancingStrategyFromSettings();
