#pragma once

#include <jaiscript/core/value.hpp>
#include <memory>
#include <string>

namespace jai {
class coroutine_handle;
}

namespace jai::detail {

// The coroutine a value holds, or null
std::shared_ptr<coroutine_handle> coroutine_of(const script_value& a_value);

// The name a coroutine's errors spell: its function, a method's behind its class
std::string coroutine_display_name(const coroutine_handle& a_handle);

[[noreturn]] void throw_coroutine_not_saved(const coroutine_handle& a_handle);

} // namespace jai::detail
