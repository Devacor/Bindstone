#pragma once
#include <jaiscript/core/engine.hpp>

namespace jai::stdlib {
	// Kept as an idempotent host API entry point. Math is now engine-core under math::.
	inline void register_math_functions(engine&) {}
}
