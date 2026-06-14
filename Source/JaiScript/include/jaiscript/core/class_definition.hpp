#pragma once

#ifndef __JAISCRIPT_CORE_CLASS_DEFINITION_HPP__
#define __JAISCRIPT_CORE_CLASS_DEFINITION_HPP__

#include "engine.hpp"
#include <set>
#include <algorithm>

namespace jai {

// Forward declarations for script class support
class function_decl;
class interpreter;
class environment;

// Common types for class system
enum class access_level {
    public_access,
    private_access,
    protected_access
};

enum class delegation_type {
    none,
    same_class,
    base_class
};

class class_definition;

class class_instance : public std::enable_shared_from_this<class_instance> {
public:
    class_instance(const std::string& class_name, engine* eng = nullptr)
        : class_name_(class_name), engine_(eng), invalid_value_(std::monostate{}, eng) {}

    ~class_instance();

    void set_field(uint64_t id, const script_value& value) {
        auto it = fields_.find(id);
        if (it != fields_.end() && it->second.is_reference()) {
            it->second.deref() = value;
        } else {
            fields_.insert_or_assign(id, value);
        }
    }

    const script_value& get_field(uint64_t id, bool throw_if_missing = true) const;
    script_value& get_field(uint64_t id, bool throw_if_missing = true);

    bool has_field(uint64_t id) const;

    bool has_field_value(uint64_t id) const {
        return fields_.find(id) != fields_.end();
    }

    const std::string& get_class_name() const { return class_name_; }

    void migrate_fields(const std::set<uint64_t>& old_field_ids,
                       const std::unordered_map<uint64_t, script_value>& new_field_defaults) {
        std::unordered_map<uint64_t, script_value> new_fields;
        new_fields.reserve(new_field_defaults.size() + 1);

        for (const auto& [id, default_value] : new_field_defaults) {
            auto it = fields_.find(id);
            if (it != fields_.end()) {
                new_fields.emplace(id, std::move(it->second));
            } else {
                new_fields.emplace(id, default_value.clone());
            }
        }

        // The C++ base object lives in a runtime-only field (_cpp_object) that never appears
        // in the declared field defaults. Carry it across so a field-changing reload of a
        // script class extending a C++ class doesn't drop (and destruct) the C++ object and
        // leave inherited C++ methods uncallable.
        uint64_t cpp_id = get_cpp_object_field_id();
        if (cpp_id != 0 && new_fields.find(cpp_id) == new_fields.end()) {
            auto it = fields_.find(cpp_id);
            if (it != fields_.end()) {
                new_fields.emplace(cpp_id, std::move(it->second));
            }
        }

        fields_ = std::move(new_fields);
    }

    script_value get_method(uint64_t id, bool throw_if_missing = true) const;

    void set_class_definition(std::shared_ptr<class_definition> class_def) {
        class_def_ = class_def.get();
    }

    const std::unordered_map<uint64_t, script_value>& get_fields() const {
        return fields_;
    }

    class_definition* get_class_definition() const {
        return class_def_;
    }

    bool is_script_class() const;
    bool is_cpp_class() const;

    std::shared_ptr<void> get_cpp_object() const {
        uint64_t field_id = get_cpp_object_field_id();
        if (field_id == 0) return nullptr;

        auto cpp_field = get_field(field_id);
        if (!cpp_field.is_null() && cpp_field.is_object()) {
            return extract_cpp_object_impl(cpp_field);
        }
        return nullptr;
    }

    template<typename T>
    std::shared_ptr<T> get_cpp_object_as() const {
        auto obj = get_cpp_object();
        return std::static_pointer_cast<T>(obj);
    }

    bool has_cpp_object() const {
        uint64_t field_id = get_cpp_object_field_id();
        if (field_id == 0) return false;
        return !get_field(field_id).is_null();
    }

    std::shared_ptr<class_instance> deep_copy() const;

    void copy_fields_from(const class_instance& other) {
        for (const auto& [id, value] : other.fields_) {
            fields_.insert_or_assign(id, value.clone());
        }
    }

    uint64_t get_cpp_object_field_id() const;

    const script_value& get_cached_bound_method(uint64_t method_id) const {
        auto it = bound_method_cache_.find(method_id);
        if (it != bound_method_cache_.end()) {
            return it->second;
        }
        return invalid_value_;
    }

    void cache_bound_method(uint64_t method_id, const script_value& bound_method) const {
        bound_method_cache_[method_id] = bound_method;
    }

    bool has_cached_bound_method(uint64_t method_id) const {
        return bound_method_cache_.find(method_id) != bound_method_cache_.end();
    }

private:
    std::string class_name_;
    std::unordered_map<uint64_t, script_value> fields_;
    class_definition* class_def_ = nullptr;
    engine* engine_ = nullptr;
    mutable uint64_t cpp_object_field_id_ = 0;
    mutable script_value invalid_value_;
    mutable std::unordered_map<uint64_t, script_value> bound_method_cache_;

    static std::shared_ptr<void> extract_cpp_object_impl(const script_value& val);
};

class class_definition : public std::enable_shared_from_this<class_definition> {
protected:
    template<size_t ExpectedArgs>
    static void validate_method_args(const std::vector<script_value>& args,
                                    const std::string& method_name) {
        if (args.empty()) {
            throw runtime_error("Method '" + method_name + "' called without 'this' object");
        }
        if (args.size() != ExpectedArgs + 1) {
            throw runtime_error("Method '" + method_name + "' expects " +
                              std::to_string(ExpectedArgs) +
                              " arguments, got " + std::to_string(args.size() - 1));
        }
    }

public:
    enum class_type { cpp_class, script_class, vm_class };

    virtual ~class_definition() = default;

    class_definition(std::string_view name, uint64_t type_id, engine* eng)
        : name_(name), type_id_(type_id), type_info_(nullptr), engine_(eng),
          null_field_value_(std::monostate{}, eng), class_type_(cpp_class) {
        if (eng) {
            type_info_ = eng->get_type_info_object(type_id_);
        }
    }

    class_definition(std::string_view name, uint64_t type_id, class_type type, engine* eng)
        : name_(name), type_id_(type_id), type_info_(nullptr), engine_(eng),
          null_field_value_(std::monostate{}, eng), class_type_(type) {
        if (eng) {
            type_info_ = eng->get_type_info_object(type_id_);
        }
    }

    class_type get_class_type() const { return class_type_; }
    bool is_script_class() const { return class_type_ == script_class; }
    bool is_cpp_class() const { return class_type_ == cpp_class; }
    bool is_vm_class() const { return class_type_ == vm_class; }

    bool has_property_getters() const { return has_property_getters_; }

    type_info_ptr get_type_info() const { return type_info_; }

    void set_type_info(type_info_ptr type_info) { type_info_ = type_info; }

    void add_method_by_id(uint64_t name_id, script_function func, bool is_property_getter = false) {
        auto eng = engine_;
        if (!eng) return;

        if (is_property_getter) {
            has_property_getters_ = true;
        }

        methods_.insert_or_assign(name_id, script_value::make_function(func, engine_));
    }

    void add_method(const std::string& name, script_function func, size_t arity = SIZE_MAX) {
        auto eng = engine_;
        if (!eng) return;

        if (name.size() > 5 && name.substr(0, 5) == "_get_") {
            has_property_getters_ = true;
        }

        uint64_t name_id = eng->symbolize(name);

        if (arity != SIZE_MAX) {
            cpp_method_overloads_[name_id][arity] = func;
            method_arities_[name_id].push_back(arity);

            class_definition* class_def_ptr = this;

            methods_.insert_or_assign(name_id, script_value::make_function(
                [class_def_ptr, name_id](const std::vector<script_value>& args) -> checked_result<script_value> {
                    if (args.empty() || args[0].get_engine() == nullptr) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::class_not_found),
                                                            "Class definition no longer exists");
                    }

                    size_t script_arg_count = args.size() - 1;

                    auto& overloads_for_name = class_def_ptr->cpp_method_overloads_[name_id];
                    auto it = overloads_for_name.find(script_arg_count);
                    if (it != overloads_for_name.end()) {
                        return it->second(args);
                    }

                    return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                                                        "Method argument count mismatch: no overload accepts this number of arguments",
                                                        name_id);
                },
                engine_
            ));
        } else {
            methods_.insert_or_assign(name_id, script_value::make_function(func, engine_));
        }
    }

    void add_static_method(const std::string& name, script_function func, size_t arity = SIZE_MAX) {
        auto eng = engine_;
        if (!eng) return;

        if (name.size() > 5 && name.substr(0, 5) == "_get_") {
            has_property_getters_ = true;
        }

        uint64_t name_id = eng->symbolize(name);
        static_methods_.insert_or_assign(name_id, script_value::make_function(func, engine_));

        if (arity != SIZE_MAX) {
            static_method_arities_[name_id].push_back(arity);
        }
    }

    void add_static_method(uint64_t name_id, script_function func, size_t arity = SIZE_MAX) {
        static_methods_.insert_or_assign(name_id, script_value::make_function(func, engine_));

        if (arity != SIZE_MAX) {
            static_method_arities_[name_id].push_back(arity);
        }
    }

    void add_script_method(std::string_view name, std::shared_ptr<function_decl> ast, interpreter* interp, std::shared_ptr<environment> definition_env);

    void add_static_script_method(std::string_view name, std::shared_ptr<function_decl> ast, interpreter* interp, std::shared_ptr<environment> definition_env);

    void add_field(std::string_view name, const script_value& default_value) {
        auto eng = engine_;
        if (!eng) return;

        uint64_t name_id = eng->symbolize(name);

        if (default_value.get_engine() == nullptr && !!engine_) {
            script_value value_with_engine(default_value);
            value_with_engine.set_engine(engine_);
            field_defaults_.insert_or_assign(name_id, value_with_engine);
        } else {
            field_defaults_.insert_or_assign(name_id, default_value);
        }
        field_defaults_cache_valid_ = false;
    }

    void add_field(const std::string& name) {
        auto eng = engine_;
        if (!eng) return;

        uint64_t name_id = eng->symbolize(name);
        field_defaults_.insert_or_assign(name_id, script_value(std::monostate{}, engine_));
        field_defaults_cache_valid_ = false;
    }

    void add_static_field(uint64_t id, const script_value& initial_value) {
        static_fields_.insert(id);

        if (initial_value.get_engine() == nullptr && !!engine_) {
            script_value value_with_engine(initial_value);
            value_with_engine.set_engine(engine_);
            static_field_values_.insert_or_assign(id, value_with_engine);
        } else {
            static_field_values_.insert_or_assign(id, initial_value);
        }
    }

    // Hot reload: clear the instance-method overload sets + resolution cache so a reload
    // rebuilds them fresh (re-adding each method via add_script_method) instead of appending
    // to the previous definition's overloads.
    void clear_instance_method_overloads() {
        method_overloads_.clear();
        overload_resolution_cache_.clear();
    }

    // Hot reload: drop statics absent from the new definition so they're no longer accessible.
    void retain_static_fields(const std::set<uint64_t>& keep) {
        for (auto it = static_fields_.begin(); it != static_fields_.end();) {
            if (keep.find(*it) == keep.end()) {
                static_field_values_.erase(*it);
                it = static_fields_.erase(it);
            } else {
                ++it;
            }
        }
    }

    const script_value* get_static_field_ptr(uint64_t id) const {
        auto it = static_field_values_.find(id);
        if (it != static_field_values_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    script_value* get_static_field_ptr(uint64_t id) {
        auto it = static_field_values_.find(id);
        if (it != static_field_values_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const script_value& get_static_field(uint64_t id) const {
        const script_value* ptr = get_static_field_ptr(id);
        if (ptr) {
            return *ptr;
        }
        null_field_value_ = script_value::make_null(engine_);
        return null_field_value_;
    }

    script_value& get_static_field(uint64_t id) {
        script_value* ptr = get_static_field_ptr(id);
        if (ptr) {
            return *ptr;
        }
        null_field_value_ = script_value::make_null(engine_);
        return null_field_value_;
    }

    [[nodiscard]] bool set_static_field(uint64_t id, const script_value& value) {
        auto it = static_field_values_.find(id);
        if (it != static_field_values_.end()) {
            it->second = value;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool set_static_field(uint64_t id, script_value&& value) {
        auto it = static_field_values_.find(id);
        if (it != static_field_values_.end()) {
            it->second = std::move(value);
            return true;
        }
        return false;
    }

    bool is_static_field(uint64_t id) const {
        return static_fields_.find(id) != static_fields_.end();
    }

    bool has_static_field(uint64_t id) const {
        return static_field_values_.find(id) != static_field_values_.end();
    }

    script_value get_method(uint64_t id, bool throw_if_missing = true) const {
        auto it = methods_.find(id);
        if (it != methods_.end()) {
            return it->second;
        }

        for (const auto& parent : parent_classes_) {
            if (parent) {
                auto result = parent->get_method(id, false);
                if (!result.is_invalid()) {
                    return result;
                }
            }
        }

        if (cpp_base_class_) {
            auto result = cpp_base_class_->get_method(id, false);
            if (!result.is_invalid()) {
                return result;
            }
        }

        if (throw_if_missing) {
            std::string name;
            if (auto eng = engine_) {
                name = eng->get_symbolizer()->get_string(id);
            } else {
                name = std::to_string(id);
            }
            throw runtime_error("Method '" + name + "' not found in class '" + name_ + "'");
        }

        return script_value::make_invalid(engine_);
    }

    bool has_method(uint64_t id) const {
        return methods_.find(id) != methods_.end();
    }

    script_value get_static_method(uint64_t id, bool throw_if_missing = true) const {
        auto it = static_methods_.find(id);
        if (it != static_methods_.end()) {
            return it->second;
        }

        if (throw_if_missing) {
            std::string name;
            if (auto eng = engine_) {
                name = eng->get_symbolizer()->get_string(id);
            } else {
                name = std::to_string(id);
            }
            throw runtime_error("Static method '" + name + "' not found in class '" + name_ + "'");
        }
        return script_value::make_null(engine_);
    }

    bool has_static_method(uint64_t id) const {
        return static_methods_.find(id) != static_methods_.end();
    }

    bool has_static_method_with_arity(uint64_t name_id, size_t arity) const;

    std::vector<std::string_view> get_static_field_names() const {
        std::vector<std::string_view> names;
        names.reserve(static_fields_.size());
        if (auto eng = engine_) {
            for (const auto& field_id : static_fields_) {
                names.push_back(eng->get_symbolizer()->get_string(field_id));
            }
        }
        return names;
    }

    std::vector<std::string_view> get_static_method_names() const {
        std::vector<std::string_view> names;
        names.reserve(static_methods_.size());
        if (auto eng = engine_) {
            for (const auto& [method_id, _] : static_methods_) {
                names.push_back(eng->get_symbolizer()->get_string(method_id));
            }
        }
        return names;
    }

    std::shared_ptr<class_instance> create_instance() {
        auto* raw_instance = new class_instance(name_, engine_);

        std::shared_ptr<class_instance> instance;
        auto destructor_name = "~" + name_;
        uint64_t destructor_id = 0;
        if (auto eng = engine_) {
            destructor_id = eng->symbolize(destructor_name);
        }

        if (is_script_class() && destructor_id != 0 && has_method(destructor_id)) {
            auto class_def = shared_from_this();

            instance = std::shared_ptr<class_instance>(raw_instance,
                [class_def, destructor_name](class_instance* ptr) {
                    if (ptr) {
                        std::function<void(std::shared_ptr<class_definition>)> call_destructors;
                        call_destructors = [&](std::shared_ptr<class_definition> current_class) {
                            if (!current_class) return;

                            auto current_destructor_name = "~" + current_class->name_;
                            uint64_t destructor_id = 0;
                            if (auto eng = current_class->get_engine()) {
                                destructor_id = eng->symbolize(current_destructor_name);
                            }
                            if (destructor_id == 0) return;

                            auto method_it = current_class->methods_.find(destructor_id);

                            if (method_it != current_class->methods_.end()) {
                                try {
                                    std::shared_ptr<class_instance> temp_this(ptr, [](class_instance*){});

                                    const script_function& destructor_func = method_it->second.as_function();
                                    auto result = destructor_func({script_value::make_object(current_class->name_, temp_this, current_class->get_engine())});
                                    (void)result;
                                } catch (...) {
                                }
                            }

                            for (const auto& parent : current_class->parent_classes_) {
                                call_destructors(parent);
                            }
                        };

                        call_destructors(class_def);

                        delete ptr;
                    }
                });
        } else {
            instance = std::shared_ptr<class_instance>(raw_instance);
        }

        instance->set_class_definition(shared_from_this());

        register_instance(std::weak_ptr<class_instance>(instance));

        for (const auto& parent : parent_classes_) {
            if (parent) {
                const auto& parent_fields = parent->get_all_field_defaults();
                for (const auto& [field_id, default_value] : parent_fields) {
                    if (!instance->has_field(field_id)) {
                        instance->set_field(field_id, default_value);
                    }
                }
            }
        }

        for (const auto& [field_id, default_value] : field_defaults_) {
            instance->set_field(field_id, default_value);
        }

        return instance;
    }

    const std::unordered_map<uint64_t, script_value>& get_all_field_defaults() const {
        if (!field_defaults_cache_valid_) {
            all_field_defaults_cache_.clear();

            for (const auto& parent : parent_classes_) {
                if (parent) {
                    const auto& parent_fields = parent->get_all_field_defaults();
                    for (const auto& [id, value] : parent_fields) {
                        if (all_field_defaults_cache_.find(id) == all_field_defaults_cache_.end()) {
                            all_field_defaults_cache_[id] = value;
                        }
                    }
                }
            }

            for (const auto& [id, value] : field_defaults_) {
                all_field_defaults_cache_.insert_or_assign(id, value);
            }

            field_defaults_cache_valid_ = true;
        }

        return all_field_defaults_cache_;
    }

    void set_parent(std::shared_ptr<class_definition> parent) {
        parent_classes_.clear();
        if (parent) {
            parent_classes_.push_back(parent);
            parent->add_derived_class(shared_from_this());
            if (parent->has_property_getters()) {
                has_property_getters_ = true;
            }
        }
        field_defaults_cache_valid_ = false;
    }

    [[nodiscard]] bool add_parent(std::shared_ptr<class_definition> parent) {
        if (!parent) return true;

        for (const auto& existing : parent_classes_) {
            if (existing.get() == parent.get()) {
                return true;
            }
        }

        parent_classes_.push_back(parent);
        field_defaults_cache_valid_ = false;

        if (has_diamond_inheritance()) {
            parent_classes_.pop_back();
            return false;
        }

        parent->add_derived_class(shared_from_this());
        if (parent->has_property_getters()) {
            has_property_getters_ = true;
        }

        return true;
    }

    [[nodiscard]] bool set_parents(const std::vector<std::shared_ptr<class_definition>>& parents) {
        parent_classes_ = parents;
        field_defaults_cache_valid_ = false;

        if (has_diamond_inheritance()) {
            parent_classes_.clear();
            return false;
        }

        for (auto& parent : parent_classes_) {
            if (parent) {
                parent->add_derived_class(shared_from_this());
                if (parent->has_property_getters()) {
                    has_property_getters_ = true;
                }
            }
        }

        return true;
    }

    const std::vector<std::shared_ptr<class_definition>>& get_parent_classes() const {
        return parent_classes_;
    }

    std::shared_ptr<class_definition> get_parent() const {
        if (!parent_classes_.empty()) {
            return parent_classes_[0];
        }
        if (cpp_base_class_) {
            return cpp_base_class_;
        }
        return nullptr;
    }

    bool has_diamond_inheritance() const {
        if (parent_classes_.size() <= 1) {
            return false;
        }

        std::unordered_set<const class_definition*> visited;
        std::vector<const class_definition*> to_visit;

        for (const auto& parent : parent_classes_) {
            if (parent) {
                to_visit.push_back(parent.get());
                visited.insert(parent.get());
            }
        }

        size_t index = 0;
        while (index < to_visit.size()) {
            const class_definition* current = to_visit[index++];

            for (const auto& parent : current->parent_classes_) {
                if (parent) {
                    if (visited.count(parent.get()) > 0) {
                        return true;
                    }
                    visited.insert(parent.get());
                    to_visit.push_back(parent.get());
                }
            }
        }

        return false;
    }

    void add_derived_class(std::weak_ptr<class_definition> derived) {
        derived_classes_.push_back(derived);
    }

    void set_cpp_base_class(std::shared_ptr<class_definition> cpp_base) {
        cpp_base_class_ = cpp_base;
        if (cpp_base && cpp_base->has_property_getters()) {
            has_property_getters_ = true;
        }
    }

    std::shared_ptr<class_definition> get_cpp_base_class() const { return cpp_base_class_; }

    void register_property_setter(uint64_t field_id, uint64_t setter_id) {
        property_setter_ids_[field_id] = setter_id;
    }

    uint64_t get_property_setter_id(uint64_t field_id) const {
        auto it = property_setter_ids_.find(field_id);
        if (it != property_setter_ids_.end()) {
            return it->second;
        }
        return 0;
    }

    bool has_property_setter(uint64_t field_id) const {
        return property_setter_ids_.find(field_id) != property_setter_ids_.end();
    }

    bool is_transparent_wrapper() const { return is_transparent_wrapper_; }

    script_value unwrap(script_value& wrapper_value) const {
        if (!is_transparent_wrapper_ || !unwrap_function_) {
            return script_value(std::monostate{}, engine_);
        }
        return unwrap_function_(wrapper_value, engine_);
    }

    void set_unwrap_function(std::function<script_value(script_value&, engine*)> fn) {
        is_transparent_wrapper_ = true;
        unwrap_function_ = std::move(fn);
    }

    const std::string& get_name() const { return name_; }
    uint64_t get_type_id() const { return type_id_; }

    void set_type_id(uint64_t type_id) { type_id_ = type_id; }

    engine* get_engine() const { return engine_; }

    uint64_t get_cpp_object_field_id() const {
        if (cpp_object_field_id_ == 0) {
            if (auto eng = engine_) {
                cpp_object_field_id_ = eng->symbolize(class_constants::CPP_OBJECT_FIELD);
            }
        }
        return cpp_object_field_id_;
    }

    std::vector<std::string> get_property_names(bool include_inherited = true) const {
        std::vector<std::string> properties;
        std::unordered_set<std::string> seen;

        if (auto eng = engine_) {
            if (include_inherited) {
                for (const auto& parent : parent_classes_) {
                    if (parent) {
                        for (const auto& prop : parent->get_property_names(true)) {
                            if (seen.insert(prop).second) {
                                properties.push_back(prop);
                            }
                        }
                    }
                }
                if (cpp_base_class_) {
                    for (const auto& prop : cpp_base_class_->get_property_names(true)) {
                        if (seen.insert(prop).second) {
                            properties.push_back(prop);
                        }
                    }
                }
            }

            for (const auto& [id, default_value] : field_defaults_) {
                std::string prop_name(eng->get_symbolizer()->get_string(id));
                if (seen.insert(prop_name).second) {
                    properties.push_back(prop_name);
                }
            }
        }
        return properties;
    }

    bool is_subtype_of(const std::string& base_class_name) const {
        if (name_ == base_class_name) {
            return true;
        }

        for (const auto& parent : parent_classes_) {
            if (parent && parent->is_subtype_of(base_class_name)) {
                return true;
            }
        }

        if (cpp_base_class_ && cpp_base_class_->is_subtype_of(base_class_name)) {
            return true;
        }

        return false;
    }

    void set_default_access(access_level access) { default_access_ = access; }
    access_level get_default_access() const { return default_access_; }

    using copy_function = std::function<std::shared_ptr<void>(const void*)>;

    void set_copy_function(copy_function copier) {
        copy_function_ = std::move(copier);
    }

    bool has_copy_function() const {
        return copy_function_ != nullptr;
    }

    std::shared_ptr<void> copy_object(const void* src) const {
        if (!copy_function_) {
            throw runtime_error("No copy constructor available for class " + name_);
        }
        return copy_function_(src);
    }

    struct method_metadata {
        bool is_virtual = false;
        access_level access = access_level::public_access;
        size_t vtable_index = 0;
    };

    void set_method_metadata(std::string_view name, const method_metadata& metadata) {
        auto eng = engine_;
        if (!eng) return;
        uint64_t name_id = eng->symbolize(name);
        method_metadata_[name_id] = metadata;
    }

    const method_metadata* get_method_metadata(std::string_view name) const {
        auto eng = engine_;
        if (!eng) return nullptr;
        uint64_t name_id = eng->symbolize(name);
        auto it = method_metadata_.find(name_id);
        return it != method_metadata_.end() ? &it->second : nullptr;
    }

    method_metadata* get_method_metadata_mutable(std::string_view name) {
        auto eng = engine_;
        if (!eng) return nullptr;
        uint64_t name_id = eng->symbolize(name);
        auto it = method_metadata_.find(name_id);
        return it != method_metadata_.end() ? &it->second : nullptr;
    }

    struct method_info {
        std::shared_ptr<class_definition> owner_class;
        method_metadata* metadata;
    };

    method_info find_method(std::string_view name) {
        auto eng = engine_;
        if (!eng) return { nullptr, nullptr };

        uint64_t name_id = eng->symbolize(name);

        if (has_method(name_id)) {
            return { shared_from_this(), get_method_metadata_mutable(name) };
        }

        auto parent = get_parent();
        while (parent) {
            if (parent->has_method(name_id)) {
                return { parent, parent->get_method_metadata_mutable(name) };
            }
            parent = parent->get_parent();
        }

        return { nullptr, nullptr };
    }

    void initialize_fingerprint() {
        current_fingerprint_ = compute_fingerprint(field_defaults_, methods_, static_methods_);
    }

    void redefine_class(const std::unordered_map<uint64_t, script_value>& new_field_defaults,
                        const std::unordered_map<uint64_t, script_value>& new_methods,
                        const std::unordered_map<uint64_t, script_value>& new_static_methods,
                        engine* engine_ref) {
        field_defaults_cache_valid_ = false;
        size_t new_fingerprint = compute_fingerprint(new_field_defaults, new_methods, new_static_methods);

        if (new_fingerprint == current_fingerprint_) {
            for (size_t i = 0; i < instances_.size(); ++i) {
                if (auto instance = instances_[i].lock()) {
                    try {
                        instance->set_class_definition(shared_from_this());
                    } catch (const std::exception& e) {
                        (void)e;
                    }
                }
            }
            return;
        }
        bool fields_changed = false;

        if (field_defaults_.size() != new_field_defaults.size()) {
            fields_changed = true;
        } else {
            for (const auto& [id, _] : field_defaults_) {
                if (new_field_defaults.find(id) == new_field_defaults.end()) {
                    fields_changed = true;
                    break;
                }
            }
            if (!fields_changed) {
                for (const auto& [id, _] : new_field_defaults) {
                    if (field_defaults_.find(id) == field_defaults_.end()) {
                        fields_changed = true;
                        break;
                    }
                }
            }
        }

        std::set<uint64_t> old_fields;
        if (fields_changed) {
            for (const auto& [id, _] : field_defaults_) {
                old_fields.insert(id);
            }
        }

        uint64_t migrate_method_id = 0;
        if (engine_ref) {
            migrate_method_id = engine_ref->symbolize("hot_reload_migrate");
        }
        auto migrate_method_it = new_methods.find(migrate_method_id);

        methods_ = new_methods;
        static_methods_ = new_static_methods;

        has_property_getters_ = false;
        for (const auto& parent : parent_classes_) {
            if (parent && parent->has_property_getters()) {
                has_property_getters_ = true;
                break;
            }
        }
        if (!has_property_getters_ && cpp_base_class_ && cpp_base_class_->has_property_getters()) {
            has_property_getters_ = true;
        }
        if (!has_property_getters_ && engine_ref) {
            for (const auto& [method_id, _] : new_methods) {
                std::string_view name = engine_ref->get_symbolizer()->get_string(method_id);
                if (name.size() > 5 && name.substr(0, 5) == "_get_") {
                    has_property_getters_ = true;
                    break;
                }
            }
        }

        field_defaults_.clear();
        for (const auto& [id, value] : new_field_defaults) {
            if (value.get_engine() == nullptr && engine_ref) {
                script_value value_with_engine(value);
                value_with_engine.set_engine(engine_ref);
                field_defaults_[id] = value_with_engine;
            } else {
                field_defaults_[id] = value;
            }
        }

        // Migrate against a SNAPSHOT of the current instances. A hot_reload_migrate hook can
        // construct new same-class instances, which register into instances_ mid-migration;
        // iterating the live vector would migrate those too (re-invoking the hook) and loop
        // unboundedly. Snapshotted instances built against the new definition need no
        // migration, so skipping them is also correct.
        std::vector<std::weak_ptr<class_instance>> to_migrate = instances_;
        for (auto& weak_instance : to_migrate) {
            auto instance = weak_instance.lock();
            if (!instance) {
                continue;
            }
            if (fields_changed) {
                for (const auto& [name, default_value] : new_field_defaults) {
                    if (!instance->has_field(name)) {
                        instance->set_field(name, default_value.clone());
                    }
                }

                if (migrate_method_it != new_methods.end() && !migrate_method_it->second.is_null()) {
                    // Per-instance isolation: one instance's migrate hook erroring (throwing,
                    // or returning an error) must not abort the reload for the others.
                    try {
                        auto instance_value = script_value::make_object(name_, instance, engine_ref);
                        const script_function& migrate_func = migrate_method_it->second.as_function();
                        std::vector<script_value> args = {instance_value};
                        auto result = migrate_func(args);
                        (void)result;
                    } catch (...) {
                        // A migrate hook that throws (script throw, runtime error, anything)
                        // must not abort the reload for the other instances. Catch-all because
                        // a script throw is not necessarily a std::exception.
                    }
                }

                instance->migrate_fields(old_fields, new_field_defaults);
            }

            try {
                instance->set_class_definition(shared_from_this());
            } catch (const std::exception& e) {
                (void)e;
            }
        }
        // Drop dead weak refs; keep live ones plus any registered during migration.
        instances_.erase(
            std::remove_if(instances_.begin(), instances_.end(),
                [](const std::weak_ptr<class_instance>& w) { return w.expired(); }),
            instances_.end());

        method_metadata_.clear();

        // Propagate to derived classes (their inherited fields changed), snapshotting for the
        // same reason as above, then drop any dead derived refs.
        std::vector<std::weak_ptr<class_definition>> derived_snapshot = derived_classes_;
        for (auto& weak_derived : derived_snapshot) {
            if (auto derived = weak_derived.lock()) {
                derived->update_instances_from_base();
            }
        }
        derived_classes_.erase(
            std::remove_if(derived_classes_.begin(), derived_classes_.end(),
                [](const std::weak_ptr<class_definition>& w) { return w.expired(); }),
            derived_classes_.end());

        current_fingerprint_ = new_fingerprint;
    }

    void update_instances_from_base() {
        field_defaults_cache_valid_ = false;
        const auto& all_fields = get_all_field_defaults();
        std::set<uint64_t> dummy_old_fields;

        for (auto& weak_instance : instances_) {
            if (auto instance = weak_instance.lock()) {
                instance->migrate_fields(dummy_old_fields, all_fields);
            }
        }

        for (auto& weak_derived : derived_classes_) {
            if (auto derived = weak_derived.lock()) {
                derived->update_instances_from_base();
            }
        }
    }

    void register_instance(std::weak_ptr<class_instance> instance) {
        instances_.push_back(instance);
    }

    void unregister_instance(class_instance* instance) {
        instances_.erase(
            std::remove_if(instances_.begin(), instances_.end(),
                [instance](const std::weak_ptr<class_instance>& w) {
                    auto locked = w.lock();
                    return !locked || locked.get() == instance;
                }),
            instances_.end()
        );
    }

private:
    std::string name_;
    uint64_t type_id_;
    type_info_ptr type_info_;
    engine* engine_ = nullptr;
    std::unordered_map<uint64_t, script_value> methods_;
    std::unordered_map<uint64_t, std::vector<size_t>> method_arities_;
    std::unordered_map<uint64_t, std::unordered_map<size_t, script_function>> cpp_method_overloads_;
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<function_decl>>> method_overloads_;

    struct overload_cache_key {
        uint64_t name_id;
        size_t arity;
        bool operator==(const overload_cache_key& other) const noexcept {
            return name_id == other.name_id && arity == other.arity;
        }
    };
    struct overload_cache_hash {
        size_t operator()(const overload_cache_key& k) const noexcept {
            return std::hash<uint64_t>{}(k.name_id) ^ (std::hash<size_t>{}(k.arity) << 32);
        }
    };
    mutable std::unordered_map<overload_cache_key, std::shared_ptr<function_decl>, overload_cache_hash> overload_resolution_cache_;

    std::unordered_map<uint64_t, script_value> static_methods_;
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<function_decl>>> static_method_overloads_;
    std::unordered_map<uint64_t, std::vector<size_t>> static_method_arities_;
    std::unordered_map<uint64_t, script_value> field_defaults_;
    std::unordered_map<uint64_t, script_value> static_field_values_;
    std::unordered_set<uint64_t> static_fields_;
    std::vector<std::shared_ptr<class_definition>> parent_classes_;
    mutable std::unordered_map<uint64_t, script_value> all_field_defaults_cache_;
    mutable bool field_defaults_cache_valid_ = false;
    mutable uint64_t cpp_object_field_id_ = 0;
    mutable script_value null_field_value_;

    class_type class_type_;
    access_level default_access_ = access_level::public_access;

    std::shared_ptr<class_definition> cpp_base_class_;

    std::unordered_map<uint64_t, uint64_t> property_setter_ids_;

    bool has_property_getters_ = false;

    bool is_transparent_wrapper_ = false;
    std::function<script_value(script_value&, engine*)> unwrap_function_;

    copy_function copy_function_;

    std::unordered_map<uint64_t, method_metadata> method_metadata_;

    std::vector<std::weak_ptr<class_instance>> instances_;

    std::vector<std::weak_ptr<class_definition>> derived_classes_;

    size_t current_fingerprint_ = 0;

    template<typename T>
    static void hash_combine(size_t& seed, const T& val) {
        seed ^= std::hash<T>{}(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    static size_t compute_fingerprint(
        const std::unordered_map<uint64_t, script_value>& field_defaults,
        const std::unordered_map<uint64_t, script_value>& methods,
        const std::unordered_map<uint64_t, script_value>& static_methods = {}) {

        size_t hash = 0;

        std::vector<uint64_t> field_ids;
        field_ids.reserve(field_defaults.size());
        for (const auto& [id, _] : field_defaults) {
            field_ids.push_back(id);
        }
        std::sort(field_ids.begin(), field_ids.end());

        for (const auto& id : field_ids) {
            hash_combine(hash, id);
        }

        std::vector<std::pair<uint64_t, size_t>> method_hashes;
        method_hashes.reserve(methods.size());
        for (const auto& [id, value] : methods) {
            size_t impl_hash = 0;
            if (value.is_function()) {
                try {
                    const auto& func = value.as_function();
                    if (func.target_type() != typeid(void)) {
                        impl_hash = reinterpret_cast<size_t>(&func);
                    }
                } catch (...) {
                    impl_hash = 0;
                }
            }
            method_hashes.emplace_back(id, impl_hash);
        }
        std::sort(method_hashes.begin(), method_hashes.end());

        for (const auto& [id, impl_hash] : method_hashes) {
            hash_combine(hash, id);
            hash_combine(hash, impl_hash);
        }

        std::vector<std::pair<uint64_t, size_t>> static_method_hashes;
        static_method_hashes.reserve(static_methods.size());
        for (const auto& [id, value] : static_methods) {
            size_t impl_hash = 0;
            if (value.is_function()) {
                try {
                    const auto& func = value.as_function();
                    if (func.target_type() != typeid(void)) {
                        impl_hash = reinterpret_cast<size_t>(&func);
                    }
                } catch (...) {
                    impl_hash = 0;
                }
            }
            static_method_hashes.emplace_back(id, impl_hash);
        }
        std::sort(static_method_hashes.begin(), static_method_hashes.end());

        for (const auto& [id, impl_hash] : static_method_hashes) {
            hash_combine(hash, id);
            hash_combine(hash, std::string("static"));
            hash_combine(hash, impl_hash);
        }

        return hash;
    }
};

inline std::shared_ptr<class_definition> make_script_class_definition(const std::string& class_name, engine* eng) {
    return std::make_shared<class_definition>(class_name, class_definition::script_class, eng);
}

// Out-of-line class_instance implementations (need class_definition to be complete)

inline bool class_instance::is_script_class() const {
    return class_def_ && class_def_->is_script_class();
}

inline bool class_instance::is_cpp_class() const {
    return class_def_ && class_def_->is_cpp_class();
}

inline std::shared_ptr<void> class_instance::extract_cpp_object_impl(const script_value& val) {
    if (val.type() == script_value_type::jai_object_type) {
        auto obj_holder = val.get_object_holder();
        if (!obj_holder) {
            throw runtime_error("script_value is not an object type");
        }
        return obj_holder->data;
    }
    return nullptr;
}

inline uint64_t class_instance::get_cpp_object_field_id() const {
    if (cpp_object_field_id_ == 0 && class_def_) {
        if (auto eng = class_def_->get_engine()) {
            cpp_object_field_id_ = eng->symbolize(class_constants::CPP_OBJECT_FIELD);
        }
    }
    return cpp_object_field_id_;
}

inline std::shared_ptr<class_instance> class_instance::deep_copy() const {
    auto new_instance = std::make_shared<class_instance>(class_name_, engine_);

    uint64_t cpp_obj_id = get_cpp_object_field_id();
    for (const auto& [id, value] : fields_) {
        if (id == cpp_obj_id && !value.is_null()) {
            if (class_def_ && class_def_->has_copy_function()) {
                auto cpp_obj = extract_cpp_object_impl(value);
                if (cpp_obj) {
                    auto new_cpp_obj = class_def_->copy_object(cpp_obj.get());

                    if (!new_cpp_obj) {
                        throw runtime_error("Failed to copy C++ object of type '" + class_name_ +
                            "'. The registered copy function returned null.");
                    }

                    auto eng_ref = value.get_engine();
                    new_instance->set_field(cpp_obj_id,
                        script_value::make_cpp_object(class_name_, class_def_->get_type_id(), new_cpp_obj, eng_ref));
                    continue;
                }
            }
        }

        new_instance->set_field(id, value.clone());
    }

    new_instance->class_def_ = class_def_;

    if (class_def_) {
        class_def_->register_instance(std::weak_ptr<class_instance>(new_instance));
    }

    return new_instance;
}

inline const script_value& class_instance::get_field(uint64_t id, bool throw_if_missing) const {
    auto it = fields_.find(id);
    if (it != fields_.end()) {
        return it->second;
    }

    if (class_def_) {
        auto& field_defaults = class_def_->get_all_field_defaults();
        auto default_it = field_defaults.find(id);
        if (default_it != field_defaults.end()) {
            return default_it->second;
        }

        for (const auto& parent : class_def_->get_parent_classes()) {
            if (parent) {
                auto& parent_defaults = parent->get_all_field_defaults();
                auto parent_it = parent_defaults.find(id);
                if (parent_it != parent_defaults.end()) {
                    return parent_it->second;
                }
            }
        }
    }

    if (throw_if_missing) {
        std::string field_name;
        if (class_def_) {
            if (auto eng = class_def_->get_engine()) {
                field_name = eng->get_symbolizer()->get_string(id);
            } else {
                field_name = std::to_string(id);
            }
        } else {
            field_name = std::to_string(id);
        }
        throw runtime_error("Field '" + field_name + "' not found and no default value available");
    }

    if (class_def_) {
        invalid_value_ = script_value::make_invalid(class_def_->get_engine());
    } else {
        invalid_value_ = script_value::make_invalid(nullptr);
    }
    return invalid_value_;
}

inline script_value& class_instance::get_field(uint64_t id, bool throw_if_missing) {
    auto it = fields_.find(id);
    if (it != fields_.end()) {
        return it->second;
    }

    if (class_def_) {
        auto& field_defaults = class_def_->get_all_field_defaults();
        auto default_it = field_defaults.find(id);
        if (default_it != field_defaults.end()) {
            auto [new_it, _] = fields_.emplace(id, default_it->second.clone());
            return new_it->second;
        }

        for (const auto& parent : class_def_->get_parent_classes()) {
            if (parent) {
                auto& parent_defaults = parent->get_all_field_defaults();
                auto parent_it = parent_defaults.find(id);
                if (parent_it != parent_defaults.end()) {
                    auto [new_it, _] = fields_.emplace(id, parent_it->second.clone());
                    return new_it->second;
                }
            }
        }

        auto cpp_base = class_def_->get_cpp_base_class();
        if (cpp_base) {
            auto& cpp_defaults = cpp_base->get_all_field_defaults();
            auto cpp_it = cpp_defaults.find(id);
            if (cpp_it != cpp_defaults.end()) {
                auto [new_it, _] = fields_.emplace(id, cpp_it->second.clone());
                return new_it->second;
            }
        }
    }

    if (throw_if_missing) {
        std::string field_name;
        if (class_def_) {
            if (auto eng = class_def_->get_engine()) {
                field_name = eng->get_symbolizer()->get_string(id);
            } else {
                field_name = std::to_string(id);
            }
        } else {
            field_name = std::to_string(id);
        }
        throw runtime_error("Field '" + field_name + "' not found and no default value available");
    }

    if (class_def_) {
        invalid_value_ = script_value::make_invalid(class_def_->get_engine());
    } else {
        invalid_value_ = script_value::make_invalid(nullptr);
    }
    return invalid_value_;
}

inline bool class_instance::has_field(uint64_t id) const {
    if (fields_.find(id) != fields_.end()) {
        return true;
    }

    if (class_def_) {
        const auto& all_fields = class_def_->get_all_field_defaults();
        if (all_fields.find(id) != all_fields.end()) {
            return true;
        }

        auto cpp_base = class_def_->get_cpp_base_class();
        if (cpp_base) {
            const auto& cpp_fields = cpp_base->get_all_field_defaults();
            if (cpp_fields.find(id) != cpp_fields.end()) {
                return true;
            }
        }
    }

    return false;
}

inline script_value class_instance::get_method(uint64_t id, bool throw_if_missing) const {
    if (class_def_) {
        return class_def_->get_method(id, throw_if_missing);
    }

    if (throw_if_missing) {
        throw runtime_error("Class definition not available for method with ID " + std::to_string(id) + " lookup");
    }
    return script_value::make_invalid(nullptr);
}

inline class_instance::~class_instance() {
    if (class_def_) {
        class_def_->unregister_instance(this);
    }
}

} // namespace jai

#endif // __JAISCRIPT_CORE_CLASS_DEFINITION_HPP__
