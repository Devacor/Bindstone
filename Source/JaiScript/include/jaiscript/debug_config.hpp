#pragma once

#ifndef __JAISCRIPT_DEBUG_CONFIG_HPP__
#define __JAISCRIPT_DEBUG_CONFIG_HPP__

// ============================================================================
// JaiScript Debug Configuration
// ============================================================================
// This file contains compile-time debug flags that can be toggled to enable
// detailed debugging and tracking features. These should normally be OFF in
// production builds.
//
// To enable a feature, change the value from 0 to 1 (or false to true).
// ============================================================================

namespace jai {
namespace debug {

    // Track all shared_ptr copies for script objects to debug lifetime issues
    // When enabled, logs file/line/function for every copy/move of object shared_ptrs
    // WARNING: This has significant performance overhead - only use during debugging!
    constexpr bool TRACK_OBJECT_REFERENCES = true;  // ENABLED FOR DEBUGGING

    // Log destructor calls with detailed information
    constexpr bool LOG_DESTRUCTOR_CALLS = false;

    // Log environment scope push/pop operations
    constexpr bool LOG_SCOPE_OPERATIONS = false;

} // namespace debug
} // namespace jai

#endif // __JAISCRIPT_DEBUG_CONFIG_HPP__
