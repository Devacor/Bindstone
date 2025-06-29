#pragma once

// Main JaiScript include file
// Include this to use JaiScript in your project

#include "jaiscript_fwd.hpp"
#include "core/types.hpp"
#include "core/value.hpp"
#include "core/engine.hpp"

// Version information
#define JAISCRIPT_VERSION_MAJOR 0
#define JAISCRIPT_VERSION_MINOR 1
#define JAISCRIPT_VERSION_PATCH 0

namespace jai {
    
    // Version string
    inline const std::string& version() {
        static const std::string version_string = "0.1.0";
        return version_string;
    }
    
    // Convenience function to create an engine
    inline std::unique_ptr<engine> createEngine() {
        return std::make_unique<engine>();
    }
    
} // namespace jai