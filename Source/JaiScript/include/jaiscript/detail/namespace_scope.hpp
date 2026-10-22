#pragma once

#ifndef __JAISCRIPT_DETAIL_NAMESPACE_SCOPE_HPP__
#define __JAISCRIPT_DETAIL_NAMESPACE_SCOPE_HPP__

// A namespace is a scope (C++): its script-declared variables are the cells of one environment per
// namespace, parented on the global environment, so a namespace function reads and writes them by
// their bare names and `ns::x = v` writes the same cell. Shared VERBATIM by the vm and the interpreter:
// the scope, the qualified path of a static member chain and the store target of `ns::x = v`.
// Host-registered entries (script_namespace_data::variables) stay values the host owns: readable
// through `ns::name`, never assigned from script.

#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/script_namespace.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <memory>
#include <optional>
#include <string>

namespace jai::detail {

	// A class a script namespace declares, by its qualified spelling (`ns::Base`, `a::b::C`): a qualified base class names it
	inline std::shared_ptr<class_definition> namespace_class(engine& eng, std::string_view qualified) {
		const size_t cut = qualified.rfind("::");
		if (cut == std::string_view::npos) { return nullptr; }
		auto& namespaces = eng.script_namespaces();
		auto ns = namespaces.find(eng.symbolize(qualified.substr(0, cut)));
		if (ns == namespaces.end() || !ns->second) { return nullptr; }
		auto cls = ns->second->classes.find(eng.symbolize(qualified.substr(cut + 2)));
		return cls != ns->second->classes.end() ? cls->second : nullptr;
	}

	// `ns::C` reaches the class's constructor and definition as its bare name does: an importer never saw the declaration
	inline void register_namespace_class(engine& eng, script_namespace_data& ns, std::string_view ns_name, uint64_t class_name_id,
	                                     const std::shared_ptr<class_definition>& def) {
		ns.classes[class_name_id] = def;
		std::string qualified = std::string(ns_name) + "::" + def->get_name();
		if (auto owner = eng.get_class_definition(qualified); owner && owner->get_name() == qualified) { return; }   // a host class registered under that spelling keeps it
		auto& symbols = *eng.get_symbolizer();
		const auto globals = eng.get_global_environment();
		const uint64_t qualified_id = symbols.intern(qualified);
		if (const script_value* ctor = globals->get_value_ptr(class_name_id)) { globals->define(qualified_id, *ctor); }
		if (const script_value* cls = globals->get_value_ptr(symbols.get_class_var_id_with_view(class_name_id).first)) {
			globals->define(symbols.get_class_var_id_with_view(qualified_id).first, *cls);
		}
		def->set_namespace_spelling(std::move(qualified), qualified_id);
	}

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
		bool missing = false;           // the path names a namespace that has no such member
	};

	// `path::member` as a namespace member for a store: the scope's cell, a host entry (cell null), a namespace without the
	// member (missing: the read's undefined-name text), or nothing (the path names no namespace: the class ladder decides).
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
		return namespace_store_target{nullptr, true};
	}

	// `ns::x = v` where ns declares no x: the text a read of ns::x raises
	JAI_NOINLINE inline error_propagator namespace_member_missing(const member_expr* member, string_symbolizer& symbols) {
		if (symbols.frozen()) {
			return error_propagator{make_error_code(runtime_error_code::undefined_variable), "Undefined variable", 0, 0};
		}
		return error_propagator{make_error_code(runtime_error_code::undefined_variable), "Undefined variable '{0}'",
			symbols.intern(static_path_of(member->object.get()) + "::" + std::string(member->member)), 0};
	}

} // namespace jai::detail

#endif
