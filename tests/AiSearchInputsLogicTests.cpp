#include <string>

#include "api/GeminiApiUtils.h"
#include "doctest/doctest.h"
#include "ui/AiSearchInputsLogic.h"

using ui::ai_search_inputs::AiResultAction;
using ui::ai_search_inputs::HelpMeSearchAction;
using ui::ai_search_inputs::ResolveAiResultAction;
using ui::ai_search_inputs::ResolveHelpMeSearchAction;

TEST_CASE("Help Me Search button action") {
  SUBCASE("A description with an idle pipeline starts the AI call") {
    CHECK(ResolveHelpMeSearchAction(false, false, true) == HelpMeSearchAction::StartAiCall);
  }
  SUBCASE("No description falls through to a manual search") {
    CHECK(ResolveHelpMeSearchAction(false, false, false) == HelpMeSearchAction::ManualSearch);
  }
  SUBCASE("An in-flight call is never restarted, with or without a description") {
    // Re-pressing while a request is running must not dispatch a second call.
    CHECK(ResolveHelpMeSearchAction(false, true, true) == HelpMeSearchAction::IgnoreInFlight);
    CHECK(ResolveHelpMeSearchAction(false, true, false) == HelpMeSearchAction::IgnoreInFlight);
  }
  SUBCASE("An index build blocks the button") {
    CHECK(ResolveHelpMeSearchAction(true, false, true) == HelpMeSearchAction::BlockedByIndexBuild);
    CHECK(ResolveHelpMeSearchAction(true, false, false) == HelpMeSearchAction::BlockedByIndexBuild);
  }
  SUBCASE("An in-flight call outranks an index build") {
    // The call is already paid for; the response is still applied later, so the
    // press must not be treated as "index building" and dropped silently.
    CHECK(ResolveHelpMeSearchAction(true, true, true) == HelpMeSearchAction::IgnoreInFlight);
  }
}

TEST_CASE("AI result action") {
  SUBCASE("A successful response applies the config and searches") {
    gemini_api_utils::GeminiApiResult result;
    result.success = true;
    CHECK(ResolveAiResultAction(result) == AiResultAction::ApplyConfigAndSearch);
  }
  SUBCASE("A failed response is reported, not applied") {
    gemini_api_utils::GeminiApiResult result;
    result.success = false;
    result.error_message = "FreeTierError";
    CHECK(ResolveAiResultAction(result) == AiResultAction::ReportError);
  }
  SUBCASE("Success is decided by the flag alone, not by a populated config") {
    // A provider can legitimately answer with an empty search_config; the
    // decision must not silently turn that into an error report.
    gemini_api_utils::GeminiApiResult result;
    result.success = true;
    CHECK(result.search_config.filename.empty());
    CHECK(ResolveAiResultAction(result) == AiResultAction::ApplyConfigAndSearch);
  }
}
