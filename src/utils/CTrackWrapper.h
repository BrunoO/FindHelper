#pragma once

// ctrack redefines CRoaring's STRINGIFY without undefining it first.
// We guard the undefine to avoid SonarQube cpp:S1066 (undefining an undefined macro).
#ifdef STRINGIFY
#undef STRINGIFY  // NOSONAR(cpp:S959) - deliberate dep-conflict mediation: ctrack redefines roaring's macro without undefining; guarded no-op when absent
#endif

// When profiling is disabled (CTRACK_DISABLE defined), ctrack is not fetched and its
// header is unavailable. Provide empty no-op macros so call sites compile unchanged.
#ifndef CTRACK_DISABLE
#include "ctrack.hpp"  // IWYU pragma: export
#else
#define CTRACK_PROD
#define CTRACK_PROD_NAME(name)
#define CTRACK_DEV
#define CTRACK_DEV_NAME(name)
#define CTRACK
#define CTRACK_NAME(name)
#endif