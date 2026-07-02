#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/runtime_errors.hpp>

namespace jai {

void environment::define(const std::string& name, const script_value& value) {
    uint64_t id = symbolizer_->intern(name);
    define(id, value);
}

void environment::define(const std::string& name, script_value&& value) {
    uint64_t id = symbolizer_->intern(name);
    define(id, std::move(value));
}

void environment::define(uint64_t id, const script_value& value) {
    // Check if we're redefining a local variable (shadowing parent is ok)
    if (local_ids_.count(id) > 0) {
        // Redefining local variable - update in place via flat_lookup_
        auto it = flat_lookup_.find(id);
        if (it != flat_lookup_.end()) {
            *(it->second) = value;
            return;
        }
    }

    // New local variable - add to stable storage
    local_storage_.push_back(value);
    flat_lookup_[id] = &local_storage_.back();  // Update/shadow in flat lookup
    local_ids_.insert(id);
}

void environment::define(uint64_t id, script_value&& value) {
    // Check if we're redefining a local variable (shadowing parent is ok)
    if (local_ids_.count(id) > 0) {
        // Redefining local variable - update in place via flat_lookup_
        auto it = flat_lookup_.find(id);
        if (it != flat_lookup_.end()) {
            *(it->second) = std::move(value);
            return;
        }
    }

    // New local variable - add to stable storage
    local_storage_.push_back(std::move(value));
    flat_lookup_[id] = &local_storage_.back();  // Update/shadow in flat lookup
    local_ids_.insert(id);
}

checked_result<script_value> environment::get(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    return get(id);
}

checked_result<script_value> environment::get(uint64_t id) const {
    // For method environments, handle 'this' specially
    if (kind_ == env_kind::method && id == symbolizer_->get_this_id()) {
        return this_object_;
    }

    // Check cache first (O(1))
    auto it = flat_lookup_.find(id);
    if (it != flat_lookup_.end()) {
        return *(it->second);
    }

    // FAST PATH: Iterate parent caches without function call overhead
    for (auto* p = parent_.get(); p != nullptr; p = p->parent_.get()) {
        auto parent_it = p->flat_lookup_.find(id);
        if (parent_it != p->flat_lookup_.end()) {
            // Found in ancestor cache - copy to our cache and return
            flat_lookup_[id] = parent_it->second;
            return *(parent_it->second);
        }
    }

    // SLOW PATH: No ancestor had it cached - do full lookup
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            // Cache for future O(1) access
            flat_lookup_[id] = ptr;
            return *ptr;
        }
    }

    // Kind-specific fallback: check 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) &&
            !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                // Try to get field first (non-throwing)
                const script_value& field_ref = instance->get_field(id, false);
                if (!field_ref.is_invalid()) {
                    return field_ref;
                }

                // Try to get method (non-throwing)
                script_value method = instance->get_method(id, false);
                if (!method.is_invalid()) {
                    bound_method_storage_ = make_bound_method(this_object_, method);
                    return bound_method_storage_;
                }

                // Try to get static field from class definition
                auto class_def = instance->get_class_definition();
                if (class_def && class_def->has_static_field(id)) {
                    return class_def->get_static_field(id);
                }
            }
        }
    }

    // Kind-specific fallback: check static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        // Check static fields
        if (class_def_->has_static_field(id)) {
            return class_def_->get_static_field(id);
        }

        // Check static methods (for calling other static methods)
        if (class_def_->has_static_method(id)) {
            return class_def_->get_static_method(id);
        }
    }

    // Not found anywhere - id is already the interned variable name
    return checked_result<script_value>(make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '{0}'", id);
}

checked_result<void> environment::assign(const std::string& name, const script_value& value) {
    uint64_t id = symbolizer_->intern(name);
    return assign(id, value);
}

checked_result<std::reference_wrapper<const script_value>> environment::get_ref(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    return get_ref(id);
}

checked_result<std::reference_wrapper<const script_value>> environment::get_ref(uint64_t id) const {
    // For method environments, handle 'this' specially
    if (kind_ == env_kind::method && id == symbolizer_->get_this_id()) {
        return std::cref(this_object_);
    }

    // Check cache first (O(1))
    auto it = flat_lookup_.find(id);
    if (it != flat_lookup_.end()) {
        return std::cref(*(it->second));
    }

    // FAST PATH: Iterate parent caches without function call overhead
    for (auto* p = parent_.get(); p != nullptr; p = p->parent_.get()) {
        auto parent_it = p->flat_lookup_.find(id);
        if (parent_it != p->flat_lookup_.end()) {
            flat_lookup_[id] = parent_it->second;
            return std::cref(*(parent_it->second));
        }
    }

    // SLOW PATH: Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return std::cref(*ptr);
        }
    }

    // Kind-specific fallback: check 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) &&
            !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                // Try to get instance field first (return const reference)
                const script_value& inst_field_ref = instance->get_field(id, false);
                if (!inst_field_ref.is_invalid()) {
                    return std::cref(inst_field_ref);
                }

                // Try to get static field from class definition (return const reference)
                auto class_def = instance->get_class_definition();
                if (class_def) {
                    const script_value* field_ptr = class_def->get_static_field_ptr(id);
                    if (field_ptr) {
                        return std::cref(*field_ptr);
                    }
                }
            }
        }
    }

    // Kind-specific fallback: check static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        const script_value* field_ptr = class_def_->get_static_field_ptr(id);
        if (field_ptr) {
            return std::cref(*field_ptr);
        }
    }

    return checked_result<std::reference_wrapper<const script_value>>(
        make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '{0}'", id);
}

checked_result<std::reference_wrapper<script_value>> environment::get_ref(const std::string& name) {
    uint64_t id = symbolizer_->intern(name);
    return get_ref(id);
}

checked_result<std::reference_wrapper<script_value>> environment::get_ref(uint64_t id) {
    // For method environments, handle 'this' specially (note: this is non-const so we return mutable ref)
    if (kind_ == env_kind::method && id == symbolizer_->get_this_id()) {
        return std::ref(this_object_);
    }

    // Check cache first (O(1))
    auto it = flat_lookup_.find(id);
    if (it != flat_lookup_.end()) {
        return std::ref(*(it->second));
    }

    // FAST PATH: Iterate parent caches without function call overhead
    for (auto* p = parent_.get(); p != nullptr; p = p->parent_.get()) {
        auto parent_it = p->flat_lookup_.find(id);
        if (parent_it != p->flat_lookup_.end()) {
            flat_lookup_[id] = parent_it->second;
            return std::ref(*(parent_it->second));
        }
    }

    // SLOW PATH: Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return std::ref(*ptr);
        }
    }

    // Kind-specific fallback: check 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) &&
            !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                // Try to get instance field first (return non-const reference)
                script_value& inst_field_ref = instance->get_field(id, false);
                if (!inst_field_ref.is_invalid()) {
                    return std::ref(inst_field_ref);
                }

                // Try to get static field from class definition (return non-const reference)
                auto class_def = instance->get_class_definition();
                if (class_def) {
                    script_value* field_ptr = class_def->get_static_field_ptr(id);
                    if (field_ptr) {
                        return std::ref(*field_ptr);
                    }
                }
            }
        }
    }

    // Kind-specific fallback: check static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        script_value* field_ptr = class_def_->get_static_field_ptr(id);
        if (field_ptr) {
            return std::ref(*field_ptr);
        }
    }

    return checked_result<std::reference_wrapper<script_value>>(
        make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '{0}'", id);
}

checked_result<void> environment::assign(const std::string& name, script_value&& value) {
    uint64_t id = symbolizer_->intern(name);
    return assign(id, std::move(value));
}

checked_result<void> environment::assign(uint64_t id, const script_value& value) {
    // Check cache first (O(1))
    auto it = flat_lookup_.find(id);
    if (it != flat_lookup_.end()) {
        *(it->second) = value;
        return {};
    }

    // FAST PATH: Iterate parent caches without function call overhead
    for (auto* p = parent_.get(); p != nullptr; p = p->parent_.get()) {
        auto parent_it = p->flat_lookup_.find(id);
        if (parent_it != p->flat_lookup_.end()) {
            flat_lookup_[id] = parent_it->second;
            *(parent_it->second) = value;
            return {};
        }
    }

    // SLOW PATH: Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            *ptr = value;
            return {};
        }
    }

    // Kind-specific fallback: assign to 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type)) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                if (instance && instance->has_field(id)) {
                    // Check if this is a C++ parent property that needs setter method
                    auto class_def = instance->get_class_definition();
                    if (class_def) {
                        auto cpp_base = class_def->get_cpp_base_class();
                        if (cpp_base) {
                            uint64_t setter_id = cpp_base->get_property_setter_id(id);
                            if (setter_id != 0) {
                                auto setter = cpp_base->get_method(setter_id, false);
                                if (setter.is_function()) {
                                    std::vector<script_value> args = {this_object_, value};
                                    auto result = setter.as_function()(args);
                                    if (!result) {
                                        return result.error_value();
                                    }
                                    return {};
                                }
                            }
                        }
                    }
                    instance->set_field(id, clone_for_assignment(value));
                    return {};
                }
            }
        }
    }

    // Kind-specific fallback: assign to static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        if (class_def_->has_static_field(id)) {
            if (class_def_->set_static_field(id, value.clone())) {
                return {};
            }
            // Field existed but set failed - shouldn't happen, but handle gracefully
        }
    }

    return checked_result<void>(
        make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '{0}'", id);
}

checked_result<void> environment::assign(uint64_t id, script_value&& value) {
    // Check cache first (O(1))
    auto it = flat_lookup_.find(id);
    if (it != flat_lookup_.end()) {
        *(it->second) = std::move(value);
        return {};
    }

    // FAST PATH: Iterate parent caches without function call overhead
    for (auto* p = parent_.get(); p != nullptr; p = p->parent_.get()) {
        auto parent_it = p->flat_lookup_.find(id);
        if (parent_it != p->flat_lookup_.end()) {
            flat_lookup_[id] = parent_it->second;
            *(parent_it->second) = std::move(value);
            return {};
        }
    }

    // SLOW PATH: Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            *ptr = std::move(value);
            return {};
        }
    }

    // Kind-specific fallback: assign to 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type)) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                if (instance && instance->has_field(id)) {
                    // Check if this is a C++ parent property that needs setter method
                    auto class_def = instance->get_class_definition();
                    if (class_def) {
                        auto cpp_base = class_def->get_cpp_base_class();
                        if (cpp_base) {
                            uint64_t setter_id = cpp_base->get_property_setter_id(id);
                            if (setter_id != 0) {
                                auto setter = cpp_base->get_method(setter_id, false);
                                if (setter.is_function()) {
                                    std::vector<script_value> args = {this_object_, std::move(value)};
                                    auto result = setter.as_function()(args);
                                    if (!result) {
                                        return result.error_value();
                                    }
                                    return {};
                                }
                            }
                        }
                    }
                    instance->set_field(id, std::move(value));
                    return {};
                }
            }
        }
    }

    // Kind-specific fallback: assign to static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        if (class_def_->has_static_field(id)) {
            if (class_def_->set_static_field(id, std::move(value))) {
                return {};
            }
            // Field existed but set failed - shouldn't happen, but handle gracefully
        }
    }

    return checked_result<void>(
        make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '{0}'", id);
}

bool environment::contains(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    return contains(id);
}

bool environment::contains(uint64_t id) const {
    // Check cache first (O(1))
    if (flat_lookup_.find(id) != flat_lookup_.end()) {
        return true;
    }

    // FAST PATH: Iterate parent caches without function call overhead
    for (auto* p = parent_.get(); p != nullptr; p = p->parent_.get()) {
        if (p->flat_lookup_.find(id) != p->flat_lookup_.end()) {
            // Found in ancestor cache - we know it exists
            // Note: we don't cache here since contains() is just a check
            return true;
        }
    }

    // SLOW PATH: Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return true;
        }
    }
    return false;
}

std::unordered_map<std::string_view, script_value> environment::get_local_variables() const {
    std::unordered_map<std::string_view, script_value> result;
    // Iterate over local_ids_ set and look up values via flat_lookup_
    for (uint64_t id : local_ids_) {
        auto it = flat_lookup_.find(id);
        if (it != flat_lookup_.end()) {
            result[symbolizer_->get_string(id)] = *(it->second);
        }
    }
    return result;
}

void environment::clear_values() {
    // Remove local IDs from flat_lookup_
    for (uint64_t id : local_ids_) {
        flat_lookup_.erase(id);
    }

    // Destroy locals in REVERSE declaration order (C++ LIFO semantics): script class
    // destructors observably fire here, and deque::clear()'s element destruction
    // order is implementation-defined (MS STL and libstdc++ disagree).
    while (!local_storage_.empty()) {
        local_storage_.pop_back();
    }
    local_ids_.clear();
}

void environment::clear_parent_cache() {
    // Remove non-local entries (parent/field pointers that may be stale)
    // Keep local variable entries (which have stable pointers into local_storage_)
    for (auto it = flat_lookup_.begin(); it != flat_lookup_.end(); ) {
        if (local_ids_.find(it->first) == local_ids_.end()) {
            it = flat_lookup_.erase(it);
        } else {
            ++it;
        }
    }
}

void environment::reset(std::shared_ptr<environment> new_parent) {
    // Clear all local values first
    clear_values();

    // Validate the parent chain before setting (debug mode only)
    validate_parent_chain(new_parent);

    // Only clear cache if parent actually changed
    // If same parent, cached pointers to parent chain remain valid
    // This is critical for recursive functions - avoids re-walking parent chain
    if (parent_.get() != new_parent.get()) {
        flat_lookup_.clear();
    }

    parent_ = new_parent;

    // Reset to standard kind and clear kind-specific fields
    kind_ = env_kind::standard;
    this_object_ = script_value::make_null(nullptr);
    class_def_.reset();
    bound_method_storage_ = script_value::make_null(nullptr);
}

void environment::reset_as_method(std::shared_ptr<environment> parent, script_value this_obj) {
    // Clear all local values first
    clear_values();

    // Validate the parent chain before setting (debug mode only)
    validate_parent_chain(parent);

    // Only clear cache if parent actually changed
    if (parent_.get() != parent.get()) {
        flat_lookup_.clear();
    }

    parent_ = parent;

    // Set to method kind with this object
    kind_ = env_kind::method;
    this_object_ = std::move(this_obj);
    class_def_.reset();
    bound_method_storage_ = script_value::make_null(nullptr);
}

void environment::reset_as_static_method(std::shared_ptr<environment> parent, std::shared_ptr<class_definition> class_def) {
    // Clear all local values first
    clear_values();

    // Validate the parent chain before setting (debug mode only)
    validate_parent_chain(parent);

    // Only clear cache if parent actually changed
    if (parent_.get() != parent.get()) {
        flat_lookup_.clear();
    }

    parent_ = parent;

    // Set to static_method kind with class definition
    kind_ = env_kind::static_method;
    this_object_ = script_value::make_null(nullptr);
    class_def_ = class_def;
    bound_method_storage_ = script_value::make_null(nullptr);
}

std::unordered_map<std::string_view, script_value> environment::get_all_variables() const {
    // With lazy caching, we need to walk the parent chain to get all variables
    std::unordered_map<std::string_view, script_value> allVars;

    // First, get all from parent (if any)
    if (parent_) {
        allVars = parent_->get_all_variables();
    }

    // Then add/override with our local variables
    for (uint64_t id : local_ids_) {
        auto it = flat_lookup_.find(id);
        if (it != flat_lookup_.end()) {
            allVars[symbolizer_->get_string(id)] = *(it->second);
        }
    }
    return allVars;
}

script_value* environment::get_value_ptr(uint64_t id) {
    // For method environments, handle 'this' specially
    if (kind_ == env_kind::method && id == symbolizer_->get_this_id()) {
        return &this_object_;
    }

    // Check cache first (O(1))
    auto it = flat_lookup_.find(id);
    if (it != flat_lookup_.end()) {
        return it->second;
    }

    // FAST PATH: Iterate parent caches without function call overhead
    // This is critical for deep recursion (Fibonacci, BST) where the same
    // function name is looked up repeatedly at each call depth.
    // Once any ancestor has it cached, all descendants benefit.
    for (auto* p = parent_.get(); p != nullptr; p = p->parent_.get()) {
        auto parent_it = p->flat_lookup_.find(id);
        if (parent_it != p->flat_lookup_.end()) {
            // Found in ancestor cache - copy to our cache and return
            flat_lookup_[id] = parent_it->second;
            return parent_it->second;
        }
    }

    // SLOW PATH: No ancestor had it cached - do full lookup
    // This handles first-time lookups, locals, 'this' fields, static fields, etc.
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return ptr;
        }
    }

    // Kind-specific fallback: check 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) &&
            !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                // Try to get instance field - get_field returns a reference
                script_value& field_ref = instance->get_field(id, false);
                if (!field_ref.is_invalid()) {
                    return &field_ref;
                }

                // Try to get static field from class definition
                auto class_def = instance->get_class_definition();
                if (class_def) {
                    script_value* static_ptr = class_def->get_static_field_ptr(id);
                    if (static_ptr) {
                        return static_ptr;
                    }
                }
            }
        }
    }

    // Kind-specific fallback: check static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        script_value* field_ptr = class_def_->get_static_field_ptr(id);
        if (field_ptr) {
            return field_ptr;
        }
    }

    return nullptr;
}

script_value make_bound_method(const script_value& this_obj, const script_value& method) {
    auto eng = this_obj.get_engine();
    return script_value::make_function([this_obj, method](const std::vector<script_value>& args) -> checked_result<script_value> {
        // Create a new argument list with 'this' as the first argument
        std::vector<script_value> method_args;
        method_args.reserve(args.size() + 1);
        method_args.push_back(this_obj);
        method_args.insert(method_args.end(), args.begin(), args.end());

        // Call the method with 'this' included
        const auto& method_func = method.as_function();
        auto result = method_func(method_args);

        // If the method returned a NON-OWNING reference into its receiver (the
        // classic chaining idiom `Counter& increment() { ...; return *this; }`
        // produces a cpp-bound T& whose object_holder owns nothing), pin the
        // receiver's underlying object onto the result so it survives even when
        // the receiver was a temporary, e.g. `Counter().increment().add(5)`.
        // Without this, the temporary receiver is freed when this bound-method
        // closure is destroyed, leaving the returned reference dangling
        // (a use-after-free that Release builds happened to mask).
        if (result && this_obj.is_object()) {
            script_value& rv = result.value();
            if (rv.is_non_owning_object()) {
                auto rv_holder = rv.get_object_holder();
                auto recv_holder = this_obj.get_object_holder();
                if (rv_holder && recv_holder) {
                    // Prefer the receiver's owning data; if the receiver is itself a
                    // non-owning link in the chain, propagate its existing anchor.
                    rv_holder->keep_alive = recv_holder->data ? recv_holder->data
                                                              : recv_holder->keep_alive;
                }
            }
        }
        return result;
    }, eng);
}

} // namespace jai
