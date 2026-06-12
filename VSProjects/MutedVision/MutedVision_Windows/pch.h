#pragma once

// MutedVision precompiled header
// Includes expensive headers that rarely change, parsed once per build.

// === Standard library (most expensive on MSVC) ===
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <functional>
#include <algorithm>
#include <optional>
#include <variant>
#include <set>
#include <type_traits>
#include <cstdint>
#include <iostream>
#include <sstream>

// === JaiScript (signals, properties, serialization concepts) ===
#include <jaiscript/signals/signal.hpp>
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/core/class_definition.hpp>
