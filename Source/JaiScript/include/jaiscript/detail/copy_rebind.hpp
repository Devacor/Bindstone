#pragma once

// What a copy does with the closures and coroutines its source holds (internal/coroutine_snapshot_design.md "Copy and
// restore agree"): a lambda, a bound method or a method coroutine the copy holds by value that is bound to an instance the
// source holds by value is rebuilt bound to that instance's copy. One kernel for both backends: the copy of an instance
// whose class can hold a callable ends here when it met one bound to an instance (class_instance::deep_copy_with_care), and
// no other copy does.

#include <jaiscript/core/value.hpp>

namespace jai { class class_instance; }

namespace jai::detail {

// The value is a function or a coroutine bound to a class instance: a lambda that captured this or holds a weak_ptr, a
// bound method, a method coroutine
bool binds_class_instance(const script_value& a_callable) noexcept;

// `a_copy` holds copies of `a_source`'s members (a deep copy, or the implicit copy assignment into an object that keeps its
// identity). Each closure and coroutine a_copy holds by value (a field, an element, an entry's value, at any depth of
// by-value nesting) that is bound to a_source or to an instance a_source holds by value takes that instance's copy for its
// receiver: a lambda gets its own captured values, a coroutine its own frame paused where the source's is. A coroutine
// whose state has no copy leaves a handle that is never done and raises the reason when it is resumed
void rebind_copied_callables(const class_instance& a_source, class_instance& a_copy);

} // namespace jai::detail
