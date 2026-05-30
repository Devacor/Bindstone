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

// === Cereal (serialization - used by most MV types) ===
#include <cereal/cereal.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/archives/portable_binary.hpp>
