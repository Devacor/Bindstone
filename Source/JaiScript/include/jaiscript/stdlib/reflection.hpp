#pragma once

#ifndef JAISCRIPT_STDLIB_REFLECTION_HPP
#define JAISCRIPT_STDLIB_REFLECTION_HPP

// The reflect:: namespace (docs/reflection.md — the front door; docs/reflection_design.md —
// the machinery). Every builtin is a LIVE read of the engine's registered classes: hot
// reload a class and the next call sees the new shape (recapture = re-ask). Results are
// ordinary value-semantic containers minted at call time. The namespace itself is RESERVED
// (is_reserved_namespace_name): these answers are engine truth and cannot be impersonated
// from script. Registered once per engine; both backends invoke the same std::function, so
// output and error text are byte-identical by construction.

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/script_namespace.hpp>
#include <jaiscript/properties/property_schema.hpp>
#include <string>
#include <typeindex>
#include <vector>

namespace jai {
namespace stdlib {
namespace reflect_detail {

// The semantic type name: objects answer their class (ladder) name, everything else the
// same names the type system uses. References deref; cpp-bound primitives answer their
// semantic kind, never the storage wrapper.
inline std::string semantic_type_name(const script_value& val, engine* eng) {
    const script_value& d = val.is_reference() ? val.deref() : val;
    if (d.is_object()) {
        try {
            auto instance = d.as<std::shared_ptr<class_instance>>();
            if (instance) {
                if (auto def = instance->get_class_definition()) {
                    return def->get_name();
                }
            }
        } catch (...) {}
        if (auto ti = d.get_type_info()) {
            if (!ti->type_name.empty() && ti->type_name != "any") { return ti->type_name; }
        }
        return "object";
    }
    // Typed containers carry their canonical tag ("array<int>"); untyped report the kind
    if (d.is_array() || d.is_map()) {
        if (auto ti = d.get_type_info()) {
            if (!ti->type_name.empty() && ti->type_name != "any") { return ti->type_name; }
        }
        return d.is_array() ? "array" : "map";
    }
    if (d.is_null()) { return "null"; }
    if (d.is_bool()) { return "bool"; }
    if (d.is_int()) { return "int"; }
    if (d.is_float()) { return "float"; }
    if (d.is_char()) { return "char"; }
    if (d.is_string()) { return "string"; }
    if (d.is_function()) { return "function"; }
    if (d.is_weak_ptr()) { return "weak_ptr"; }
    (void)eng;
    return "unknown";
}

// object | "Class" -> class_definition. Unknown class NAME throws the family error;
// a non-class VALUE returns nullptr (callers decide whether that is an error).
inline class_definition* resolve_class(engine* eng, const script_value& v,
                                       const char* who) {
    // Raw pointers throughout: the engine's registries own every definition returned
    // here, so lifetime spans any single builtin call.
    const script_value& d = v.is_reference() ? v.deref() : v;
    if (d.is_string()) {
        const std::string name = d.as_string();
        auto def = eng->get_class_definition(name);
        if (!def) {
            throw runtime_error(std::string("reflect::") + who + ": unknown class '" + name + "'");
        }
        return def.get();
    }
    if (d.is_object()) {
        try {
            auto instance = d.as<std::shared_ptr<class_instance>>();
            if (instance) {
                if (auto* def = instance->get_class_definition()) { return def; }
            }
        } catch (...) {}
        // Host-bound object without a class_instance wrapper: resolve by semantic name
        if (auto ti = d.get_type_info()) {
            if (!ti->type_name.empty()) {
                if (auto def = eng->get_class_definition(ti->type_name)) { return def.get(); }
            }
        }
    }
    return nullptr;
}

inline void expect_args(const std::vector<script_value>& args, size_t n, const char* who) {
    if (args.size() != n) {
        throw runtime_error(std::string("reflect::") + who + " expects " +
                            std::to_string(n) + (n == 1 ? " argument" : " arguments"));
    }
}

// A C++ value type's script-facing name for field entries (host property types)
inline std::string cpp_type_display_name(engine* eng, std::type_index ti) {
    if (ti == std::type_index(typeid(int)) || ti == std::type_index(typeid(int64_t)) ||
        ti == std::type_index(typeid(script_int))) { return "int"; }
    if (ti == std::type_index(typeid(float)) || ti == std::type_index(typeid(double)) ||
        ti == std::type_index(typeid(script_float))) { return "float"; }
    if (ti == std::type_index(typeid(bool))) { return "bool"; }
    if (ti == std::type_index(typeid(std::string))) { return "string"; }
    if (auto def = eng->get_class_definition_by_type(ti)) { return def->get_name(); }
    return "var";
}

inline script_value make_field_entry(engine* eng, const std::string& name, const std::string& type,
                                     const std::string& kind, const std::string& access,
                                     bool is_static, const std::string& from) {
    auto entry = script_value::make_map(eng->get_type_info_string(), nullptr, eng);
    auto& m = const_cast<script_map&>(entry.as_map());
    m[script_value(std::string("name"), eng)] = script_value(name, eng);
    m[script_value(std::string("type"), eng)] = script_value(type, eng);
    m[script_value(std::string("kind"), eng)] = script_value(kind, eng);
    m[script_value(std::string("access"), eng)] = script_value(access, eng);
    m[script_value(std::string("static"), eng)] = script_value(is_static, eng);
    m[script_value(std::string("from"), eng)] = script_value(from, eng);
    return entry;
}

// Base-first, declaration-order field walk (the determinism contract): each level
// contributes its OWN fields from the retained field_order_, parents before children.
inline void append_fields_of(engine* eng, class_definition* def, script_value& arr) {
    for (const auto& parent : def->get_parent_classes()) {
        if (parent) { append_fields_of(eng, parent.get(), arr); }
    }
    const std::string from = def->get_name();
    for (uint64_t id : def->field_order()) {
        const std::string name{eng->get_symbolizer()->get_string(id)};
        std::string type = "auto";
        if (auto ti = def->get_field_declared_type(id)) {
            type = (ti->type_name == "any") ? "var"
                 : (ti->type_name.empty() ? "auto" : ti->type_name);
        }
        std::string access = "public";
        access_level level = access_level::public_access;
        if (def->find_nonpublic_declarer(id, level)) {
            access = (level == access_level::private_access) ? "private" : "protected";
        }
        arr.as_array().push_back(make_field_entry(eng, name, type, "field", access,
                                                  def->is_static_field(id), from));
    }
    // Host-backed classes: the type_registry schema is the declaration record —
    // own_properties() is declaration order, kind distinguishes signals/observables
    if (def->cpp_backed() && def->cpp_type()) {
        if (const auto* schema = type_registry::instance().try_get(std::type_index(*def->cpp_type()))) {
            for (const auto& prop : schema->own_properties()) {
                const char* kind = prop.is_signal ? "signal"
                                 : prop.is_observable ? "observable" : "field";
                arr.as_array().push_back(make_field_entry(
                    eng, prop.name, cpp_type_display_name(eng, prop.value_type_id),
                    kind, "public", false, from));
            }
        }
    }
}

} // namespace reflect_detail

inline void register_reflection_functions(engine& eng_ref) {
    engine* eng = &eng_ref;
    auto& ns = eng_ref.script_namespaces()[eng_ref.get_symbolizer()->intern("reflect")];
    if (!ns) { ns = std::make_shared<script_namespace_data>(); }

    auto add = [&](const char* name, script_function fn) {
        ns->variables[eng_ref.get_symbolizer()->intern(name)] =
            script_value::make_function(std::move(fn), eng);
    };

    add("type_name", [eng](const std::vector<script_value>& args) -> checked_result<script_value> {
        reflect_detail::expect_args(args, 1, "type_name");
        return script_value(reflect_detail::semantic_type_name(args[0], eng), eng);
    });

    add("is_cpp_bound", [eng](const std::vector<script_value>& args) -> checked_result<script_value> {
        reflect_detail::expect_args(args, 1, "is_cpp_bound");
        const script_value& d = args[0].is_reference() ? args[0].deref() : args[0];
        // Class form: host-registered vs script-declared
        if (d.is_string()) {
            auto def = reflect_detail::resolve_class(eng, d, "is_cpp_bound");
            return script_value(def->cpp_backed(), eng);
        }
        // Value form: is this value C++-backed (bound primitive or bound object payload)?
        if (d.is_cpp_bound_primitive()) { return script_value(true, eng); }
        if (d.is_object()) {
            if (d.get_cpp_bound_ptr() != nullptr) { return script_value(true, eng); }
            if (auto def = reflect_detail::resolve_class(eng, d, "is_cpp_bound")) {
                return script_value(def->cpp_backed(), eng);
            }
        }
        return script_value(false, eng);
    });

    add("has_field", [eng](const std::vector<script_value>& args) -> checked_result<script_value> {
        reflect_detail::expect_args(args, 2, "has_field");
        if (!args[1].is_string()) { throw runtime_error("reflect::has_field expects a field name string"); }
        auto def = reflect_detail::resolve_class(eng, args[0], "has_field");
        if (!def) { return script_value(false, eng); }
        const std::string field = args[1].as_string();
        const uint64_t id = eng->symbolize(field);
        if (def->get_all_field_defaults().count(id)) { return script_value(true, eng); }
        for (auto sv : def->get_static_field_names()) {
            if (sv == field) { return script_value(true, eng); }
        }
        for (const auto& prop : def->get_property_names(true)) {
            if (prop == field) { return script_value(true, eng); }
        }
        return script_value(false, eng);
    });

    add("has_method", [eng](const std::vector<script_value>& args) -> checked_result<script_value> {
        reflect_detail::expect_args(args, 2, "has_method");
        if (!args[1].is_string()) { throw runtime_error("reflect::has_method expects a method name string"); }
        auto def = reflect_detail::resolve_class(eng, args[0], "has_method");
        if (!def) { return script_value(false, eng); }
        const uint64_t id = eng->symbolize(args[1].as_string());
        return script_value(def->defines_method(id), eng);
    });

    add("classes", [eng](const std::vector<script_value>& args) -> checked_result<script_value> {
        reflect_detail::expect_args(args, 0, "classes");
        auto arr = script_value::make_array(nullptr, eng);
        for (const auto& name : eng->registered_class_names()) {
            arr.as_array().push_back(script_value(name, eng));
        }
        return arr;
    });

    add("bases", [eng](const std::vector<script_value>& args) -> checked_result<script_value> {
        reflect_detail::expect_args(args, 1, "bases");
        auto def = reflect_detail::resolve_class(eng, args[0], "bases");
        auto arr = script_value::make_array(nullptr, eng);
        if (def) {
            for (const auto& parent : def->get_parent_classes()) {
                if (parent) { arr.as_array().push_back(script_value(parent->get_name(), eng)); }
            }
        }
        return arr;
    });

    add("generation", [eng](const std::vector<script_value>& args) -> checked_result<script_value> {
        reflect_detail::expect_args(args, 1, "generation");
        auto def = reflect_detail::resolve_class(eng, args[0], "generation");
        if (!def) { throw runtime_error("reflect::generation expects a class or class name"); }
        return script_value(static_cast<script_int>(def->reflection_generation()), eng);
    });

    add("fields", [eng](const std::vector<script_value>& args) -> checked_result<script_value> {
        reflect_detail::expect_args(args, 1, "fields");
        auto arr = script_value::make_array(nullptr, eng);
        // Non-class values (primitives, containers, functions) answer an empty array —
        // a property grid can call fields() on anything (front door §A)
        if (auto* def = reflect_detail::resolve_class(eng, args[0], "fields")) {
            reflect_detail::append_fields_of(eng, def, arr);
        }
        return arr;
    });

    add("instances", [eng](const std::vector<script_value>& args) -> checked_result<script_value> {
        reflect_detail::expect_args(args, 1, "instances");
        auto* def = reflect_detail::resolve_class(eng, args[0], "instances");
        if (!def) { throw runtime_error("reflect::instances expects a class or class name"); }
        auto arr = script_value::make_array(nullptr, eng);
        for (auto& inst : def->live_instances()) {
            arr.as_array().push_back(
                script_value::make_object(def->get_name(), std::move(inst), eng));
        }
        return arr;
    });
}

// shared_ptr / raw-pointer convenience, matching every other stdlib unit
inline void register_reflection_functions(const std::shared_ptr<engine>& eng) { register_reflection_functions(*eng); }
inline void register_reflection_functions(engine* eng) { register_reflection_functions(*eng); }

} // namespace stdlib
} // namespace jai

#endif // JAISCRIPT_STDLIB_REFLECTION_HPP
