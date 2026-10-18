#pragma once

/**
 * @file PatternDiagnostics.h
 * @brief Surfaces PathPattern compile failures to the user.
 *
 * A PathPattern that fails to compile matches nothing, which the search reports
 * as an ordinary empty result. The user then has no way to tell "your pattern is
 * broken" from "nothing matches that", so a rejected pattern is surfaced as an
 * input error in the empty-result view instead.
 *
 * The check compiles each PathPattern-typed query, so it is done once per input
 * change rather than per frame; the inputs it was computed from are cached in
 * SearchPipelineState.
 */

#include <string_view>

#include "gui/SearchPipelineState.h"

/**
 * @brief Recompute the PathPattern diagnostic for the current queries.
 *
 * Writes pipeline.pattern_error (empty when both queries are fine). Cheap when
 * the inputs are unchanged - a string compare - so it is safe to call every
 * frame from the UI thread.
 *
 * @param filename_query Item-name query (may carry a pp: prefix)
 * @param path_query Path query (may carry a pp: prefix)
 * @param case_sensitive Current case-sensitivity setting
 * @param pipeline Pipeline state to update (sole writer of its pattern_error)
 */
void UpdatePatternDiagnostics(std::string_view filename_query,
                              std::string_view path_query,
                              bool case_sensitive,
                              SearchPipelineState& pipeline);