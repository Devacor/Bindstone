#pragma once

#ifndef __JAISCRIPT_DETAIL_STRING_MUTATOR_RECEIVER_HPP__
#define __JAISCRIPT_DETAIL_STRING_MUTATOR_RECEIVER_HPP__

// Receiver binding for the in-place string builtins (k_string_mutator_method_names),
// shared VERBATIM by both backends. C++ `string& to_upper()`: the receiver binds like a
// reference parameter and the write lands in that storage - a subscript read or ref
// alias (the popped receiver IS a reference), a field / C++ property (resolved through
// the ref-lvalue kernel, exactly how `f(obj.field)` binds a `string&` parameter), or the
// root of a call chain whose inner calls handed back the root's own string node
// (`t.trim().to_upper()`). Anything else is a temporary: the builtin mutates the popped
// copy, and the copy-on-write gate in unchecked_as_string_ref keeps that write out of
// every other holder. Identifier receivers never come here - each backend resolves them
// to the variable cell on its own fast path.

#include <jaiscript/detail/ref_lvalue.hpp>

namespace jai::detail {

	struct string_mutator_binding {
		script_value pin;                              // the reference pinning the storage (null = unbound)
		const identifier_expr* root_ident = nullptr;   // a chain root the backend resolves to its cell at call time
		bool chain = false;                            // pin came from a chain root: binds only on node identity

		explicit string_mutator_binding(engine* eng) : pin(std::monostate{}, eng) {}

		// The storage behind the pin: reference chains resolve to their final referent (a
		// PROPERTY scratch may itself hold a map-entry reference - `kv.second`)
		script_value* pinned_storage() {
			if (!pin.is_reference()) { return nullptr; }
			script_value* p = &pin.deref();
			while (p->is_reference()) { p = &p->deref(); }
			return p;
		}
		// Scratch-backed pins (typed element, PROPERTY) write the mutated scratch back
		void commit() {
			if (auto* sr = pin.scratch_ref_holder()) { sr->commit_scratch(); }
		}
	};

	// The receiver under a run of plain member calls: `t.trim().to_upper()` -> `t`. Null
	// when the receiver expression is not itself a member call.
	inline const expression* string_mutator_chain_root(const expression* receiver) noexcept {
		const expression* root = nullptr;
		while (receiver && receiver->get_type() == node_type::call_expr) {
			const auto* call = static_cast<const call_expr*>(receiver);
			if (!call->callee || call->callee->get_type() != node_type::member_expr) { return nullptr; }
			const auto* m = static_cast<const member_expr*>(call->callee.get());
			if (m->is_static || m->null_safe || m->is_arrow) { return nullptr; }
			root = m->object.get();
			receiver = root;
		}
		return root;
	}

	// A chain root binds only while its node IS the node the inner call handed back (an
	// inner call that minted a new string - substr, a user method - leaves a temporary)
	inline bool string_mutator_chain_matches(const script_value& root_storage, const script_value& receiver_copy) noexcept {
		const size_t kind = root_storage.raw_storage_index();
		if (kind != receiver_copy.raw_storage_index()) { return false; }
		if (kind == script_value::TYPEID_UTF8_STRING) { return &root_storage.unchecked_as_utf8() == &receiver_copy.unchecked_as_utf8(); }
		return kind == script_value::TYPEID_STRING &&
		       root_storage.unchecked_string_handle().get() == receiver_copy.unchecked_string_handle().get();
	}

	// Binds the receiver of a string mutator on a non-identifier receiver expression.
	// `receiver` is the evaluated receiver as the call site popped it (a reference for
	// subscript reads and ref aliases - moved into the pin - a copy for member reads and
	// call results). Pure resolution over already-evaluated state: never runs script,
	// never throws.
	inline string_mutator_binding bind_string_mutator_receiver(script_value& receiver, const expression* receiver_expr,
	                                                          const caller_frame_view& caller, environment* caller_env,
	                                                          engine* eng, string_symbolizer* symbolizer) {
		string_mutator_binding b(eng);
		if (receiver.is_reference()) {
			b.pin = std::move(receiver);
			return b;
		}
		if (is_member_final_ref_lvalue(receiver_expr)) {
			auto resolved = resolve_ref_lvalue(receiver_expr, caller, caller_env, eng, symbolizer);
			if (resolved && resolved.value().is_reference()) { b.pin = std::move(resolved.value()); }
			return b;   // a computed property / non-lvalue verdict: the copy is a temporary
		}
		const expression* root = string_mutator_chain_root(receiver_expr);
		if (!root) { return b; }
		if (root->get_type() == node_type::identifier_expr) {
			b.root_ident = static_cast<const identifier_expr*>(root);
		} else if (is_ref_bindable_lvalue(root)) {
			auto resolved = resolve_ref_lvalue(root, caller, caller_env, eng, symbolizer);
			if (resolved && resolved.value().is_reference()) {
				b.pin = std::move(resolved.value());
				b.chain = true;
			}
		}
		return b;
	}

	// The storage self binds to once the call site resolved a chain root cell (null when
	// the root is not an identifier or did not resolve): the pinned storage when its
	// referent is a string (a chain-root pin also on node identity), else null - the
	// caller then mutates its copy.
	inline script_value* string_mutator_self(string_mutator_binding& b, script_value* root_cell, const script_value& receiver_copy) {
		if (b.root_ident) {
			if (!root_cell) { return nullptr; }
			script_value* storage = &root_cell->deref();
			return string_mutator_chain_matches(*storage, receiver_copy) ? storage : nullptr;
		}
		script_value* storage = b.pinned_storage();
		if (!storage || storage->is_utf8_string() != receiver_copy.is_utf8_string() ||
		    !(storage->is_string() || storage->is_utf8_string())) { return nullptr; }
		if (b.chain && !string_mutator_chain_matches(*storage, receiver_copy)) { return nullptr; }
		return storage;
	}

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_STRING_MUTATOR_RECEIVER_HPP__
