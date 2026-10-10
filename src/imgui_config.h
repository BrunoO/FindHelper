#pragma once

//-----------------------------------------------------------------------------
// DEAR IMGUI COMPILE-TIME OPTIONS
// Custom configuration file for USN_WINDOWS project
//-----------------------------------------------------------------------------
// This file extends the default imconfig.h with project-specific settings.
//-----------------------------------------------------------------------------

// Include the default imconfig.h first
#include "../external/imgui/imconfig.h"

#ifdef ENABLE_IMGUI_TEST_ENGINE
#define IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL 1  // NOLINT(cppcoreguidelines-macro-usage) NOSONAR(cpp:S5028) - ImGui test engine expects macro for te_imconfig.h
#include "../external/imgui_test_engine/imgui_test_engine/imgui_te_imconfig.h"
#endif  // ENABLE_IMGUI_TEST_ENGINE

//---- Enable FreeType for better font rendering
// Requires FreeType headers to be available in the include path. Requires program to be compiled with 'misc/freetype/imgui_freetype.cpp' (in this repository) + the FreeType library (not provided).
// Note that imgui_freetype.cpp may be used _without_ this define, if you manually call ImFontAtlas::SetFontLoader(). The define is simply a convenience.
#define IMGUI_ENABLE_FREETYPE  // NOSONAR(cpp:S5028) - ImGui expects a macro for feature detection (#ifdef), not constexpr
