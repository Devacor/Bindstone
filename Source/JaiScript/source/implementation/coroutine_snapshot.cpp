#include "../../include/jaiscript/detail/coroutine_snapshot.hpp"
#include "../../include/jaiscript/core/coroutine.hpp"
#include "../../include/jaiscript/core/engine.hpp"
#include "../../include/jaiscript/detail/ast.hpp"
#include "../../include/jaiscript/detail/class_runtime.hpp"
#include "../../include/jaiscript/serialization/archive_impl.hpp"

namespace jai::detail {

std::shared_ptr<coroutine_handle> coroutine_of(const script_value& a_value) {
	const script_value& value = a_value.is_reference() ? a_value.deref() : a_value;
	if (!value.is_object()) { return nullptr; }
	const auto holder = value.get_object_holder();
	if (!holder || !holder->is_coroutine_handle()) { return nullptr; }
	return std::static_pointer_cast<coroutine_handle>(holder->host_object());
}

std::string coroutine_display_name(const coroutine_handle& a_handle) {
	std::string result;
	if (const class_instance* receiver = a_handle.receiver().peek_class_instance()) {
		if (const class_definition* owner = receiver->get_class_definition()) { result = owner->get_name() + "::"; }
	}
	if (const auto function = a_handle.get_function()) { result += function->name; }
	return result;
}

JAI_NOINLINE void throw_coroutine_not_saved(const coroutine_handle& a_handle) {
	throw serialization_error("Cannot save coroutine '" + coroutine_display_name(a_handle) + "': a paused coroutine is not data");
}

} // namespace jai::detail
