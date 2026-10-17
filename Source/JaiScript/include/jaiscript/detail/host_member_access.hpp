#pragma once
#ifndef __JAISCRIPT_DETAIL_HOST_MEMBER_ACCESS_HPP__
#define __JAISCRIPT_DETAIL_HOST_MEMBER_ACCESS_HPP__

#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/detail/host_member.hpp>

namespace jai::detail {

// The C++ object a bound class's instance holds: its holder (the registered type it is stored under) and address
inline const script_value::object_holder* host_member_stored(const class_instance* inst, const void*& stored) {
	const script_value* native = inst->native();
	if (!native || native->raw_storage_index() != script_value::TYPEID_OBJECT) { return nullptr; }
	const auto& holder = native->unchecked_object_handle();
	if (!holder) { return nullptr; }
	stored = holder->data ? holder->data.get() : holder->bound_ptr;
	return stored ? holder.get() : nullptr;
}

// A member's lane for this receiver when it may replace the bound accessors: a pure bound C++ class (no script layer,
// no access-controlled chain) whose member has a lane the receiver's object reaches. The hit fixes the layout it was
// measured under: the stored type and, for a polymorphic stored type, the object's vptr. KEEP BYTE-PARALLEL: both
// backends fill through here and read through host_member_address.
inline bool host_member_fill(class_definition* cd, const class_instance* inst, uint64_t member_id,
                             const script_value& receiver, engine* eng, host_member_hit& out) {
	out = host_member_hit{};
	if (!cd->is_cpp_class() || cd->extended_ever() || cd->chain_has_nonpublic()) { return false; }
	const host_member_lane* lane = cd->host_member_lane_for(member_id);
	if (!lane || !lane->locate) { return false; }
	const void* stored = nullptr;
	const script_value::object_holder* holder = host_member_stored(inst, stored);
	if (!holder || holder->type_id == UINT64_MAX) { return false; }
	auto stored_def = eng->get_class_definition(holder->type_id);
	if (!stored_def || !stored_def->is_cpp_class()) { return false; }
	void* address = lane->locate(*lane, receiver);
	if (!address) { return false; }
	out.lane = lane;
	out.delta = static_cast<const char*>(address) - static_cast<const char*>(stored);
	out.stored_type = holder->type_id;
	out.vptr = stored_def->cpp_polymorphic() ? *static_cast<const void* const*>(stored) : nullptr;
	return true;
}

// The member's address when the receiver's object still has the layout the hit was measured under, else null
inline void* host_member_address(const class_instance* inst, const host_member_hit& hit) {
	const void* stored = nullptr;
	const script_value::object_holder* holder = host_member_stored(inst, stored);
	if (!holder || holder->type_id != hit.stored_type) { return nullptr; }
	if (hit.vptr && *static_cast<const void* const*>(stored) != hit.vptr) { return nullptr; }
	return const_cast<char*>(static_cast<const char*>(stored)) + hit.delta;
}

} // namespace jai::detail

#endif
