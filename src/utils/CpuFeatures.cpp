#include <array>
#include <atomic>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "utils/CpuFeatures.h"
#include "utils/Logger.h"
#include "utils/StringSearch.h"  // For STRING_SEARCH_AVX2_AVAILABLE definition

#ifdef _WIN32
#include <intrin.h>  // For __cpuid on Windows
#include <windows.h>  // For GetLogicalProcessorInformation (lowercase for portability S3806)
#endif  // _WIN32

namespace cpu_features {

// Cached result (thread-safe initialization)
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Intentionally non-const for thread-safe lazy initialization
static std::atomic<bool> g_avx2_supported{false};  // NOSONAR(cpp:S5421) - These are intentionally non-const cache variables that must be mutable for thread-safe lazy initialization
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Intentionally non-const for thread-safe lazy initialization
static std::atomic<bool> g_avx2_checked{false};  // NOSONAR(cpp:S5421) - Atomic globals used for one-time initialization are acceptable

// Hyperthreading detection cache
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Intentionally non-const for thread-safe lazy initialization
static std::atomic<bool> g_ht_enabled{false};  // NOSONAR(cpp:S5421) - Atomic globals used for one-time initialization are acceptable
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Intentionally non-const for thread-safe lazy initialization
static std::atomic<bool> g_ht_checked{false};  // NOSONAR(cpp:S5421) - Atomic globals used for one-time initialization are acceptable
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Intentionally non-const for thread-safe lazy initialization
static std::atomic<unsigned int> g_physical_cores{0};  // NOSONAR(cpp:S5421) - Atomic globals used for one-time initialization are acceptable
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Intentionally non-const for thread-safe lazy initialization
static std::atomic<unsigned int> g_logical_cores{0};  // NOSONAR(cpp:S5421) - Atomic globals used for one-time initialization are acceptable
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Intentionally non-const for thread-safe lazy initialization
static std::atomic<bool> g_core_counts_checked{false};  // NOSONAR(cpp:S5421) - Atomic globals used for one-time initialization are acceptable

// CPUID feature detection constants (snake_case matches Intel/CPUID documentation)
// NOLINTNEXTLINE(readability-identifier-naming)
constexpr unsigned int cpuid_leaf_extended_features = 7;  // Extended features leaf
// NOLINTNEXTLINE(readability-identifier-naming)
constexpr unsigned int cpuid_leaf_basic_features = 1;     // Basic features leaf
// NOLINTNEXTLINE(readability-identifier-naming)
constexpr unsigned int cpuid_avx2_bit = 5;                // AVX2 is bit 5 of EBX (leaf 7)
// NOLINTNEXTLINE(readability-identifier-naming)
constexpr unsigned int cpuid_avx_bit = 28;                // AVX is bit 28 of ECX (leaf 1)
// NOLINTNEXTLINE(readability-identifier-naming)
constexpr unsigned int cpuid_osxsave_bit = 27;            // OSXSAVE is bit 27 of ECX (leaf 1); OS must enable XSAVE for AVX

#ifdef _WIN32
// Windows CPUID wrapper
// NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) - cpuid is standard x86 function name, info is output parameter array for CPUID instruction
static void cpuid(int info[4], [[maybe_unused]] int function_id) {
    __cpuid(info, function_id);
}

// Check for AVX2 support using CPUID
static bool CheckAVX2Support() {
    std::array<int, 4> info{};

    // Check if CPUID supports extended features (leaf 7)
    cpuid(info.data(), 0);
    if (const int max_leaf = info[0]; max_leaf < cpuid_leaf_extended_features) {
        return false;  // CPUID doesn't support leaf 7
    }
    
    // Get extended features (leaf 7, subleaf 0)
    // EBX register (info[1]) contains feature flags
    cpuid(info.data(), cpuid_leaf_extended_features);
    
    // AVX2 is bit 5 of EBX register
    // Check if bit 5 is set: (EBX >> 5) & 1
    bool has_avx2 = (info[1] & (1U << cpuid_avx2_bit)) != 0;

    // Also check for AVX support (required for AVX2)
    // AVX is bit 28 of ECX register (leaf 1)
    cpuid(info.data(), cpuid_leaf_basic_features);
    const bool has_avx = (info[2] & (1U << cpuid_avx_bit)) != 0;
    // OS must enable XSAVE for AVX state; otherwise AVX/AVX2 instructions fault
    const bool has_osxsave = (info[2] & (1U << cpuid_osxsave_bit)) != 0;

    return has_avx && has_avx2 && has_osxsave;
}

#else
// Linux/macOS CPUID using inline assembly
// NOLINTNEXTLINE(readability-identifier-naming,cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) - cpuid is standard x86 function name, info is output parameter array for CPUID instruction
static void cpuid(unsigned int info[4], [[maybe_unused]] unsigned int function_id) {  // NOSONAR(cpp:S995) - info is an output parameter (written to via assembly)
    #if defined(__x86_64__) || defined(__i386__)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Required for inline assembly output operands
        __asm__ __volatile__(
            "cpuid"
            : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3])
            : "a"(function_id), "c"(0)
        );
    #else
        // Not x86/x64, set all to 0
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Array indexing required for CPUID output parameter
        info[0] = info[1] = info[2] = info[3] = 0;
    #endif  // defined(__x86_64__) || defined(__i386__)
}

// NOLINTNEXTLINE(readability-identifier-naming,cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) - cpuid_count is standard x86 function name, info is output parameter array for CPUID instruction
// NOLINTNEXTLINE(readability-identifier-naming,cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) - cpuid_count is standard x86 function name, info is output parameter array for CPUID instruction
static void cpuid_count(unsigned int info[4], [[maybe_unused]] unsigned int function_id, [[maybe_unused]] unsigned int subleaf) {  // NOSONAR(cpp:S995) - info is an output parameter (written to via assembly)
    #if defined(__x86_64__) || defined(__i386__)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Required for inline assembly output operands
        __asm__ __volatile__(
            "cpuid"
            : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3])
            : "a"(function_id), "c"(subleaf)
        );
    #else
        // Not x86/x64, set all to 0
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - Array indexing required for CPUID output parameter
        info[0] = info[1] = info[2] = info[3] = 0;
    #endif  // defined(__x86_64__) || defined(__i386__)
}

// Check for AVX2 support using CPUID (used when !_WIN32, see DetectAVX2Support)
static bool CheckAVX2Support() {  // NOSONAR(cpp:S1144) - Used in DetectAVX2Support() in non-Windows build
    #if !defined(__x86_64__) && !defined(__i386__)  // NOSONAR(cpp:S1763) - Branch taken on ARM etc.; analyzer may not see it
        return false;
    #endif  // !defined(__x86_64__) && !defined(__i386__)

    std::array<unsigned int, 4> info{};  // NOSONAR(cpp:S1763) - Reached on x86; analyzer may treat as unreachable when non-x86 branch taken

    // Check if CPUID supports extended features (leaf 7)
    cpuid(info.data(), 0);
    if (const unsigned int max_leaf = info[0]; max_leaf < cpuid_leaf_extended_features) {  // NOLINT(cppcoreguidelines-init-variables) - init-statement initializes max_leaf
        return false;  // NOSONAR(cpp:S1763) - Reachable on old CPUs where CPUID max leaf < 7
    }

    // Get extended features (leaf 7, subleaf 0)
    // EBX register (info[1]) contains feature flags
    cpuid_count(info.data(), cpuid_leaf_extended_features, 0);

    // AVX2 is bit 5 of EBX register
    // Check if bit 5 is set: (EBX >> 5) & 1
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized from CPUID info[] above
    const bool has_avx2{(info[1] & (1U << cpuid_avx2_bit)) != 0};
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized from CPUID info[] above
    const unsigned int leaf7_ebx{info[1]};  // NOSONAR(cpp:S1481) - Used in LOG_INFO_BUILD below

    // Also check for AVX support (required for AVX2)
    // AVX is bit 28 of ECX register (leaf 1)
    cpuid(info.data(), cpuid_leaf_basic_features);
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized from CPUID info[] above
    const bool has_avx{(info[2] & (1U << cpuid_avx_bit)) != 0};
    // OS must enable XSAVE for AVX state; otherwise AVX/AVX2 instructions fault
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized from CPUID info[] above
    const bool has_osxsave{(info[2] & (1U << cpuid_osxsave_bit)) != 0};
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized from CPUID info[] above
    const unsigned int leaf1_ecx{info[2]};  // NOSONAR(cpp:S1481) - Used in LOG_INFO_BUILD below

    const bool supported = has_avx && has_avx2 && has_osxsave;
#if STRING_SEARCH_AVX2_AVAILABLE
    if (!supported) {
        std::ostringstream oss;
        oss << "AVX2-RT:off (CPUID leaf7 EBX=0x" << std::hex << leaf7_ebx
            << " leaf1 ECX=0x" << leaf1_ecx << " avx2=" << (has_avx2 ? 1 : 0)
            << " avx=" << (has_avx ? 1 : 0) << " osxsave=" << (has_osxsave ? 1 : 0)
            << "). Compare: grep -E 'avx|osxsave' /proc/cpuinfo";
        LOG_INFO_BUILD(oss.str());
    }
#endif  // STRING_SEARCH_AVX2_AVAILABLE
    return supported;
}

#endif  // _WIN32

bool SupportsAVX2() {
    // Double-checked locking pattern for thread safety
    if (g_avx2_checked.load(std::memory_order_acquire)) {
        return g_avx2_supported.load(std::memory_order_acquire);
    }
    
    // First call: detect and cache
    bool supported = false;
    
    // Only check on x86/x64 architectures
    #if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
        supported = CheckAVX2Support();
    #else
        // Not x86/x64, AVX2 not available
        supported = false;
    #endif  // defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)

    // Cache the result
    g_avx2_supported.store(supported, std::memory_order_release);
    g_avx2_checked.store(true, std::memory_order_release);
    
    return supported;
}

bool GetAVX2Support() {
    // Ensure detection has been performed
    if (!g_avx2_checked.load(std::memory_order_acquire)) {
        return SupportsAVX2();
    }
    
    return g_avx2_supported.load(std::memory_order_acquire);
}

#ifdef _WIN32
// Count set bits in a ULONG_PTR (for ProcessorMask)
// ProcessorMask is ULONG_PTR (pointer-sized, 64-bit on 64-bit Windows)
static unsigned int CountSetBits(ULONG_PTR mask) {
    unsigned int count = 0;
    while (mask) {
        count += (mask & 1);
        mask >>= 1;
    }
    return count;
}

// Process a single processor core entry to detect hyperthreading
// Returns true if this core indicates hyperthreading is enabled
static bool ProcessProcessorCore(
    const SYSTEM_LOGICAL_PROCESSOR_INFORMATION& info,
    unsigned int& physicalCores,
    unsigned int& logicalCores) {
    
    if (info.Relationship != RelationProcessorCore) {
        return false;
    }
    
    physicalCores++;
    // Count logical processors in this core (bits set in ProcessorMask)
    unsigned int logicalCount = CountSetBits(info.ProcessorMask);
    logicalCores += logicalCount;
    
    // If Flags is set (non-zero), logical processors share functional units (HT enabled)
    // Also check if there are multiple logical processors per core
    return (info.ProcessorCore.Flags != 0 || logicalCount > 1);
}

// Store error state for hyperthreading detection failure
// Sets all cache values to indicate detection failed and returns false
static bool StoreHyperThreadingErrorState() {
    g_ht_enabled.store(false, std::memory_order_release);
    g_physical_cores.store(0, std::memory_order_release);
    g_logical_cores.store(0, std::memory_order_release);
    g_ht_checked.store(true, std::memory_order_release);
    g_core_counts_checked.store(true, std::memory_order_release);
    return false;
}

// Check for Hyper-Threading using Windows API
static bool CheckHyperThreadingEnabled() {
    DWORD bufferSize = 0;
    GetLogicalProcessorInformation(nullptr, &bufferSize);
    
    if (bufferSize == 0) {
        // Failed to get buffer size, use fallback
        return StoreHyperThreadingErrorState();
    }
    
    // Use std::vector for RAII memory management instead of malloc/free
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(
        bufferSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
    
    bool hyperThreadingEnabled = false;
    unsigned int physicalCores = 0;
    unsigned int logicalCores = 0;
    
    if (GetLogicalProcessorInformation(buffer.data(), &bufferSize)) {
        DWORD numEntries = bufferSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        
        for (DWORD i = 0; i < numEntries; ++i) {
            if (ProcessProcessorCore(buffer[i], physicalCores, logicalCores)) {
                hyperThreadingEnabled = true;
            }
        }
        
        // Additional check: if logical cores > physical cores, HT is likely enabled
        if (logicalCores > physicalCores) {
            hyperThreadingEnabled = true;
        }
    } else {
        // API call failed, use fallback
        return StoreHyperThreadingErrorState();
    }
    
    // Cache results
    g_ht_enabled.store(hyperThreadingEnabled, std::memory_order_release);
    g_physical_cores.store(physicalCores, std::memory_order_release);
    g_logical_cores.store(logicalCores, std::memory_order_release);
    g_ht_checked.store(true, std::memory_order_release);
    g_core_counts_checked.store(true, std::memory_order_release);
    
    return hyperThreadingEnabled;
}
#else
// Non-Windows: Cannot reliably detect hyperthreading
static bool CheckHyperThreadingEnabled() {
    // On non-Windows, assume false (conservative)
    g_ht_enabled.store(false, std::memory_order_release);
    g_ht_checked.store(true, std::memory_order_release);
    return false;
}
#endif  // _WIN32

bool IsHyperThreadingEnabled() {
    // Double-checked locking pattern for thread safety
    if (g_ht_checked.load(std::memory_order_acquire)) {
        return g_ht_enabled.load(std::memory_order_acquire);
    }
    
    // First call: detect and cache
    const bool enabled = CheckHyperThreadingEnabled();
    
    return enabled;
}

std::pair<unsigned int, unsigned int> GetCoreCounts() {
    // Ensure detection has been performed
    if (!g_core_counts_checked.load(std::memory_order_acquire)) {
        IsHyperThreadingEnabled();  // This will populate core counts
    }
    
    return std::make_pair(
        g_physical_cores.load(std::memory_order_acquire),
        g_logical_cores.load(std::memory_order_acquire)
    );
}

const char *GetBuildFeatureString() {
    static std::string cached_features;
    if (!cached_features.empty()) {
        return cached_features.c_str();
    }

    cached_features = "";

    // Compile-time AVX2 availability: indicates if AVX2 code was compiled (x86/x64 only)
    // CMakeLists.txt ensures AVX2 compiler flags are set for StringSearchAVX2.cpp on x86/x64
#if STRING_SEARCH_AVX2_AVAILABLE
    cached_features += "AVX2-CT:on";
#else
    cached_features += "AVX2-CT:off";
#endif  // STRING_SEARCH_AVX2_AVAILABLE

    // Runtime AVX2 detection: checks if CPU actually supports AVX2
    // May be false even if AVX2-CT:on (e.g., running on older CPU)
    const bool avx2_runtime = SupportsAVX2();
    cached_features += avx2_runtime ? " AVX2-RT:on" : " AVX2-RT:off";

    return cached_features.c_str();
}

} // namespace cpu_features
