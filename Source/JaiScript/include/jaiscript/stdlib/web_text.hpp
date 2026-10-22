#pragma once

#include <jaiscript/core/engine.hpp>

namespace jai {
namespace stdlib {

	// html_escape, html_unescape, sha256 and sha256_file (source/implementation/stdlib/web_text.cpp)
	void register_web_text_functions(engine& eng_ref);

} // namespace stdlib
} // namespace jai
