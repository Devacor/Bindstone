#pragma once
#ifndef MV_AI_JAI_SERIALIZATION_HPP
#define MV_AI_JAI_SERIALIZATION_HPP

// JaiScript serialization support for MV AI types
// Include this header after both pathfinding.h and JaiScript serialization are available
//
// Map provides save() and load_and_construct() methods with concrete archive types
// directly in pathfinding.h. This header exists for any additional free-function
// serialization support or types that need the full archive definitions.

#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/properties/property_serialization.hpp>
#include "MV/ArtificialIntelligence/pathfinding.h"

namespace MV {

	// ============================================================
	// Map serialization
	// ============================================================
	// Map's save() and load_and_construct() use concrete archive_writer/archive_reader
	// types defined in pathfinding.h. This file provides the full archive headers
	// needed for compilation.

	// ============================================================
	// NavigationAgent serialization
	// ============================================================
	// NavigationAgent has a public default constructor, so it can use normal
	// serialization. Its cereal save/load methods handle the map reference
	// which will be restored via shared_ptr ID tracking.

} // namespace MV

#endif // MV_AI_JAI_SERIALIZATION_HPP
