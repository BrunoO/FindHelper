#pragma once

// WindowsTypesStub.h - Minimal Windows type definitions for cross-platform testing
// This file is only used when building tests on non-Windows platforms
// to allow test code to compile without Windows.h

#ifndef _WIN32

#include <cstdint>

// LONG type (Windows-specific, typically 32-bit signed integer)
using LONG = int32_t;

// FILETIME structure (matches Windows winbase.h definition exactly)
// dwLowDateTime: Low-order 32 bits of the file time
// dwHighDateTime: High-order 32 bits of the file time
struct FILETIME {
    uint32_t dwLowDateTime;
    uint32_t dwHighDateTime;
    
    // Allow initialization: FILETIME{0, 0}
    FILETIME() : dwLowDateTime(0), dwHighDateTime(0) {}
    FILETIME(uint32_t low, uint32_t high) : dwLowDateTime(low), dwHighDateTime(high) {}
    
    // Comparison operators for test assertions
    bool operator==(const FILETIME& other) const {  // NOSONAR(cpp:S2807) - Member operator is acceptable for test stub; could be refactored to hidden friend later if needed
        return dwLowDateTime == other.dwLowDateTime && 
               dwHighDateTime == other.dwHighDateTime;
    }
    bool operator!=(const FILETIME& other) const {  // NOSONAR(cpp:S2807) - Member operator is acceptable for test stub; could be refactored to hidden friend later if needed
        return !(*this == other);
    }
};

#endif  // _WIN32
