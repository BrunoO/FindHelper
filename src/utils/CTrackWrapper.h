#pragma once

// ctrack redefines CRoaring's STRINGIFY without undefining it first.
// We guard the undefine to avoid SonarQube cpp:S1066 (undefining an undefined macro).
#ifdef STRINGIFY
#undef STRINGIFY  // NOSONAR(cpp:S959) - deliberate dep-conflict mediation: ctrack redefines roaring's macro without undefining; guarded no-op when absent
#endif
#include "ctrack.hpp"
