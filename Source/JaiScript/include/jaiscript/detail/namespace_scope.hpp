#pragma once

#ifndef __JAISCRIPT_DETAIL_NAMESPACE_SCOPE_HPP__
#define __JAISCRIPT_DETAIL_NAMESPACE_SCOPE_HPP__

// A namespace is a scope (C++): its script-declared variables are the cells of one environment per
// namespace, parented on the global environment, so a namespace function reads and writes them by
// their bare names and `ns::x = v` writes the same cell. Shared VERBATIM by the vm and the interpreter:
// the scope, the qualified path of a static member chain and the store target of `ns::x = v`.
// Host-registered entries (script_namespace_data::variables) stay values the host owns: readable
// through `ns::name`, never assigned from script.

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/script_namespace.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <memory>
#include <optional>
#include <string>

namespace jai::detail {

	inline const std::shared_ptr<environment>& namespace_scope(engine& eng, script_namespace_data& ns) {
		if (!ns.scope) {
			ns.scope = std::make_shared<environment>(eng.get_global_environment(), eng.get_symbolizer());
			ns.scope->mark_namespace_scope();
		}
		return ns.scope;
	}

	// "a::b" for the object of `a::b::x`; empty when the chain is not identifiers joined by ::
	inline std::string static_path_of(const expression* object) {
		if (object->get_type() == node_type::identifier_expr) {
			return std::string(static_cast<const identifier_expr*>(object)->name);
		}
		if (object->get_type() == node_type::member_expr) {
			const auto* m = static_cast<const member_expr*>(object);
			if (m->is_static) {
				std::string head = static_path_of(m->object.get());
				return head.empty() ? head : head + "::" + std::string(m->member);
			}
		}
		return {};
	}

	struct namespace_store_target {
		script_value* cell = nullptr;   // the scope's own cell; null names a host-registered entry
	};

	// `path::member` as a namespace member for a store: the scope's cell, a host entry (cell null), or
	// nothing (the path names no namespace, or the namespace has no such member: the class ladder decides).
	// Reads only: no name is interned on this path.
	inline std::optional<namespace_store_target> resolve_namespace_store(engine& eng, const member_expr* member, const string_symbolizer& symbols) {
		const std::string path = static_path_of(member->object.get());
		if (path.empty()) {
			return std::nullopt;
		}
		const auto path_id = symbols.find(path);
		if (!path_id) {
			return std::nullopt;
		}
		auto& spaces = eng.script_namespaces();
		auto ns_it = spaces.find(*path_id);
		if (ns_it == spaces.end() || !ns_it->second) {
			return std::nullopt;
		}
		script_namespace_data& ns = *ns_it->second;
		if (ns.scope) {
			if (script_value* cell = ns.scope->get_own_value_ptr(member->member_id)) {
				return namespace_store_target{cell};
			}
		}
		if (ns.variables.count(member->member_id) != 0) {
			return namespace_store_target{};
		}
		return std::nullopt;
	}

} // namespace jai::detail

#endif
