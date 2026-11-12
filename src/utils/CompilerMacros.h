#pragma once

/**
 * @file CompilerMacros.h
 * @brief Compiler-specific attribute macros for cross-platform modern C++ safety.
 *
 * Provides annotations for lifetime safety and static analysis:
 * - LIFETIMEBOUND: Informs the compiler that a returned reference or view
 *   (e.g., std::string_view, span, or reference) borrows from a parameter or *this,
 *   enabling compile-time detection of dangling references and use-after-free bugs.
 *   Supported on Clang ([[clang::lifetimebound]]) and MSVC ([[msvc::lifetimebound]]).
 */

#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(clang::lifetimebound)
#    define LIFETIMEBOUND [[clang::lifetimebound]]  // NOLINT(cppcoreguidelines-macro-usage) - retained suppression NOSONAR(cpp:S5028)
#  elif __has_cpp_attribute(msvc::lifetimebound)
#    define LIFETIMEBOUND [[msvc::lifetimebound]]   // NOLINT(cppcoreguidelines-macro-usage) - retained suppression NOSONAR(cpp:S5028)
#  else
#    define LIFETIMEBOUND                           // NOLINT(cppcoreguidelines-macro-usage) - retained suppression NOSONAR(cpp:S5028)
#  endif
#elif defined(__clang__)
#  define LIFETIMEBOUND [[clang::lifetimebound]]    // NOLINT(cppcoreguidelines-macro-usage) - retained suppression NOSONAR(cpp:S5028)
#else
#  define LIFETIMEBOUND                             // NOLINT(cppcoreguidelines-macro-usage) - retained suppression NOSONAR(cpp:S5028)
#endif

// Clang Thread Safety Analysis attributes
#if defined(__clang__) && (!defined(SWIG))
#  define FH_THREAD_ANNOTATION_ATTRIBUTE(x) __attribute__((x))  // NOLINT(cppcoreguidelines-macro-usage) - retained suppression NOSONAR(cpp:S5028)
#else
#  define FH_THREAD_ANNOTATION_ATTRIBUTE(x)                     // NOLINT(cppcoreguidelines-macro-usage) - retained suppression NOSONAR(cpp:S5028)
#endif

#define GUARDED_BY(x) FH_THREAD_ANNOTATION_ATTRIBUTE(guarded_by(x))  // NOLINT(cppcoreguidelines-macro-usage) - retained suppression NOSONAR(cpp:S5028)
#define PT_GUARDED_BY(x) FH_THREAD_ANNOTATION_ATTRIBUTE(pt_guarded_by(x))  // NOLINT(cppcoreguidelines-macro-usage) - retained suppression NOSONAR(cpp:S5028)
#define REQUIRES(...) FH_THREAD_ANNOTATION_ATTRIBUTE(requires_capability(__VA_ARGS__))  // NOLINT(cppcoreguidelines-macro-usage) - retained suppression NOSONAR(cpp:S5028)
#define ACQUIRES(...) FH_THREAD_ANNOTATION_ATTRIBUTE(acquire_capability(__VA_ARGS__))  // NOLINT(cppcoreguidelines-macro-usage) - retained suppression NOSONAR(cpp:S5028)
#define RELEASES(...) FH_THREAD_ANNOTATION_ATTRIBUTE(release_capability(__VA_ARGS__))  // NOLINT(cppcoreguidelines-macro-usage) - retained suppression NOSONAR(cpp:S5028)
