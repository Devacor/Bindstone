#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/containers.hpp>
#include <stdexcept>
#include <sstream>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <filesystem>
#include <fstream>

namespace jai {

// Define static method registries for built-in types
const std::unordered_map<std::string, interpreter::builtin_method> interpreter::arrayMethods_ = {
    {"size", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("size() takes no arguments");
        }
        return interp->make_value(static_cast<script_int>(self.as_array().size()));
    }},
    
    {"push", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (args.size() != 1) {
            throw runtime_error("push() takes exactly one argument");
        }
        auto& arrayPtr = get_array_storage(self);
        arrayPtr->push_back(args[0].clone());  // Deep copy when pushing
        return interp->make_value();
    }},
    
    {"pop", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("pop() takes no arguments");
        }
        auto& arrayPtr = get_array_storage(self);
        if (arrayPtr->empty()) {
            throw runtime_error("Cannot pop from empty array");
        }
        script_value last = arrayPtr->back();
        arrayPtr->pop_back();
        return last;
    }},
    
    {"empty", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("empty() takes no arguments");
        }
        return interp->make_value(self.as_array().empty());
    }},
    
    {"clear", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("clear() takes no arguments");
        }
        auto& arrayPtr = get_array_storage(self);
        arrayPtr->clear();
        return interp->make_value();
    }},
    
    {"front", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("front() takes no arguments");
        }
        const auto& arr = self.as_array();
        if (arr.empty()) {
            throw runtime_error("Cannot get front of empty array");
        }
        return arr.front();
    }},
    
    {"back", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("back() takes no arguments");
        }
        const auto& arr = self.as_array();
        if (arr.empty()) {
            throw runtime_error("Cannot get back of empty array");
        }
        return arr.back();
    }}
};

const std::unordered_map<std::string, interpreter::builtin_method> interpreter::mapMethods_ = {
    {"size", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("size() takes no arguments");
        }
        return interp->make_value(static_cast<script_int>(self.as_map().size()));
    }},
    
    {"empty", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("empty() takes no arguments");
        }
        return interp->make_value(self.as_map().empty());
    }},
    
    {"clear", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("clear() takes no arguments");
        }
        auto& mapPtr = get_map_storage(self);
        mapPtr->clear();
        return interp->make_value();
    }},
    
    {"contains", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (args.size() != 1) {
            throw runtime_error("contains() takes exactly one argument");
        }
        const auto& map = self.as_map();
        return interp->make_value(map.find(args[0]) != map.end());
    }},
    
    {"erase", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (args.size() != 1) {
            throw runtime_error("erase() takes exactly one argument");
        }
        auto& mapPtr = get_map_storage(self);
        mapPtr->erase(args[0]);
        return interp->make_value();
    }},
    
    {"keys", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("keys() takes no arguments");
        }
        const auto& map = self.as_map();
        script_value result = script_value::make_array(nullptr, interp->engine_ref_);
        auto& arrayPtr = get_array_storage(result);
        arrayPtr->reserve(map.size());
        for (const auto& [key, value] : map) {
            arrayPtr->push_back(key.clone());
        }
        return result;
    }},
    
    {"values", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("values() takes no arguments");
        }
        const auto& map = self.as_map();
        script_value result = script_value::make_array(nullptr, interp->engine_ref_);
        auto& arrayPtr = get_array_storage(result);
        arrayPtr->reserve(map.size());
        for (const auto& [key, value] : map) {
            arrayPtr->push_back(value.clone());
        }
        return result;
    }}
};

const std::unordered_map<std::string, interpreter::builtin_method> interpreter::weakPtrMethods_ = {
    {"lock", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("lock() takes no arguments");
        }
        
        if (!self.is_weak_ptr()) {
            throw runtime_error("lock() can only be called on weak_ptr");
        }
        
        auto weak_ptr = self.get_weak_ptr();
        if (auto locked = weak_ptr.lock()) {
            // Reconstruct a script_value from the object_holder
            script_value result(std::monostate{}, interp->engine_ref_);
            
            // Get the original type info from the weak_ptr's type info
            auto weak_type_info = self.get_type_info();
            if (weak_type_info && weak_type_info->element_type()) {
                result.set_type_info(weak_type_info->element_type());
            } else {
                // Fallback: use the object type
                result.set_type_info(type_info::make_object(locked->type_name));
            }
            
            // Create the object value
            result = script_value::make_object(locked->type_name, locked->data, interp->engine_ref_);
            if (weak_type_info && weak_type_info->element_type()) {
                result.set_type_info(weak_type_info->element_type());
            }
            
            return result;
        } else {
            // weak_ptr is expired, return null
            return script_value::make_null(interp->engine_ref_);
        }
    }},
    
    {"expired", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("expired() takes no arguments");
        }
        
        if (!self.is_weak_ptr()) {
            throw runtime_error("expired() can only be called on weak_ptr");
        }
        
        auto weak_ptr = self.get_weak_ptr();
        return interp->make_value(weak_ptr.expired());
    }},
    
    {"reset", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("reset() takes no arguments");
        }
        
        if (!self.is_weak_ptr()) {
            throw runtime_error("reset() can only be called on weak_ptr");
        }
        
        // Reset the weak_ptr to null
        auto& weak_storage = self.get_weak_ptr_storage();
        weak_storage.reset();
        
        return self; // Return the reset weak_ptr
    }}
};

const std::unordered_map<std::string, interpreter::builtin_method> interpreter::sharedPtrMethods_ = {
    {"reset", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("reset() takes no arguments");
        }
        
        // In JaiScript, all objects are internally shared_ptr<object_holder>
        // Reset it to null while preserving the shared_ptr type
        auto current_type_info = self.get_type_info();
        self = script_value::make_null(interp->engine_ref_);
        self.set_type_info(current_type_info); // Preserve the shared_ptr<T> type
        
        return self; // Return the reset shared_ptr
    }},
    
    {"use_count", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("use_count() takes no arguments");
        }
        
        if (self.is_object()) {
            auto obj_holder = self.get_object_holder();
            if (obj_holder && obj_holder->data) {
                // Get the use count of the underlying shared_ptr
                long count = obj_holder->data.use_count();
                return interp->make_value(static_cast<script_int>(count));
            }
        }
        
        // Not a valid shared_ptr
        return interp->make_value(static_cast<script_int>(0));
    }},
    
    {"unique", [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("unique() takes no arguments");
        }
        
        if (self.is_object()) {
            auto obj_holder = self.get_object_holder();
            if (obj_holder && obj_holder->data) {
                // Check if use count is 1
                bool is_unique = (obj_holder->data.use_count() == 1);
                return interp->make_value(is_unique);
            }
        }
        
        // Not a valid shared_ptr
        return interp->make_value(false);
    }}
};

void environment::define(const std::string& name, const script_value& value) {
    uint64_t id = symbolizer_->intern(name);
    values_[id] = value;
}

void environment::define(const std::string& name, script_value&& value) {
    uint64_t id = symbolizer_->intern(name);
    values_[id] = std::move(value);
}

void environment::define(uint64_t id, const script_value& value) {
    values_[id] = value;
}

void environment::define(uint64_t id, script_value&& value) {
    values_[id] = std::move(value);
}

script_value environment::get(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    if (parent_) {
        return parent_->get(name);
    }
    
    throw runtime_error("Undefined variable '" + name + "'");
}

script_value environment::get(uint64_t id) const {
    return get(id, 0);
}

script_value environment::get(uint64_t id, int depth) const {
    // Prevent infinite recursion in environment chains
    const int MAX_RECURSION_DEPTH = 100;
    if (depth > MAX_RECURSION_DEPTH) {
        const std::string& name = symbolizer_->get_string(id);
        throw runtime_error("Maximum environment recursion depth exceeded for variable '" + name + "' at depth " + std::to_string(depth));
    }
    
    
    
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    if (parent_) {
        return parent_->get(id, depth + 1);
    }
    
    // Need to get the name for error message
    const std::string& name = symbolizer_->get_string(id);
    throw runtime_error("Undefined variable '" + name + "'");
}

void environment::assign(const std::string& name, const script_value& value) {
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = value;
        return;
    }
    
    if (parent_) {
        parent_->assign(name, value);
        return;
    }
    
    throw runtime_error("Undefined variable '" + name + "'");
}

const script_value& environment::get_ref(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    if (parent_) {
        return parent_->get_ref(name);
    }
    
    throw runtime_error("Undefined variable '" + name + "'");
}

const script_value& environment::get_ref(uint64_t id) const {
    const std::string& name = symbolizer_->get_string(id);
    if (name == "getValue") {
        std::cerr << "  this type: " << typeid(*this).name() << "\n";
    }
    return get_ref(id, 0);
}

const script_value& environment::get_ref(uint64_t id, int depth) const {
    // This version with depth is only called internally for recursion tracking
    // The public version delegates here
    const int MAX_RECURSION_DEPTH = 100;
    if (depth > MAX_RECURSION_DEPTH) {
        const std::string& name = symbolizer_->get_string(id);
        throw runtime_error("Maximum environment recursion depth exceeded for variable '" + name + "' at depth " + std::to_string(depth));
    }
    
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    if (parent_) {
        // For proper virtual dispatch, we need to call the public virtual method
        // on the parent, not this internal version
        const std::string& name = symbolizer_->get_string(id);
        if (name == "getValue") {
            std::cerr << "  parent type: " << typeid(*parent_).name() << "\n";
        }
        return parent_->get_ref(id);
    }
    
    // Need to get the name for error message
    const std::string& name = symbolizer_->get_string(id);
    throw runtime_error("Undefined variable '" + name + "'");
}

script_value& environment::get_ref(const std::string& name) {
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    if (parent_) {
        return parent_->get_ref(name);
    }
    
    throw runtime_error("Undefined variable '" + name + "'");
}

script_value& environment::get_ref(uint64_t id) {
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    if (parent_) {
        return parent_->get_ref(id);
    }
    
    const std::string& name = symbolizer_->get_string(id);
    throw runtime_error("Undefined variable '" + name + "'");
}

void environment::assign(const std::string& name, script_value&& value) {
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = std::move(value);
        return;
    }
    
    if (parent_) {
        parent_->assign(name, std::move(value));
        return;
    }
    
    throw runtime_error("Undefined variable '" + name + "'");
}

void environment::assign(uint64_t id, const script_value& value) {
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = value;
        return;
    }
    
    if (parent_) {
        parent_->assign(id, value);
        return;
    }
    
    const std::string& name = symbolizer_->get_string(id);
    throw runtime_error("Undefined variable '" + name + "'");
}

void environment::assign(uint64_t id, script_value&& value) {
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = std::move(value);
        return;
    }
    
    if (parent_) {
        parent_->assign(id, std::move(value));
        return;
    }
    
    const std::string& name = symbolizer_->get_string(id);
    throw runtime_error("Undefined variable '" + name + "'");
}

bool environment::contains(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    if (values_.find(id) != values_.end()) {
        return true;
    }
    return parent_ ? parent_->contains(name) : false;
}

bool environment::contains(uint64_t id) const {
    if (values_.find(id) != values_.end()) {
        return true;
    }
    return parent_ ? parent_->contains(id) : false;
}

std::unordered_map<std::string, script_value> environment::get_local_variables() const {
    std::unordered_map<std::string, script_value> result;
    for (const auto& [id, value] : values_) {
        result[symbolizer_->get_string(id)] = value;
    }
    return result;
}

void environment::reset(std::shared_ptr<environment> new_parent) {
    values_.clear();
    parent_ = new_parent;
}

std::unordered_map<std::string, script_value> environment::get_all_variables() const {
    std::unordered_map<std::string, script_value> allVars;
    
    // Start with parent's variables (if any)
    if (parent_) {
        allVars = parent_->get_all_variables();
    }
    
    // Add/override with local variables
    for (const auto& [id, value] : values_) {
        const std::string& name = symbolizer_->get_string(id);
        allVars[name] = value;
    }
    
    return allVars;
}

script_value* environment::get_value_ptr(uint64_t id) {
    auto it = values_.find(id);
    if (it != values_.end()) {
        return &it->second;
    }
    
    if (parent_) {
        return parent_->get_value_ptr(id);
    }
    
    return nullptr;
}

// interpreter implementation

// Helper to resolve include/import paths
std::string resolve_include_path(const std::string& path, std::shared_ptr<engine> engine_ptr) {
    // First, try the path as-is (for absolute paths)
    if (std::filesystem::exists(path)) {
        return std::filesystem::canonical(path).string();
    }
    
    // Get the include paths from the engine
    auto include_paths = engine_ptr->get_include_paths();
    
    // Try each include path
    for (const auto& include_path : include_paths) {
        auto full_path = std::filesystem::path(include_path) / path;
        if (std::filesystem::exists(full_path)) {
            return std::filesystem::canonical(full_path).string();
        }
    }
    
    // Path not found
    throw runtime_error("Could not find include/import file: " + path);
}

// Note: Direct engine implementation access removed
// Import tracking will be handled by the engine's public API

// Helper to create a bound method - binds 'this' as the first argument
script_value interpreter::create_bound_method(const script_value& this_obj, const script_value& method) {
    auto engine_weak = this_obj.get_engine_ref();
    return script_value::make_function([this_obj, method](const std::vector<script_value>& args) -> script_value {
        // Create a new argument list with 'this' as the first argument
        std::vector<script_value> method_args;
        method_args.reserve(args.size() + 1);
        method_args.push_back(this_obj);
        method_args.insert(method_args.end(), args.begin(), args.end());
        
        // Call the method with 'this' included
        const auto& method_func = method.as_function();
        return method_func(method_args);
    }, engine_weak);
}

// method_environment implementation
script_value method_environment::get(const std::string& name) const {
    // DEBUG: Log method environment lookup
    // std::cerr << "DEBUG: method_environment::get(\"" << name << "\")\n";
    
    // Special handling for 'this'
    if (name == "this") {
        return this_object_;
    }

    // First try normal environment lookup
    try {
        return environment::get(name);
    } catch (const runtime_error&) {
        // During class definition, we shouldn't resolve methods - let the unresolved identifier handling take over
        // The this_object_ might be invalid during class parsing
        if (name != "this" && this_object_.type() == script_value_type::jai_object_type && !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                
                // Try to get field first (non-throwing) - now returns a reference
                const script_value& field_ref = instance->get_field(name, false);
                if (!field_ref.is_invalid()) {
                    return field_ref;
                }
                
                // Try to get method (non-throwing)
                script_value method = instance->get_method(name, false);
                if (!method.is_invalid()) {
                    // Found the method! Store in instance member
                    bound_method_storage_ = interpreter::create_bound_method(this_object_, method);
                    return bound_method_storage_;
                }
                
                // Try to get static field from class definition
                auto class_def = instance->get_class_definition();
                if (class_def && class_def->has_static_field(name)) {
                    return class_def->get_static_field(name);
                }
            }
        }
        // Re-throw the original error
        throw;
    }
}

script_value method_environment::get(uint64_t id) const {
    // Special handling for 'this' using cached ID
    if (id == symbolizer_->get_this_id()) {
        return this_object_;
    }
    
    // First try normal environment lookup
    try {
        return environment::get(id);
    } catch (const runtime_error&) {
        // If not found, check 'this' object fields and methods
        const std::string& name = symbolizer_->get_string(id);
        if (name != "this" && this_object_.type() == script_value_type::jai_object_type) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                
                // Try to get field first (non-throwing) - now returns a reference
                const script_value& field_ref = instance->get_field(name, false);
                if (!field_ref.is_invalid()) {
                    return field_ref;
                }
                
                // Try to get method (non-throwing)
                script_value method = instance->get_method(name, false);
                if (!method.is_invalid()) {
                    // Found the method! Store in instance member
                    bound_method_storage_ = interpreter::create_bound_method(this_object_, method);
                    return bound_method_storage_;
                }
                
                // Try to get static field from class definition
                auto class_def = instance->get_class_definition();
                if (class_def && class_def->has_static_field(name)) {
                    return class_def->get_static_field(name);
                }
            }
        }
        // Re-throw the original error
        throw runtime_error("Undefined variable '" + name + "'");
    }
}

const script_value& method_environment::get_ref(const std::string& name) const {
    
    // First check if it's in our local values
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    
    // Then try parent environment lookup
    if (parent_) {
        try {
            return parent_->get_ref(name);
        } catch (const runtime_error&) {
            // Not in parent, fall through to check 'this'
        }
    }
    
    // Finally, check 'this' object fields and methods
        if (name != "this" && this_object_.type() == script_value_type::jai_object_type && !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                
                // Try to get field first (non-throwing) - now returns a reference
                const script_value& field_ref = instance->get_field(name, false);
                if (!field_ref.is_invalid()) {
                    return field_ref;
                }
                
                // Try to get method (non-throwing)
                script_value method = instance->get_method(name, false);
                if (!method.is_invalid()) {
                    // Found the method! Store in instance member
                    bound_method_storage_ = interpreter::create_bound_method(this_object_, method);
                    return bound_method_storage_;
                }
                
                // Try to get static field from class definition
                auto class_def = instance->get_class_definition();
                if (class_def && class_def->has_static_field(name)) {
                    return class_def->get_static_field(name);
                }
            }
        }
        throw runtime_error("Undefined variable '" + name + "'");
}

const script_value& method_environment::get_ref(uint64_t id) const {
    // Force no inlining
    volatile int x = 42;
    (void)x;
    
    const std::string& name = symbolizer_->get_string(id);
    
    // First check if it's in our local values (parameters, local variables)
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    // Then try parent environment lookup
    if (parent_) {
        try {
            return parent_->get_ref(id);
        } catch (const runtime_error&) {
            // Not in parent, fall through to check 'this'
        }
    }
    
    // Finally, check 'this' object fields and methods as fallback
    if (name != "this" && this_object_.type() == script_value_type::jai_object_type) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                
                // Try to get field first (non-throwing) - now returns a reference
                const script_value& field_ref = instance->get_field(name, false);
                if (!field_ref.is_invalid()) {
                    return field_ref;
                }
                
                // Try to get method (non-throwing)
                script_value method = instance->get_method(name, false);
                if (!method.is_invalid()) {
                    // Found the method! Store in instance member
                    bound_method_storage_ = interpreter::create_bound_method(this_object_, method);
                    return bound_method_storage_;
                }
                
                // Try to get static field from class definition
                auto class_def = instance->get_class_definition();
                if (class_def && class_def->has_static_field(name)) {
                    return class_def->get_static_field(name);
                }
            }
        }
        throw runtime_error("Undefined variable '" + name + "'");
}

script_value& method_environment::get_ref(const std::string& name) {
    
    // First check if it's in our local values
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    // Then try parent environment lookup
    if (parent_) {
        try {
            return parent_->get_ref(name);
        } catch (const runtime_error&) {
            // Not in parent, fall through to check 'this'
        }
    }
    
    // Finally, check 'this' object fields and methods
        if (name != "this" && this_object_.type() == script_value_type::jai_object_type && !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                
                // Try to get field first (non-throwing) - now returns a reference
                script_value& field_ref = instance->get_field(name, false);
                if (!field_ref.is_invalid()) {
                    return field_ref;
                }
                
                // Try to get method
                script_value method = instance->get_method(name, false);
                if (!method.is_invalid()) {
                    // Found the method! Store in instance member
                    bound_method_storage_ = interpreter::create_bound_method(this_object_, method);
                    return bound_method_storage_;
                }
                
                // Try to get static field from class definition
                auto class_def = instance->get_class_definition();
                if (class_def && class_def->has_static_field(name)) {
                    return class_def->get_static_field(name);
                }
            }
        }
        throw runtime_error("Undefined variable '" + name + "'");
}

script_value& method_environment::get_ref(uint64_t id) {
    const std::string& name = symbolizer_->get_string(id);
    
    
    // First check if it's in our local values (parameters, local variables)
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    // Then try parent environment lookup
    if (parent_) {
        try {
            return parent_->get_ref(id);
        } catch (const runtime_error&) {
            // Not in parent, fall through to check 'this'
        }
    }
    
    // Finally, check 'this' object fields and methods as fallback
    if (name != "this" && this_object_.type() == script_value_type::jai_object_type) {
        auto obj_holder = this_object_.get_object_holder();
        if (obj_holder && obj_holder->data) {
            auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
            
            // Try to get field first (non-throwing) - now returns a reference
            script_value& field_ref = instance->get_field(name, false);
            if (!field_ref.is_invalid()) {
                return field_ref;
            }
            
            // Try to get method (non-throwing)
            script_value method = instance->get_method(name, false);
            if (!method.is_invalid()) {
                // Found the method! Store in instance member
                bound_method_storage_ = interpreter::create_bound_method(this_object_, method);
                return bound_method_storage_;
            }
            
            // Try to get static field from class definition
            auto class_def = instance->get_class_definition();
            if (class_def && class_def->has_static_field(name)) {
                return class_def->get_static_field(name);
            }
        }
    }
    throw runtime_error("Undefined variable '" + name + "'");
}

void method_environment::assign(const std::string& name, const script_value& value) {
    // First check if it's a local variable or parameter
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = value;
        return;
    }
    
    // Check parent environments
    if (parent_ && parent_->contains(name)) {
        parent_->assign(name, value);
        return;
    }
    
    // If not found anywhere and 'this' has this field, update it
    if (name != "this" && this_object_.type() == script_value_type::jai_object_type) {
        auto obj_holder = this_object_.get_object_holder();
        if (obj_holder && obj_holder->data) {
            auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
            if (instance && instance->has_field(name)) {
                instance->set_field(name, value.clone());
                return;
            }
        }
    }
    
    // Not found anywhere - define it locally (this matches normal environment behavior)
    define(name, value);
}

void method_environment::assign(uint64_t id, const script_value& value) {
    // Convert to string name and use the string version
    const std::string& name = symbolizer_->get_string(id);
    assign(name, value);
}

// static_method_environment implementation
script_value static_method_environment::get(const std::string& name) const {
    std::cout << "[DEBUG] static_method_environment::get('" << name << "') called" << std::endl;
    std::cout << "[DEBUG] class_def_ = " << (class_def_ ? "valid" : "null") << std::endl;

    // First try normal environment lookup
    try {
        auto result = environment::get(name);
        std::cout << "[DEBUG] Found '" << name << "' in parent environment" << std::endl;
        return result;
    } catch (const runtime_error&) {
        std::cout << "[DEBUG] Not in parent environment, checking static members" << std::endl;

        // If not found, check static fields
        if (class_def_ && class_def_->has_static_field(name)) {
            std::cout << "[DEBUG] Found '" << name << "' as static field" << std::endl;
            return class_def_->get_static_field(name);
        }
        std::cout << "[DEBUG] Not a static field" << std::endl;

        // Also check static methods (for calling other static methods)
        if (class_def_ && class_def_->has_static_method(name)) {
            std::cout << "[DEBUG] Found '" << name << "' as static method" << std::endl;
            return class_def_->get_static_method(name);
        }
        std::cout << "[DEBUG] Not a static method either, throwing error" << std::endl;

        // Re-throw the original error
        throw;
    }
}

script_value static_method_environment::get(uint64_t id) const {
    // First try normal environment lookup
    try {
        return environment::get(id);
    } catch (const runtime_error&) {
        // If not found, check static fields
        const std::string& name = symbolizer_->get_string(id);
        if (class_def_ && class_def_->has_static_field(name)) {
            return class_def_->get_static_field(name);
        }
        // Also check static methods (for calling other static methods)
        if (class_def_ && class_def_->has_static_method(name)) {
            return class_def_->get_static_method(name);
        }
        // Re-throw the original error
        throw runtime_error("Undefined variable '" + name + "'");
    }
}

const script_value& static_method_environment::get_ref(const std::string& name) const {
    // First check if it's in our local values
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }

    // Then try parent environment lookup
    if (parent_) {
        try {
            return parent_->get_ref(name);
        } catch (const runtime_error&) {
            // Continue to static field check
        }
    }

    // Check static fields
    if (class_def_ && class_def_->has_static_field(name)) {
        return class_def_->get_static_field(name);
    }

    // Check static methods (for calling other static methods)
    if (class_def_ && class_def_->has_static_method(name)) {
        // Store in bound_method_storage_ to return a reference
        bound_method_storage_ = class_def_->get_static_method(name);
        return bound_method_storage_;
    }

    throw runtime_error("Undefined variable '" + name + "'");
}

const script_value& static_method_environment::get_ref(uint64_t id) const {
    const std::string& name = symbolizer_->get_string(id);
    return get_ref(name);
}

script_value& static_method_environment::get_ref(const std::string& name) {
    // First check if it's in our local values
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }

    // Then try parent environment lookup
    if (parent_) {
        try {
            return parent_->get_ref(name);
        } catch (const runtime_error&) {
            // Continue to static field check
        }
    }

    // Check static fields
    if (class_def_ && class_def_->has_static_field(name)) {
        return class_def_->get_static_field(name);
    }

    // Check static methods (for calling other static methods)
    if (class_def_ && class_def_->has_static_method(name)) {
        // Store in bound_method_storage_ to return a reference
        bound_method_storage_ = class_def_->get_static_method(name);
        return bound_method_storage_;
    }

    throw runtime_error("Undefined variable '" + name + "'");
}

script_value& static_method_environment::get_ref(uint64_t id) {
    const std::string& name = symbolizer_->get_string(id);
    return get_ref(name);
}

void static_method_environment::assign(const std::string& name, const script_value& value) {
    // First check if it's a local variable or parameter
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = value;
        return;
    }
    
    // Check parent environments
    if (parent_ && parent_->contains(name)) {
        parent_->assign(name, value);
        return;
    }
    
    // Check if it's a static field
    if (class_def_ && class_def_->has_static_field(name)) {
        class_def_->set_static_field(name, value);
        return;
    }
    
    // Not found anywhere - define it locally
    define(name, value);
}

void static_method_environment::assign(uint64_t id, const script_value& value) {
    const std::string& name = symbolizer_->get_string(id);
    assign(name, value);
}

interpreter::interpreter() 
    : ownedSymbolizer_(std::make_unique<string_symbolizer>()),
      string_symbolizer_(ownedSymbolizer_.get()),
      environment_(std::make_shared<environment>(string_symbolizer_)),
      hasReturnValue_(false),
      current_method_this_(std::monostate{}, std::weak_ptr<engine>{}) {
    // Initialize optimization pools
    argument_pool_.reserve(16);  // Reasonable default for most function calls
    environment_pool_.reserve(8);  // For nested function calls
    
    // Pre-populate environment pool
    for (size_t i = 0; i < 8; ++i) {
        environment_pool_.push_back(std::make_shared<environment>(nullptr, string_symbolizer_));
    }
    
    // Initialize binary operator dispatch table
    init_dispatch_table();
}

interpreter::interpreter(string_symbolizer* external_symbolizer)
    : ownedSymbolizer_(nullptr),
      string_symbolizer_(external_symbolizer),
      environment_(std::make_shared<environment>(string_symbolizer_)),
      hasReturnValue_(false),
      current_method_this_(std::monostate{}, std::weak_ptr<engine>{}) {
    // Initialize optimization pools
    argument_pool_.reserve(16);  // Reasonable default for most function calls
    environment_pool_.reserve(8);  // For nested function calls
    
    // Pre-populate environment pool
    for (size_t i = 0; i < 8; ++i) {
        environment_pool_.push_back(std::make_shared<environment>(nullptr, string_symbolizer_));
    }
    
    // Initialize binary operator dispatch table
    init_dispatch_table();
}

interpreter::interpreter(string_symbolizer* external_symbolizer, std::shared_ptr<environment> global_env)
    : ownedSymbolizer_(nullptr),
      string_symbolizer_(external_symbolizer),
      environment_(global_env),
      hasReturnValue_(false),
      current_method_this_(std::monostate{}, std::weak_ptr<engine>{}) {
    // Initialize optimization pools
    argument_pool_.reserve(16);  // Reasonable default for most function calls
    environment_pool_.reserve(8);  // For nested function calls
    
    // Pre-populate environment pool
    for (size_t i = 0; i < 8; ++i) {
        environment_pool_.push_back(std::make_shared<environment>(nullptr, string_symbolizer_));
    }
    
    // Initialize binary operator dispatch table
    init_dispatch_table();
}

void interpreter::add_globals(const std::unordered_map<std::string, script_value>& globals) {
    for (const auto& [name, value] : globals) {
        environment_->define(name, value);
    }
}

void interpreter::add_global(const std::string& name, const script_value& value) {
    environment_->define(name, value);
}

void interpreter::prepare_for_execution() {
    // Clear execution state
    valueStack_.clear();
    returnValue_ = make_value();
    hasReturnValue_ = false;
    
    // Clear exception state
    current_exception_.reset();
    is_unwinding_ = false;
    active_exception_value_ = make_value();
    current_catch_var_.clear();
    
    // Reset to global scope but keep all variables defined at global scope
    // Only pop scopes if we're in a nested scope
    while (environment_->parent_) {
        environment_ = environment_->parent_;
    }
    // Note: We don't clear the global environment, so variables persist between executions
}

void interpreter::push_scope() {
    environment_ = std::make_shared<environment>(environment_, string_symbolizer_);
}

void interpreter::pop_scope() {
    if (environment_->parent_) {
        environment_ = environment_->parent_;
    }
}

void interpreter::define_variable(const std::string& name, const script_value& value) {
    environment_->define(name, value);
}

script_value interpreter::execute(const std::vector<declaration_ptr>& declarations) {
    // std::cerr << "DEBUG: interpreter::execute called with " << declarations.size() << " declarations\n";
    script_value last_script_value = script_value::make_null(engine_ref_);
    hasReturnValue_ = false;  // Reset return value state
    
    for (size_t i = 0; i < declarations.size(); i++) {
        const auto& decl = declarations[i];
        // std::cerr << "  Declaration " << i << " type: " << typeid(*decl).name() << "\n";

        // Execute declaration with exception handling
        try {
            // std::cerr << "  About to visit declaration " << i << "\n";
            decl->accept(this);
            // std::cerr << "  Finished visiting declaration " << i << "\n";
        } catch (const script_exception& e) {
            // Convert to interpreter exception state
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = e;
            is_unwinding_ = true;
        } catch (const std::runtime_error& e) {
            // Convert runtime errors to script exceptions
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = script_exception(e.what());
            is_unwinding_ = true;
        }
        
        // Check if we're unwinding due to an uncaught exception
        if (is_unwinding_) {
            // Stop executing further declarations
            break;
        }
        
        // Check if this is an implicit return expression
        if (auto* expr_decl = dynamic_cast<expression_decl*>(decl.get())) {
            if (expr_decl->implicit_return && !valueStack_.empty()) {
                last_script_value = pop_value();
                // Dereference in case it's a reference (for expressions like m["key"] that return references)
                last_script_value = last_script_value.deref();
            }
        }
        
        // Clear any remaining values on the stack (from non-implicit expressions)
        while (!valueStack_.empty()) {
            pop_value();
        }
        
        // If we hit a return statement, break out of execution
        if (hasReturnValue_) {
            reset_environment_pool();  // Reset pool for next execution
            return returnValue_.value();
        }
    }
    


    reset_environment_pool();  // Reset pool for next execution
    return last_script_value;
}

script_value interpreter::evaluate(expression_ptr expr) {
    expr->accept(this);
    return pop_value();
}

// Variable access methods
script_value interpreter::get_variable(const std::string& name) const {
    return environment_->get(name).deref();
}

bool interpreter::has_variable(const std::string& name) const {
    return environment_->contains(name);
}

std::unordered_map<std::string, script_value> interpreter::get_all_variables() const {
    // Since we should be at root scope after execution, just return local variables
    return environment_->get_local_variables();
}


// expression visitors
void interpreter::visit_literal_expr(literal_expr* expr) {
    // Literals are created at parse time without engine references
    // Create a new value with the proper engine reference based on the literal's type
    switch (expr->value.type()) {
        case script_value_type::jai_int_type:
            push_value(make_value(expr->value.as_int()));
            break;
        case script_value_type::jai_float_type:
            push_value(make_value(expr->value.as_float()));
            break;
        case script_value_type::jai_string_type:
            push_value(make_value(expr->value.as_string()));
            break;
        case script_value_type::jai_char_type:
            push_value(make_value(expr->value.as_char()));
            break;
        case script_value_type::jai_bool_type:
            push_value(make_value(expr->value.as_bool()));
            break;
        case script_value_type::jai_null_type:
            push_value(make_value());
            break;
        default:
            // For other types, try to set engine ref (though this shouldn't happen with literals)
            expr->value.set_engine_ref(engine_ref_);
            push_value(expr->value);
            break;
    }
}

void interpreter::visit_identifier_expr(identifier_expr* expr) {
    if (expr->name == "getValue") {
    }
    // Check if this identifier is the current catch variable
    if (!current_catch_var_.empty() && expr->name == current_catch_var_) {
        push_value(active_exception_value_.value());
        return;
    }
    
    // Special handling for type constructors like weak_ptr<T>, shared_ptr<T>
    if (expr->name.find("weak_ptr<") == 0 || expr->name.find("shared_ptr<") == 0) {
        // This is a type constructor being used as a function
        // Extract the base type name (weak_ptr or shared_ptr)
        size_t pos = expr->name.find('<');
        std::string base_type = expr->name.substr(0, pos);
        
        // Look up the constructor function for this type
        try {
            script_value constructor_func = environment_->get(base_type);
            if (constructor_func.is_function()) {
                push_value(constructor_func);
                return;
            }
        } catch (const runtime_error&) {
            // Fall through to normal error handling
        }
    }
    
    // Use cached symbol ID if available, otherwise compute and cache it
    if (expr->symbol_id == UINT64_MAX) {
        expr->symbol_id = string_symbolizer_->intern(expr->name);
    }
    
    // Try to get the variable from environment
    try {
        
        const script_value& val = environment_->get_ref(expr->symbol_id);
        push_value(val.deref());  // Automatically handles references
    } catch (const runtime_error& e) {
        // If we're in a class method context, collect unresolved identifier
        if (current_class_context_ && current_class_context_->in_method) {
            // Add to unresolved identifiers for later validation
            current_class_context_->unresolved_identifiers.insert(expr->name);
            // Push a placeholder value to continue parsing
            push_value(make_value());
            return;
        }
        
        
        // Variable not found - check if it's a member of 'this'
        try {
            script_value this_val = environment_->get("this");
            if (this_val.is_object()) {
                // Try to access as a member of 'this'
                auto obj_holder = this_val.get_object_holder();
                std::shared_ptr<class_instance> instance;
                
                // For C++ classes, is_cpp_class_instance is true and data is class_instance
                if (obj_holder->is_cpp_class_instance) {
                    instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                } else {
                    // For script classes, the data IS a class_instance directly
                    instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                }
                
                if (instance) {
                    // Check instance fields first
                    if (instance->has_field(expr->name)) {
                        push_value(instance->get_field(expr->name));
                        return;
                    }
                    
                    // Check for static fields of the class
                    auto class_def = instance->get_class_definition();
                    if (class_def && class_def->has_static_field(expr->name)) {
                        push_value(class_def->get_static_field(expr->name));
                        return;
                    }
                }
            }
        } catch (...) {
            // No 'this' in scope
        }

        // Set exception state instead of throwing C++ exception
        active_exception_value_ = make_value("Undefined variable '" + expr->name + "'");
        current_exception_ = script_exception("Undefined variable '" + expr->name + "'", expr->location);
        is_unwinding_ = true;
        push_value(make_value());  // Push null for failed variable access
        return;
    }
}

void interpreter::visit_binary_expr(binary_expr* expr) {
    // ULTRA-FAST PATH: Literal expressions like "2 + 3" - avoid all AST traversal
    if (auto* leftLit = dynamic_cast<literal_expr*>(expr->left.get())) {
        if (auto* rightLit = dynamic_cast<literal_expr*>(expr->right.get())) {
            const script_value& leftVal = leftLit->value;
            const script_value& rightVal = rightLit->value;
            
            // Fast path for integer arithmetic (most common case) - but only if no custom ops
            if (leftVal.is_int() && rightVal.is_int() && can_use_fast_path(expr->op.type)) {
                script_int leftInt = leftVal.as_int();
                script_int rightInt = rightVal.as_int();
                
                switch (expr->op.type) {
                    case token_type::plus:
                        push_value(make_value(leftInt + rightInt));
                        return;
                    case token_type::minus:
                        push_value(make_value(leftInt - rightInt));
                        return;
                    case token_type::star:
                        push_value(make_value(leftInt * rightInt));
                        return;
                    case token_type::slash:
                        if (rightInt == 0) throw runtime_error("Division by zero");
                        push_value(make_value(leftInt / rightInt));
                        return;
                    case token_type::percent:
                        if (rightInt == 0) throw runtime_error("Division by zero");
                        push_value(make_value(leftInt % rightInt));
                        return;
                    case token_type::less:
                        push_value(make_value(leftInt < rightInt));
                        return;
                    case token_type::less_equal:
                        push_value(make_value(leftInt <= rightInt));
                        return;
                    case token_type::greater:
                        push_value(make_value(leftInt > rightInt));
                        return;
                    case token_type::greater_equal:
                        push_value(make_value(leftInt >= rightInt));
                        return;
                    case token_type::equal_equal:
                        push_value(make_value(leftInt == rightInt));
                        return;
                    case token_type::bang_equal:
                        push_value(make_value(leftInt != rightInt));
                        return;
                    case token_type::spaceship:
                        push_value(make_value(leftInt < rightInt ? script_int(-1) : (leftInt > rightInt ? script_int(1) : script_int(0))));
                        return;
                    default:
                        break; // Fall through to normal path
                }
            }
            // Fast path for float arithmetic - but only if no custom ops
            else if ((leftVal.is_float() || leftVal.is_int()) && (rightVal.is_float() || rightVal.is_int()) && can_use_fast_path(expr->op.type)) {
                script_float leftFloat = leftVal.is_int() ? static_cast<script_float>(leftVal.as_int()) : leftVal.as_float();
                script_float rightFloat = rightVal.is_int() ? static_cast<script_float>(rightVal.as_int()) : rightVal.as_float();
                
                switch (expr->op.type) {
                    case token_type::plus:
                        push_value(make_value(leftFloat + rightFloat));
                        return;
                    case token_type::minus:
                        push_value(make_value(leftFloat - rightFloat));
                        return;
                    case token_type::star:
                        push_value(make_value(leftFloat * rightFloat));
                        return;
                    case token_type::slash:
                        if (rightFloat == 0.0) throw runtime_error("Division by zero");
                        push_value(make_value(leftFloat / rightFloat));
                        return;
                    case token_type::percent:
                        if (rightFloat == 0.0) throw runtime_error("Division by zero");
                        push_value(make_value(std::fmod(leftFloat, rightFloat)));
                        return;
                    case token_type::less:
                        push_value(make_value(leftFloat < rightFloat));
                        return;
                    case token_type::less_equal:
                        push_value(make_value(leftFloat <= rightFloat));
                        return;
                    case token_type::greater:
                        push_value(make_value(leftFloat > rightFloat));
                        return;
                    case token_type::greater_equal:
                        push_value(make_value(leftFloat >= rightFloat));
                        return;
                    case token_type::equal_equal:
                        push_value(make_value(leftFloat == rightFloat));
                        return;
                    case token_type::bang_equal:
                        push_value(make_value(leftFloat != rightFloat));
                        return;
                    case token_type::spaceship:
                        push_value(make_value(leftFloat < rightFloat ? script_int(-1) : (leftFloat > rightFloat ? script_int(1) : script_int(0))));
                        return;
                    default:
                        break;
                }
            }
            // Fast path for string concatenation
            else if (expr->op.type == token_type::plus && leftVal.is_string() && rightVal.is_string()) {
                push_value(make_value(leftVal.as_string() + rightVal.as_string()));
                return;
            }
        }
    }

    // Handle logical operators specially for short-circuit evaluation
    if (expr->op.type == token_type::ampersand_ampersand || expr->op.type == token_type::pipe_pipe) {
        expr->left->accept(this);
        script_value left = pop_value();
        
        bool leftTruthy = is_truthy(left);
        
        if (expr->op.type == token_type::ampersand_ampersand) {
            if (!leftTruthy) {
                push_value(left);  // Short-circuit: return left (falsy)
                return;
            }
        } else { // pipe_pipe
            if (leftTruthy) {
                push_value(left);  // Short-circuit: return left (truthy)
                return;
            }
        }
        
        // Evaluate right side
        expr->right->accept(this);
        // Result is already on stack
        return;
    }
    
    // Evaluate operands once and use them throughout
    expr->left->accept(this);
    script_value left_raw = pop_value();  // Keep raw value for subscript handling
    script_value left = left_raw.deref();  // Dereferenced version for most operations
    
    expr->right->accept(this);
    // Check if we're unwinding due to an exception in the right expression
    if (is_unwinding_) {
        // Don't try to pop a value that wasn't pushed due to the exception
        return;
    }
    script_value right = pop_value().deref();  // Handle references safely
    
    // Check for custom operator functions first
    std::string opName;
    switch (expr->op.type) {
        case token_type::plus: opName = "+"; break;
        case token_type::minus: opName = "-"; break;
        case token_type::star: opName = "*"; break;
        case token_type::slash: opName = "/"; break;
        case token_type::percent: opName = "%"; break;
        case token_type::less: opName = "<"; break;
        case token_type::less_equal: opName = "<="; break;
        case token_type::greater: opName = ">"; break;
        case token_type::greater_equal: opName = ">="; break;
        case token_type::equal_equal: opName = "=="; break;
        case token_type::bang_equal: opName = "!="; break;
        case token_type::spaceship: opName = "<=>"; break;
        case token_type::ampersand: opName = "&"; break;
        case token_type::pipe: opName = "|"; break;
        case token_type::caret: opName = "^"; break;
        case token_type::left_shift: opName = "<<"; break;
        case token_type::right_shift: opName = ">>"; break;
        default: break;
    }
    
    // Check for custom operator function (excluding subscript)
    if (!opName.empty() && environment_ && environment_->contains(opName)) {
        try {
            script_value opFunc = environment_->get(opName);
            if (opFunc.is_function()) {
                const script_function& func = opFunc.as_function();
                std::vector<script_value> args = {left, right};
                push_value(func(args));
                return;
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            if (error.find("Undefined variable") == std::string::npos) {
                throw;
            }
        }
    }
    
    // Handle subscript operation specially
    if (expr->op.type == token_type::left_bracket) {
        if (left.is_array()) {
            if (!right.is_int()) {
                throw runtime_error("Array index must be an integer");
            }
            script_int index = right.as_int();
            const auto& array = left.as_array();
            
            if (index < 0 || index >= static_cast<script_int>(array.size())) {
                throw runtime_error("Array index out of bounds: " + std::to_string(index));
            }
            
            // For arrays, check if this is a true temporary
            auto array_ptr = left.get_array_storage();
            bool is_temporary = array_ptr.use_count() == 1;
            
            if (!is_temporary) {
                // This array is stored somewhere, allow assignment
                auto& mut_array = const_cast<std::vector<script_value>&>(array);
                script_value* element_ptr = &mut_array[index];
                script_value ref_value = script_value::make_reference(element_ptr, environment_);
                push_value(ref_value);
            } else {
                // True temporary, read-only access
                push_value(array[index]);
            }
        } else if (left.is_map()) {
            // For maps, we need to handle both assignment and read access
            // Try to get a mutable reference if possible
            try {
                auto& map = const_cast<std::map<script_value, script_value>&>(left.as_map());
                
                // Check if this is a true temporary by seeing if the shared_ptr is unique
                // If it's shared (refcount > 1), it means it's stored in a variable
                auto map_ptr = left.get_map_storage();
                bool is_temporary = map_ptr.use_count() == 1;
                
                if (!is_temporary) {
                    // This map is stored somewhere (variable, field, etc.), allow assignment
                    script_value& value_ref = map[right];
                    
                    // If this created a new entry with default constructor, it has invalid engine reference
                    if (!value_ref.has_valid_engine_ref()) {
                        if (!left.has_valid_engine_ref()) {
                            throw runtime_error("Invalid script_value: both map and new entry missing engine reference");
                        }
                        value_ref.set_engine_ref(left.get_engine_ref());
                    }
                    
                    script_value* element_ptr = &value_ref;
                    script_value ref_value = script_value::make_reference(element_ptr, environment_);
                    push_value(ref_value);
                } else {
                    // This is a true temporary (e.g., function return), read-only access
                    auto it = map.find(right);
                    if (it != map.end()) {
                        // Ensure the value has an engine ref before pushing
                        script_value val = it->second;
                        if (!val.has_valid_engine_ref()) {
                            val.set_engine_ref(engine_ref_);
                        }
                        push_value(val);
                    } else {
                        push_value(script_value(std::monostate{}, engine_ref_));
                    }
                }
            } catch (...) {
                // Fallback for any edge cases
                const auto& map = left.as_map();
                auto it = map.find(right);
                if (it != map.end()) {
                    // Ensure the value has an engine ref before pushing
                    script_value val = it->second;
                    if (!val.has_valid_engine_ref()) {
                        val.set_engine_ref(engine_ref_);
                    }
                    push_value(val);
                } else {
                    push_value(script_value(std::monostate{}, engine_ref_));
                }
            }
        } else {
            if (left.is_object()) {
                try {
                    script_value getMethod = environment_->get("[]");
                    if (getMethod.is_function()) {
                        const script_function& func = getMethod.as_function();
                        std::vector<script_value> args = {left, right};
                        push_value(func(args));
                        return;
                    }
                } catch (const std::exception&) {
                    // No custom [] operator, continue with error
                }
            }
            throw runtime_error("Subscript can only be used on arrays, maps, or types with [] operator");
        }
        return;
    }
    
    // Use dispatch table for built-in operators with already-evaluated operands
    auto handler = binary_dispatch_table_.find(expr->op.type);
    if (handler != binary_dispatch_table_.end()) {
        script_value result = (this->*handler->second)(left, right);
        push_value(result);
    } else {
        throw runtime_error("Unknown binary operator");
    }
}
void interpreter::visit_unary_expr(unary_expr* expr) {
    // Fast path for literal unary operations
    if (auto* literal = dynamic_cast<literal_expr*>(expr->operand.get())) {
        const script_value& val = literal->value;
        
        switch (expr->op.type) {
            case token_type::minus:
                if (val.is_int()) {
                    push_value(make_value(-val.as_int()));
                    return;
                } else if (val.is_float()) {
                    push_value(make_value(-val.as_float()));
                    return;
                }
                break;
            case token_type::bang:
                push_value(make_value(!is_truthy(val)));
                return;
            case token_type::tilde:
                if (val.is_int()) {
                    push_value(make_value(~val.as_int()));
                    return;
                }
                break;
            default:
                break; // Fall through to generic path for increment/decrement
        }
    }
    
    // Generic path - evaluate operand and use existing logic
    expr->operand->accept(this);
    script_value operand = pop_value();
    
    switch (expr->op.type) {
        case token_type::minus:
            if (operand.is_int()) {
                push_value(make_value(-operand.as_int()));
            } else if (operand.is_float()) {
                push_value(make_value(-operand.as_float()));
            } else {
                throw runtime_error("Unary minus requires numeric operand");
            }
            break;
            
        case token_type::bang:
            push_value(make_value(!is_truthy(operand)));
            break;
            
        case token_type::tilde:
            // Bitwise NOT
            if (!operand.is_int()) {
                throw runtime_error("Bitwise NOT requires integer operand");
            }
            push_value(make_value(~operand.as_int()));
            break;
            
        case token_type::plus_plus:
        case token_type::minus_minus: {
            // Handle increment/decrement
            if (auto* identifier = dynamic_cast<identifier_expr*>(expr->operand.get())) {
                // Cache symbol ID if not already cached
                if (identifier->symbol_id == UINT64_MAX) {
                    identifier->symbol_id = string_symbolizer_->intern(identifier->name);
                }
                script_value currentValue = environment_->get(identifier->symbol_id);
                script_value newValue = script_value::make_null(engine_ref_);
                
                if (currentValue.is_int()) {
                    int64_t val = currentValue.as_int();
                    if (expr->op.type == token_type::plus_plus) {
                        newValue = make_value(val + 1);
                    } else {
                        newValue = make_value(val - 1);
                    }
                } else if (currentValue.is_float()) {
                    double val = currentValue.as_float();
                    if (expr->op.type == token_type::plus_plus) {
                        newValue = make_value(val + 1.0);
                    } else {
                        newValue = make_value(val - 1.0);
                    }
                } else {
                    throw runtime_error("Cannot increment/decrement non-numeric value");
                }
                
                // Check if this is a reference variable
                script_value* varPtr = environment_->get_value_ptr(identifier->symbol_id);
                if (varPtr && varPtr->is_reference()) {
                    // This is a reference - update the target
                    varPtr->deref() = newValue.deref();
                } else {
                    // Regular variable assignment
                    environment_->assign(identifier->symbol_id, newValue);
                }
                
                // For prefix, return the new value; for postfix, return the old value
                if (expr->is_postfix) {
                    push_value(std::move(currentValue));
                } else {
                    push_value(std::move(newValue));
                }
            } else {
                throw runtime_error("Increment/decrement requires a variable");
            }
            break;
        }
            
        default:
            throw runtime_error("Unsupported unary operator");
    }
}

void interpreter::visit_assignment_expr(assignment_expr* expr) {
    
    // For compound assignment operators, we need the current value
    if (expr->op.type != token_type::equal) {
        // Get current value of the target
        if (auto* identifier = dynamic_cast<identifier_expr*>(expr->target.get())) {
            // Cache symbol ID if not already cached
            if (identifier->symbol_id == UINT64_MAX) {
                identifier->symbol_id = string_symbolizer_->intern(identifier->name);
            }
            script_value currentValue = environment_->get_ref(identifier->symbol_id);
            
            // Evaluate the right-hand side
            expr->value->accept(this);
            script_value rightValue = pop_value();
            
            // Perform the compound operation - try custom operators first, then built-in types
            script_value resultValue = script_value::make_null(engine_ref_);
            bool customOpFound = false;
            
            switch (expr->op.type) {
                case token_type::plus_equal: {
                    // Try custom + operator first
                    if (environment_ && environment_->contains("+")) {
                        try {
                            script_value opFunc = environment_->get("+");
                            if (opFunc.is_function()) {
                                const script_function& func = opFunc.as_function();
                                std::vector<script_value> args = {currentValue, rightValue};
                                resultValue = func(args);
                                customOpFound = true;
                            }
                        } catch (const std::exception&) {
                            // Custom operator failed, try built-in
                        }
                    }
                    
                    // Fall back to built-in operators
                    if (!customOpFound) {
                        if (currentValue.is_int() && rightValue.is_int()) {
                            resultValue = make_value(currentValue.as_int() + rightValue.as_int());
                        } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                            resultValue = make_value(currentValue.as_float() + rightValue.as_float());
                        } else if (currentValue.is_string() && rightValue.is_string()) {
                            resultValue = make_value(currentValue.as_string() + rightValue.as_string());
                        } else {
                            throw runtime_error("Invalid operands for +=");
                        }
                    }
                    break;
                }
                    
                case token_type::minus_equal: {
                    // Try custom - operator first
                    customOpFound = false;
                    if (environment_ && environment_->contains("-")) {
                        try {
                            script_value opFunc = environment_->get("-");
                            if (opFunc.is_function()) {
                                const script_function& func = opFunc.as_function();
                                std::vector<script_value> args = {currentValue, rightValue};
                                resultValue = func(args);
                                customOpFound = true;
                            }
                        } catch (const std::exception&) {
                            // Custom operator failed, try built-in
                        }
                    }
                    
                    // Fall back to built-in operators
                    if (!customOpFound) {
                        if (currentValue.is_int() && rightValue.is_int()) {
                            resultValue = make_value(currentValue.as_int() - rightValue.as_int());
                        } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                            resultValue = make_value(currentValue.as_float() - rightValue.as_float());
                        } else {
                            throw runtime_error("Invalid operands for -=");
                        }
                    }
                    break;
                }
                    
                case token_type::star_equal: {
                    // Try custom * operator first
                    customOpFound = false;
                    if (environment_ && environment_->contains("*")) {
                        try {
                            script_value opFunc = environment_->get("*");
                            if (opFunc.is_function()) {
                                const script_function& func = opFunc.as_function();
                                std::vector<script_value> args = {currentValue, rightValue};
                                resultValue = func(args);
                                customOpFound = true;
                            }
                        } catch (const std::exception&) {
                            // Custom operator failed, try built-in
                        }
                    }
                    
                    // Fall back to built-in operators
                    if (!customOpFound) {
                        if (currentValue.is_int() && rightValue.is_int()) {
                            resultValue = make_value(currentValue.as_int() * rightValue.as_int());
                        } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                            resultValue = make_value(currentValue.as_float() * rightValue.as_float());
                        } else {
                            throw runtime_error("Invalid operands for *=");
                        }
                    }
                    break;
                }
                    
                case token_type::slash_equal:
                    if (rightValue.is_int() && rightValue.as_int() == 0) {
                        throw runtime_error("Division by zero");
                    }
                    if (rightValue.is_float() && rightValue.as_float() == 0.0) {
                        throw runtime_error("Division by zero");
                    }
                    
                    if (currentValue.is_int() && rightValue.is_int()) {
                        resultValue = make_value(currentValue.as_int() / rightValue.as_int());
                    } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                        resultValue = make_value(currentValue.as_float() / rightValue.as_float());
                    } else {
                        throw runtime_error("Invalid operands for /=");
                    }
                    break;
                    
                default:
                    throw runtime_error("Unsupported compound assignment operator");
            }
            
            // Check if this is a reference variable
            script_value* varPtr = environment_->get_value_ptr(identifier->symbol_id);
            if (varPtr && varPtr->is_reference()) {
                // This is a reference - update the target (deep copy)
                varPtr->deref() = std::move(resultValue.deref().clone());
            } else {
                // Regular assignment (deep copy the result)
                environment_->assign(identifier->symbol_id, std::move(resultValue.clone()));
            }
            push_value(resultValue);
        } else if (auto* memberExpr = dynamic_cast<member_expr*>(expr->target.get())) {
            // Handle compound assignment to member expression (e.g., obj.value += 10)
            // First, get the current value of the property
            memberExpr->accept(this);
            script_value currentValue = pop_value().deref();
            
            // Evaluate the right-hand side
            expr->value->accept(this);
            script_value rightValue = pop_value();
            
            // Perform the compound operation
            script_value resultValue = script_value::make_null(engine_ref_);
            bool customOpFound = false;
            
            switch (expr->op.type) {
                case token_type::plus_equal: {
                    if (!customOpFound) {
                        if (currentValue.is_int() && rightValue.is_int()) {
                            resultValue = make_value(currentValue.as_int() + rightValue.as_int());
                        } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                            resultValue = make_value(currentValue.as_float() + rightValue.as_float());
                        } else if (currentValue.is_string() && rightValue.is_string()) {
                            resultValue = make_value(currentValue.as_string() + rightValue.as_string());
                        } else {
                            throw runtime_error("Invalid operands for +=");
                        }
                    }
                    break;
                }
                case token_type::minus_equal: {
                    if (currentValue.is_int() && rightValue.is_int()) {
                        resultValue = make_value(currentValue.as_int() - rightValue.as_int());
                    } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                        resultValue = make_value(currentValue.as_float() - rightValue.as_float());
                    } else {
                        throw runtime_error("Invalid operands for -=");
                    }
                    break;
                }
                case token_type::star_equal: {
                    if (currentValue.is_int() && rightValue.is_int()) {
                        resultValue = make_value(currentValue.as_int() * rightValue.as_int());
                    } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                        resultValue = make_value(currentValue.as_float() * rightValue.as_float());
                    } else {
                        throw runtime_error("Invalid operands for *=");
                    }
                    break;
                }
                case token_type::slash_equal: {
                    if (rightValue.is_int() && rightValue.as_int() == 0) {
                        throw runtime_error("Division by zero");
                    } else if (rightValue.is_float() && rightValue.as_float() == 0.0) {
                        throw runtime_error("Division by zero");
                    }
                    if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                        resultValue = make_value(currentValue.as_float() / rightValue.as_float());
                    } else {
                        throw runtime_error("Invalid operands for /=");
                    }
                    break;
                }
                default:
                    throw runtime_error("Unsupported compound assignment operator");
            }
            
            // Now assign the result back to the property
            // We need to evaluate the object again to get a fresh reference
            memberExpr->object->accept(this);
            script_value objectValue = pop_value();
            
            // Check if it's an object
            if (!objectValue.is_object()) {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to member of non-object type");
                current_exception_ = script_exception("Cannot assign to member of non-object type", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return;
            }
            
            // Extract the class_instance
            auto objHolder = objectValue.get_object_holder();
            if (!objHolder) {
                throw runtime_error("Cannot assign property to non-object value");
            }
            auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
            
            // Check if there's a property setter
            script_value setter = instance->get_method("_set_" + memberExpr->member, false);
            if (!setter.is_null()) {
                // Call the setter with 'this' and the value
                const script_function& func = setter.as_function();
                std::vector<script_value> args = {objectValue, std::move(resultValue.clone())};
                func(args);
            } else if (instance->has_field(memberExpr->member)) {
                // Direct field assignment (deep copy)
                instance->set_field(memberExpr->member, std::move(resultValue.clone()));
            } else {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to non-existent member '" + memberExpr->member + "'");
                current_exception_ = script_exception("Cannot assign to non-existent member '" + memberExpr->member + "'", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return;
            }
            
            push_value(std::move(resultValue));
        } else {
            // General compound assignment for any expression
            // This handles subscripts, function calls that return references, etc.
            
            // First, evaluate the target expression to get current value
            expr->target->accept(this);
            script_value currentValue = pop_value();
            
            // Evaluate the right-hand side
            expr->value->accept(this);
            script_value rightValue = pop_value();
            
            // Perform the compound operation
            script_value resultValue = script_value::make_null(engine_ref_);
            
            // Try custom operators first
            try {
                script_value opFunc = environment_->get(std::string(1, expr->op.lexeme[0]));
                if (opFunc.is_function()) {
                    const script_function& func = opFunc.as_function();
                    std::vector<script_value> args = {currentValue, rightValue};
                    resultValue = func(args);
                } else {
                    throw runtime_error("Not a function");
                }
            } catch (const std::exception&) {
                // Fall back to built-in operators
                switch (expr->op.type) {
                    case token_type::plus_equal:
                        if (currentValue.is_string() || rightValue.is_string()) {
                            resultValue = make_value(currentValue.to_string() + rightValue.to_string());
                        } else {
                            resultValue = evaluate_arithmetic(currentValue, token_type::plus, rightValue);
                        }
                        break;
                    case token_type::minus_equal:
                        resultValue = evaluate_arithmetic(currentValue, token_type::minus, rightValue);
                        break;
                    case token_type::star_equal:
                        resultValue = evaluate_arithmetic(currentValue, token_type::star, rightValue);
                        break;
                    case token_type::slash_equal:
                        if ((rightValue.is_int() && rightValue.as_int() == 0) ||
                            (rightValue.is_float() && rightValue.as_float() == 0.0)) {
                            throw runtime_error("Division by zero");
                        }
                        resultValue = evaluate_arithmetic(currentValue, token_type::slash, rightValue);
                        break;
                    case token_type::percent_equal:
                        if (rightValue.is_int() && rightValue.as_int() == 0) {
                            throw runtime_error("Modulo by zero");
                        }
                        resultValue = evaluate_arithmetic(currentValue, token_type::percent, rightValue);
                        break;
                    default:
                        throw runtime_error("Unknown compound assignment operator");
                }
            }
            
            // Now create a regular assignment and execute it
            auto regularAssignment = std::make_shared<assignment_expr>(
                expr->location,
                expr->target,
                token(token_type::equal, "=", expr->op.location),
                std::make_shared<literal_expr>(expr->location, resultValue)
            );
            regularAssignment->accept(this);
        }
    } else {
        // Regular assignment
        
        expr->value->accept(this);
        // Check if we're unwinding due to an exception in the value expression
        if (is_unwinding_) {
            // Don't try to pop a value that wasn't pushed due to the exception
            return;
        }
        script_value value = pop_value();
        
        
        // Check if target is an identifier
        if (auto* identifier = dynamic_cast<identifier_expr*>(expr->target.get())) {
            // Cache symbol ID if not already cached
            if (identifier->symbol_id == UINT64_MAX) {
                identifier->symbol_id = string_symbolizer_->intern(identifier->name);
            }
            // Get the current value to check if it's a reference
            if (environment_->contains(identifier->symbol_id)) {
                script_value* currentVal = environment_->get_value_ptr(identifier->symbol_id);
                if (currentVal && currentVal->is_reference()) {
                    // This is a reference - assign through it (deep copy the value)
                    currentVal->deref() = std::move(value.deref().clone());
                } else if (currentVal && currentVal->is_cpp_bound()) {
                    // This is a C++ bound value - use assign_through
                    currentVal->assign_through(value);
                } else if (currentVal && currentVal->is_weak_ptr()) {
                    // Special handling for weak_ptr assignment
                    if (value.is_null()) {
                        // Assign null - create empty weak_ptr
                        auto type_info = currentVal->get_type_info();
                        environment_->assign(identifier->symbol_id, script_value::make_empty_weak_ptr(type_info, engine_ref_));
                    } else if (value.is_weak_ptr()) {
                        // Assign another weak_ptr
                        environment_->assign(identifier->symbol_id, std::move(value));
                    } else if (value.type() == script_value_type::jai_object_type) {
                        // Convert object to weak_ptr
                        script_value weak = script_value::make_weak_ptr(value, engine_ref_);
                        environment_->assign(identifier->symbol_id, std::move(weak));
                    } else {
                        auto type_info = value.get_type_info();
                        std::string type_name = type_info ? type_info->type_name : "unknown";
                        throw runtime_error("Cannot assign " + type_name + " to weak_ptr");
                    }
                } else if (currentVal && currentVal->get_type_info() && 
                          currentVal->get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
                    // Special handling for shared_ptr assignment
                    if (value.is_null()) {
                        // Assign null - that's fine
                        environment_->assign(identifier->symbol_id, std::move(value));
                    } else if (value.is_weak_ptr()) {
                        throw runtime_error("Cannot assign weak_ptr to shared_ptr - use weak.lock() instead");
                    } else if (value.type() == script_value_type::jai_object_type) {
                        // Assign object to shared_ptr - just update the value but keep the shared_ptr type info
                        auto type_info = currentVal->get_type_info();
                        value.set_type_info(type_info);
                        environment_->assign(identifier->symbol_id, std::move(value));
                    } else {
                        auto type_info = value.get_type_info();
                        std::string type_name = type_info ? type_info->type_name : "unknown";
                        throw runtime_error("Cannot assign " + type_name + " to shared_ptr");
                    }
                } else {
                    // Regular variable assignment (deep copy the value)
                    environment_->assign(identifier->symbol_id, std::move(value.clone()));
                }
            } else {
                // Variable doesn't exist - check if it's a member of 'this' or a static field
                bool assigned_to_this = false;
                try {
                    script_value this_val = environment_->get("this");
                    if (this_val.is_object()) {
                        auto obj_holder = this_val.get_object_holder();
                        if (obj_holder->is_cpp_class_instance || obj_holder->data) {
                            auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                            
                            // First try instance fields
                            if (instance->has_field(identifier->name)) {
                                instance->set_field(identifier->name, std::move(value.clone()));
                                assigned_to_this = true;
                            } 
                            // Then try static fields
                            else {
                                auto class_def = instance->get_class_definition();
                                if (class_def && class_def->has_static_field(identifier->name)) {
                                    class_def->set_static_field(identifier->name, value);
                                    assigned_to_this = true;
                                }
                            }
                        }
                    }
                } catch (...) {
                    // No 'this' in scope
                }
                
                if (!assigned_to_this) {
                    throw runtime_error("Undefined variable '" + identifier->name + "'");
                }
            }
            push_value(std::move(value));  // Assignment expressions return the assigned value
        } 
        // Check if target is a member expression (property assignment)
        else if (auto* memberExpr = dynamic_cast<member_expr*>(expr->target.get())) {
            // Check if this is a static member assignment
            if (memberExpr->is_static) {
                // For static assignment, get the class definition
                auto* ident_expr = dynamic_cast<identifier_expr*>(memberExpr->object.get());
                if (!ident_expr) {
                    throw runtime_error("Static member assignment requires a class name");
                }
                
                std::string class_name = ident_expr->name;
                script_value class_var = script_value::make_null(engine_ref_);
                
                try {
                    class_var = environment_->get("__class_" + class_name);
                } catch (const runtime_error&) {
                    throw runtime_error("Class '" + class_name + "' not found");
                }
                
                if (!class_var.is_object()) {
                    throw runtime_error("'" + class_name + "' is not a class");
                }
                
                auto objHolder = class_var.get_object_holder();
                if (!objHolder || objHolder->type_name != "class_definition") {
                    throw runtime_error("'" + class_name + "' is not a valid class");
                }
                
                auto class_def = std::static_pointer_cast<class_definition>(objHolder->data);
                
                // Evaluate the value
                expr->value->accept(this);
                script_value value = pop_value();
                
                // Set the static field
                try {
                    class_def->set_static_field(memberExpr->member, value.clone());
                } catch (const runtime_error& e) {
                    throw runtime_error("Cannot assign to static member: " + std::string(e.what()));
                }
                
                push_value(value);
                return;
            }
            
            // Regular member assignment - evaluate the object
            memberExpr->object->accept(this);
            script_value objectValue = pop_value();
            
            // Check if it's an object
            if (!objectValue.is_object()) {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to member of non-object type");
                current_exception_ = script_exception("Cannot assign to member of non-object type", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return;
            }
            
            // Extract the class_instance
            auto objHolder = objectValue.get_object_holder();
            if (!objHolder) {
                throw runtime_error("Cannot assign property to non-object value");
            }
            auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
            
            // Check if there's a property setter first (for C++ properties)
            script_value setter = instance->get_method("_set_" + memberExpr->member, false);
            if (!setter.is_null()) {
                // Call the setter with 'this' and the value
                const script_function& func = setter.as_function();
                std::vector<script_value> args = {objectValue, std::move(value.clone())};
                func(args);
            } else if (instance->has_field(memberExpr->member)) {
                // Direct field assignment (deep copy)
                instance->set_field(memberExpr->member, std::move(value.clone()));
            } else {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to non-existent member '" + memberExpr->member + "'");
                current_exception_ = script_exception("Cannot assign to non-existent member '" + memberExpr->member + "'", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return;
            }
            
            push_value(std::move(value));  // Assignment expressions return the assigned value
        }
        // Check if target is a subscript expression (array[index] or map[key])
        else if (auto* binaryExpr = dynamic_cast<binary_expr*>(expr->target.get())) {
            if (binaryExpr->op.type == token_type::left_bracket) {
                // Evaluate the entire target expression (e.g., nested["nums"][1])
                // This should return a reference if it's a valid lvalue
                expr->target->accept(this);
                script_value target_ref = pop_value();
                
                // Check if we got a reference
                if (target_ref.is_reference()) {
                    // Get the actual target through the reference
                    auto refHolder = target_ref.get_reference_holder();
                    script_value* target_ptr = refHolder->target;
                    if (!target_ptr) {
                        throw runtime_error("Invalid reference in assignment");
                    }
                    
                    // Assign the value  
                    *target_ptr = std::move(value.clone());
                    push_value(std::move(value));  // Assignment expressions return the assigned value
                } else {
                    // Not a reference - this means the subscript expression didn't
                    // return an lvalue (e.g., trying to assign to a function call result)
                    throw runtime_error("Cannot assign to rvalue expression");
                }
            } else {
                throw runtime_error("Complex assignment targets not yet implemented");
            }
        } else {
            throw runtime_error("Complex assignment targets not yet implemented");
        }
    }
}

// statement visitors
void interpreter::visit_expression_stmt(expression_stmt* stmt) {
    stmt->expression->accept(this);

    // Early exit if exception is propagating
    if (is_unwinding_) return;
    
    // For top-level expressions (wrapped in expression_decl), we want to keep the value
    // The execute() method will handle popping it
}

void interpreter::visit_block_stmt(block_stmt* stmt) {
    // Create new environment for the block scope
    auto previous = environment_;
    environment_ = std::make_shared<environment>(environment_, string_symbolizer_);

    try {
        for (const auto& decl : stmt->declarations) {
            decl->accept(this);

            // Early exit if exception is propagating
            if (is_unwinding_) break;
        }
    } catch (...) {
        // Restore environment even if an error occurs
        environment_ = previous;
        throw;
    }

    // Restore previous environment
    environment_ = previous;
}

void interpreter::visit_variable_decl(variable_decl* decl) {
    // Check if this is a reference variable declaration
    bool is_reference = false;
    if (decl->type && decl->type->base_type == script_value_type::jai_reference_type) {
        is_reference = true;
    }
    
    // Check if this is a weak_ptr declaration
    bool is_weak_ptr = false;
    if (decl->type && decl->type->base_type == script_value_type::jai_weak_ptr_type) {
        is_weak_ptr = true;
    }
    
    // Check if this is a shared_ptr declaration
    bool is_shared_ptr = false;
    if (decl->type && decl->type->base_type == script_value_type::jai_shared_ptr_type) {
        is_shared_ptr = true;
    }
    
    if (is_weak_ptr) {
        // weak_ptr<T> variable - handle initialization
        if (!decl->initializer) {
            // No initializer - create empty weak_ptr
            script_value weak = script_value::make_empty_weak_ptr(decl->type, engine_ref_);
            environment_->define(decl->name, std::move(weak));
        } else {
            // Evaluate initializer
            decl->initializer->accept(this);
            script_value value = pop_value();
            
            // Handle different initialization cases
            if (value.is_null()) {
                // Initialize with null - create empty weak_ptr
                script_value weak = script_value::make_empty_weak_ptr(decl->type, engine_ref_);
                environment_->define(decl->name, std::move(weak));
            } else if (value.is_weak_ptr()) {
                // Initialize with another weak_ptr - copy it
                environment_->define(decl->name, std::move(value));
            } else if (value.type() == script_value_type::jai_object_type) {
                // Initialize with object/shared_ptr - create weak_ptr from it
                script_value weak = script_value::make_weak_ptr(value, engine_ref_);
                environment_->define(decl->name, std::move(weak));
            } else {
                auto type_info = value.get_type_info();
                std::string type_name = type_info ? type_info->type_name : "unknown";
                throw runtime_error("Cannot initialize weak_ptr with " + type_name);
            }
        }
    } else if (is_shared_ptr) {
        // shared_ptr<T> variable - handle initialization
        if (!decl->initializer) {
            // No initializer - create null shared_ptr
            script_value null_ptr = script_value::make_null(engine_ref_);
            null_ptr.set_type_info(decl->type);  // Mark as shared_ptr type
            environment_->define(decl->name, std::move(null_ptr));
        } else {
            // Evaluate initializer
            decl->initializer->accept(this);
            script_value value = pop_value();
            
            // Handle different initialization cases
            if (value.is_null()) {
                // Initialize with null - that's fine
                value.set_type_info(decl->type);  // Mark as shared_ptr type
                environment_->define(decl->name, std::move(value));
            } else if (value.is_weak_ptr()) {
                throw runtime_error("Cannot initialize shared_ptr directly from weak_ptr - use weak.lock() instead");
            } else if (value.type() == script_value_type::jai_object_type) {
                // Initialize with object/shared_ptr - that's fine, objects are already shared_ptr
                // Mark the type as shared_ptr to ensure reference semantics
                value.set_type_info(decl->type);
                environment_->define(decl->name, std::move(value));
            } else {
                auto type_info = value.get_type_info();
                std::string type_name = type_info ? type_info->type_name : "unknown";
                throw runtime_error("Cannot initialize shared_ptr with " + type_name);
            }
        }
    } else if (is_reference) {
        // Reference variable - must have initializer
        if (!decl->initializer) {
            throw runtime_error("Reference variable '" + decl->name + "' must be initialized");
        }
        
        // Check if initializer is an identifier (can take reference)
        if (auto identExpr = dynamic_cast<identifier_expr*>(decl->initializer.get())) {
            // Get the target variable's address
            uint64_t targetSymbolId = string_symbolizer_->intern(identExpr->name);
            
            // Get a pointer to the target value in the environment
            // This is safe because environment uses unordered_map which doesn't invalidate pointers
            script_value* targetPtr = environment_->get_value_ptr(targetSymbolId);
            if (!targetPtr) {
                throw runtime_error("Cannot take reference of undefined variable '" + identExpr->name + "'");
            }
            
            // Check if the target is itself a reference
            if (targetPtr->is_reference()) {
                // Reference to reference - get the final target and its environment
                auto refHolder = targetPtr->get_reference_holder();
                targetPtr = refHolder->target;
                // Use the original reference's environment
                auto target_env = refHolder->sourceEnv.lock();
                if (!target_env) {
                    throw runtime_error("Reference target environment has been destroyed");
                }
                script_value refValue = script_value::make_reference(targetPtr, target_env);
                environment_->define(decl->name, std::move(refValue));
            } else {
                // Regular reference - use current environment
                script_value refValue = script_value::make_reference(targetPtr, environment_);
                environment_->define(decl->name, std::move(refValue));
            }
        } else {
            // For other expressions, evaluate them and check if they return a reference
            decl->initializer->accept(this);
            script_value result = pop_value();
            
            // If the result is a reference, we can create a reference to its target
            if (result.is_reference()) {
                auto refHolder = result.get_reference_holder();
                script_value* targetPtr = refHolder->target;
                auto target_env = refHolder->sourceEnv.lock();
                if (!target_env) {
                    throw runtime_error("Reference target environment has been destroyed");
                }
                // Create a new reference to the same target
                script_value refValue = script_value::make_reference(targetPtr, target_env);
                environment_->define(decl->name, std::move(refValue));
            } else {
                throw runtime_error("Cannot take reference of non-lvalue expression");
            }
        }
    } else {
        // Regular variable declaration
        script_value value = script_value::make_null(engine_ref_);
        if (decl->initializer) {
            decl->initializer->accept(this);
            value = pop_value();
            // Clone the value for variable declaration with initializer
            // Note: clone() now automatically dereferences references
            value = value.clone();
        }
        // If no initializer, value remains null
        
        environment_->define(decl->name, std::move(value));
    }
}

// Binary operation helpers
script_value interpreter::evaluate_arithmetic(const script_value& left, token_type op, const script_value& right) {
    // Special case for string concatenation
    if (op == token_type::plus && (left.is_string() || right.is_string())) {
        return make_value(left.to_string() + right.to_string());
    }
    
    // Fast path for pure integer arithmetic (avoid float conversion)
    if (left.is_int() && right.is_int()) {
        script_int leftInt = left.as_int();
        script_int rightInt = right.as_int();
        
        switch (op) {
            case token_type::plus:
                return make_value(leftInt + rightInt);
            case token_type::minus:
                return make_value(leftInt - rightInt);
            case token_type::star:
                return make_value(leftInt * rightInt);
            case token_type::slash:
                if (rightInt == 0) {
                    throw runtime_error("Division by zero");
                }
                // Integer division returns integer (C++ semantics)
                return make_value(leftInt / rightInt);
            case token_type::percent:
                if (rightInt == 0) {
                    throw runtime_error("Division by zero");
                }
                return make_value(leftInt % rightInt);
            default:
                throw runtime_error("Unknown arithmetic operator");
        }
    }
    
    // Mixed or floating point arithmetic path
    script_float leftNum, rightNum;
    
    if (left.is_int()) {
        leftNum = static_cast<script_float>(left.as_int());
    } else if (left.is_float()) {
        leftNum = left.as_float();
    } else {
        throw runtime_error("Left operand must be numeric");
    }
    
    if (right.is_int()) {
        rightNum = static_cast<script_float>(right.as_int());
    } else if (right.is_float()) {
        rightNum = right.as_float();
    } else {
        throw runtime_error("Right operand must be numeric");
    }
    
    switch (op) {
        case token_type::plus:
            return make_value(leftNum + rightNum);
        case token_type::minus:
            return make_value(leftNum - rightNum);
        case token_type::star:
            return make_value(leftNum * rightNum);
        case token_type::slash:
            if (rightNum == 0.0) {
                throw runtime_error("Division by zero");
            }
            return make_value(leftNum / rightNum);
        case token_type::percent:
            if (rightNum == 0.0) {
                throw runtime_error("Division by zero");
            }
            return make_value(std::fmod(leftNum, rightNum));
        default:
            throw runtime_error("Unknown arithmetic operator");
    }
}

script_value interpreter::evaluate_comparison(const script_value& left, token_type op, const script_value& right) {
    // Handle weak_ptr comparisons with null
    if ((left.is_weak_ptr() && right.is_null()) || (left.is_null() && right.is_weak_ptr())) {
        if (op == token_type::equal_equal || op == token_type::bang_equal) {
            // For weak_ptr, null comparison checks if expired
            bool is_expired = false;
            if (left.is_weak_ptr()) {
                if (left.is_weak_ptr()) {
                    auto weak_ptr = left.get_weak_ptr();
                    // Check if weak_ptr is expired (includes default-constructed)
                    is_expired = weak_ptr.expired();
                } else if (left.get_object_holder() != nullptr) {
                    // weak_ptr_holder type - check if it contains an actual value
                    auto holder = left.get_object_holder();
                    is_expired = (holder->type_name == "weak_ptr_holder" && !holder->data);
                } else {
                    // Other cases - consider expired
                    is_expired = true;
                }
            } else {
                // right is weak_ptr
                if (right.is_weak_ptr()) {
                    auto weak_ptr = right.get_weak_ptr();
                    // Check if weak_ptr is expired (includes default-constructed)
                    is_expired = weak_ptr.expired();
                } else if (right.get_object_holder() != nullptr) {
                    // weak_ptr_holder type - check if it contains an actual value
                    auto holder = right.get_object_holder();
                    is_expired = (holder->type_name == "weak_ptr_holder" && !holder->data);
                } else {
                    // Other cases - consider expired
                    is_expired = true;
                }
            }
            
            if (op == token_type::equal_equal) {
                return make_value(is_expired);  // weak == null is true if expired
            } else {
                return make_value(!is_expired); // weak != null is true if not expired
            }
        }
    }
    
    // Handle null comparisons
    if (left.is_null() || right.is_null()) {
        switch (op) {
            case token_type::equal_equal:
                return make_value(left.is_null() && right.is_null());
            case token_type::bang_equal:
                return make_value(!(left.is_null() && right.is_null()));
            default:
                throw runtime_error("Cannot compare null values with relational operators");
        }
    }
    
    // For now, only support numeric and string comparisons
    if (left.is_string() && right.is_string()) {
        const auto& leftStr = left.as_string();
        const auto& rightStr = right.as_string();
        
        switch (op) {
            case token_type::less:
                return make_value(leftStr < rightStr);
            case token_type::less_equal:
                return make_value(leftStr <= rightStr);
            case token_type::greater:
                return make_value(leftStr > rightStr);
            case token_type::greater_equal:
                return make_value(leftStr >= rightStr);
            case token_type::equal_equal:
                return make_value(leftStr == rightStr);
            case token_type::bang_equal:
                return make_value(leftStr != rightStr);
            case token_type::spaceship: {
                // Three-way comparison for strings
                int cmp = leftStr.compare(rightStr);
                return make_value(cmp < 0 ? script_int(-1) : (cmp > 0 ? script_int(1) : script_int(0)));
            }
            default:
                throw runtime_error("Unknown comparison operator");
        }
    }
    
    // Numeric comparison
    script_float leftNum = to_numeric(left).as_float();
    script_float rightNum = to_numeric(right).as_float();
    
    switch (op) {
        case token_type::less:
            return make_value(leftNum < rightNum);
        case token_type::less_equal:
            return make_value(leftNum <= rightNum);
        case token_type::greater:
            return make_value(leftNum > rightNum);
        case token_type::greater_equal:
            return make_value(leftNum >= rightNum);
        case token_type::equal_equal:
            return make_value(leftNum == rightNum);
        case token_type::bang_equal:
            return make_value(leftNum != rightNum);
        case token_type::spaceship: {
            // Three-way comparison for numbers
            // Return -1 if less, 0 if equal, 1 if greater
            if (leftNum < rightNum) return make_value(script_int(-1));
            else if (leftNum > rightNum) return make_value(script_int(1));
            else return make_value(script_int(0));
        }
        default:
            throw runtime_error("Unknown comparison operator");
    }
}

script_value interpreter::evaluate_logical(const script_value& left, token_type op, const script_value& right) {
    bool leftTruthy = is_truthy(left);
    
    switch (op) {
        case token_type::ampersand_ampersand:
            // Short-circuit: if left is false, return left
            if (!leftTruthy) {
                return left;
            }
            return right;
            
        case token_type::pipe_pipe:
            // Short-circuit: if left is true, return left
            if (leftTruthy) {
                return left;
            }
            return right;
            
        default:
            throw runtime_error("Unknown logical operator");
    }
}

script_value interpreter::evaluate_bitwise(const script_value& left, token_type op, const script_value& right) {
    // Bitwise operations only work on integers
    if (!left.is_int() || !right.is_int()) {
        throw runtime_error("Bitwise operations require integer operands");
    }
    
    script_int leftInt = left.as_int();
    script_int rightInt = right.as_int();
    
    switch (op) {
        case token_type::ampersand:
            return make_value(leftInt & rightInt);
        case token_type::pipe:
            return make_value(leftInt | rightInt);
        case token_type::caret:
            return make_value(leftInt ^ rightInt);
        case token_type::left_shift:
            return make_value(leftInt << rightInt);
        case token_type::right_shift:
            return make_value(leftInt >> rightInt);
        default:
            throw runtime_error("Unknown bitwise operator");
    }
}


// Placeholder implementations for remaining visitors
void interpreter::visit_call_expr(call_expr* expr) {
    // Evaluate the callee expression
    expr->callee->accept(this);
    script_value callee = pop_value();
    
    // Check if the callee is a function
    if (!callee.is_function()) {
        throw runtime_error("Cannot call non-function value");
    }
    
    // Use a local vector for arguments to avoid issues with nested calls
    std::vector<script_value> arguments;
    arguments.reserve(expr->arguments.size());
    
    // Also track argument metadata for reference parameters
    std::vector<std::pair<uint64_t, std::shared_ptr<environment>>> argMetadata;
    argMetadata.reserve(expr->arguments.size());
    
    for (const auto& argExpr : expr->arguments) {
        // Check if this is a simple identifier (needed for references)
        if (auto identExpr = dynamic_cast<identifier_expr*>(argExpr.get())) {
            // Get the symbol ID for this variable
            uint64_t symbol_id = string_symbolizer_->intern(identExpr->name);
            argMetadata.emplace_back(symbol_id, environment_);
        } else {
            // Not an identifier - can't take reference
            argMetadata.emplace_back(UINT64_MAX, nullptr);
        }
        
        // Evaluate argument with exception handling
        try {
            argExpr->accept(this);
            arguments.emplace_back(std::move(pop_value()));
        } catch (const script_exception& e) {
            // Convert to interpreter exception state
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = e;
            is_unwinding_ = true;
            push_value(make_value());  // Push null for the failed call
            return;
        } catch (const std::runtime_error& e) {
            // Convert runtime errors to script exceptions
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = script_exception(e.what());
            is_unwinding_ = true;
            push_value(make_value());  // Push null for the failed call
            return;
        }
    }
    
    // Store argument metadata in a member variable so call_function can access it
    current_arg_metadata_ = std::move(argMetadata);
    
    // Call the function with C++ exception handling
    const script_function& func = callee.as_function();
    script_value result = script_value::make_null(engine_ref_);
    
    try {
        result = func(arguments);
    } catch (const script_exception& e) {
        // Convert script exceptions to interpreter exception state
        current_arg_metadata_.clear();
        active_exception_value_ = make_value(std::string(e.what()));
        current_exception_ = e;
        is_unwinding_ = true;
        push_value(make_value());  // Push a null value since the call failed
        return;
    } catch (const std::runtime_error& e) {
        // Wrap C++ runtime_error with message and trigger exception handling
        current_arg_metadata_.clear();
        active_exception_value_ = make_value(std::string(e.what()));
        current_exception_ = script_exception(e.what());
        is_unwinding_ = true;
        push_value(make_value());  // Push a null value since the call failed
        return;
    } catch (const std::exception& e) {
        // Other C++ exceptions use their what() message
        current_arg_metadata_.clear();
        active_exception_value_ = make_value(std::string(e.what()));
        current_exception_ = script_exception(e.what());
        is_unwinding_ = true;
        push_value(make_value());  // Push a null value since the call failed
        return;
    }
    
    // Clear argument metadata
    current_arg_metadata_.clear();
    
    // Push result onto the stack
    push_value(result);
}

void interpreter::visit_member_expr(member_expr* expr) {
    // Check if this is a static member access (::)
    if (expr->is_static) {
        // For static access, the object should be a class identifier
        auto* ident_expr = dynamic_cast<identifier_expr*>(expr->object.get());
        if (!ident_expr) {
            throw runtime_error("Static member access requires a class name");
        }
        
        // Look up the class definition
        std::string class_name = ident_expr->name;
        script_value class_var = script_value::make_null(engine_ref_);
        
        try {
            // Try to find the class definition
            class_var = environment_->get("__class_" + class_name);
        } catch (const runtime_error&) {
            throw runtime_error("Class '" + class_name + "' not found");
        }
        
        if (!class_var.is_object()) {
            throw runtime_error("'" + class_name + "' is not a class");
        }
        
        // Extract the class definition
        auto objHolder = class_var.get_object_holder();
        if (!objHolder || objHolder->type_name != "class_definition") {
            throw runtime_error("'" + class_name + "' is not a valid class");
        }
        
        auto class_def = std::static_pointer_cast<class_definition>(objHolder->data);
        
        // Try static field first
        try {
            script_value static_value = class_def->get_static_field(expr->member);
            push_value(static_value);
            return;
        } catch (const runtime_error&) {
            // Field not found, try static method
        }
        
        // Try static method
        try {
            script_value static_method = class_def->get_static_method(expr->member);
            push_value(static_method);
            return;
        } catch (const runtime_error&) {
            // Method not found either
        }
        
        throw runtime_error("Class '" + class_name + "' has no static member '" + expr->member + "'");
        return;
    }
    
    // Check if this is a super:: member access
    bool is_super_access = dynamic_cast<super_expr*>(expr->object.get()) != nullptr;
    
    // Evaluate the object expression
    expr->object->accept(this);
    script_value objectValue = pop_value();
    
    // Dereference if needed - subscript access returns references
    objectValue = objectValue.deref();
    
    // Handle super:: member access specially
    if (is_super_access) {
        // objectValue is 'this' from visit_super_expr
        if (!objectValue.is_object()) {
            throw runtime_error("super:: used on non-object");
        }
        
        // Get the class instance
        auto objHolder = objectValue.get_object_holder();
        if (!objHolder || !objHolder->data) {
            throw runtime_error("super:: used on non-class object");
        }
        
        // Both script and C++ classes store class_instance in data
        auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
        if (!instance) {
            throw runtime_error("super:: used on non-class object");
        }
        
        // Get the class definition and its parent
        auto class_def = instance->get_class_definition();
        if (!class_def) {
            throw runtime_error("Class definition not found for super:: access");
        }
        
        auto parent_def = class_def->get_parent();
        if (!parent_def) {
            throw runtime_error("super:: used in class with no parent");
        }
        
        // Look for the method in the parent class
        script_value method = parent_def->get_method(expr->member);
        if (method.is_null()) {
            throw runtime_error("Parent class has no method '" + expr->member + "'");
        }
        
        // Return a bound method that calls the parent's implementation
        push_value(create_bound_method(objectValue, method));
        return;
    }
    
    // Handle array methods
    if (objectValue.is_array()) {
        auto methodIt = arrayMethods_.find(expr->member);
        if (methodIt != arrayMethods_.end()) {
            // Found the method in the registry
            const builtin_method& method = methodIt->second;
            
            // Create a wrapper function that captures the array value by moving it
            script_function boundMethod = [this, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> script_value {
                return method(this, capturedValue, args);
            };
            
            push_value(script_value::make_function(boundMethod, engine_ref_));
            return;
        }
        else {
            // Set exception state instead of throwing
            active_exception_value_ = make_value("Array has no method '" + expr->member + "'");
            current_exception_ = script_exception("Array has no method '" + expr->member + "'", expr->location);
            is_unwinding_ = true;
            push_value(make_value());
            return;
        }
    }
    
    // Handle map methods
    if (objectValue.is_map()) {
        auto methodIt = mapMethods_.find(expr->member);
        if (methodIt != mapMethods_.end()) {
            // Found the method in the registry
            const builtin_method& method = methodIt->second;
            
            // Create a wrapper function that captures the map value by moving it
            script_function boundMethod = [this, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> script_value {
                return method(this, capturedValue, args);
            };
            
            push_value(script_value::make_function(boundMethod, engine_ref_));
            return;
        }
        else {
            throw runtime_error("Map has no method '" + expr->member + "'");
        }
    }
    
    // Handle weak_ptr methods
    if (objectValue.is_weak_ptr()) {
        auto methodIt = weakPtrMethods_.find(expr->member);
        if (methodIt != weakPtrMethods_.end()) {
            // Found the method in the registry
            const builtin_method& method = methodIt->second;
            
            // Create a wrapper function that captures the weak_ptr value by moving it
            script_function boundMethod = [this, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> script_value {
                return method(this, capturedValue, args);
            };
            
            push_value(script_value::make_function(boundMethod, engine_ref_));
            return;
        }
        else {
            throw runtime_error("weak_ptr has no method '" + expr->member + "'");
        }
    }
    
    // Handle shared_ptr methods (all objects in JaiScript are internally shared_ptr)
    if (objectValue.is_object()) {
        // Check if this object has shared_ptr type info
        auto type_info = objectValue.get_type_info();
        if (type_info && type_info->type_name.find("shared_ptr<") == 0) {
            auto methodIt = sharedPtrMethods_.find(expr->member);
            if (methodIt != sharedPtrMethods_.end()) {
                // Found the method in the registry
                const builtin_method& method = methodIt->second;
                
                // Create a wrapper function that captures the shared_ptr value by moving it
                script_function boundMethod = [this, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> script_value {
                    return method(this, capturedValue, args);
                };
                
                push_value(script_value::make_function(boundMethod, engine_ref_));
                return;
            }
            // If method not found, fall through to regular object member access
        }
    }
    
    // Check if it's an object
    if (!objectValue.is_object()) {
        throw runtime_error("Cannot access member '" + expr->member + "' on non-object type");
    }
    
    // Extract the class_instance from the object
    // Access the object_holder directly since we're a friend class
    auto objHolder = objectValue.get_object_holder();
    
    
    // Get the class_instance - could be script or C++ class
    std::shared_ptr<class_instance> instance;
    
    // For C++ classes, is_cpp_class_instance is true and data is class_instance
    if (objHolder->is_cpp_class_instance) {
        instance = std::static_pointer_cast<class_instance>(objHolder->data);
    } else {
        // For script classes, the data IS a class_instance directly
        // Try to cast it (will fail if it's not a class_instance)
        instance = std::static_pointer_cast<class_instance>(objHolder->data);
    }
    
    if (!instance) {
        throw runtime_error("Cannot access member '" + expr->member + "' on non-class object");
    }
    
    // First check if it's a field (registered by the property() method)
    bool has_field_result = instance->has_field(expr->member);
    if (has_field_result) {
        // For script classes, always access fields directly
        // For C++ classes, check if there's a property getter method
        if (!instance->is_script_class()) {
            try {
                script_value getter = instance->get_method("_get_" + expr->member);
                if (!getter.is_null()) {
                    // Call the getter with 'this' as argument
                    const script_function& func = getter.as_function();
                    std::vector<script_value> args = {objectValue};
                    push_value(func(args));
                    return;
                }
            } catch (const std::exception&) {
                // If get_method fails (e.g., class definition expired), fall back to direct field access
            }
        }
        // Return the field value directly (for script classes or if no getter)
        push_value(instance->get_field(expr->member));
        return;
    }
    
    // Otherwise, look for a method (pass false to avoid throwing)
    script_value method = instance->get_method(expr->member, false);
    if (!method.is_null() && !method.is_invalid()) {
        // Return a bound method (function that has 'this' pre-bound)
        // We'll create a wrapper function that includes the object as first argument
        push_value(create_bound_method(objectValue, method));
        return;
    }
    
    // Set exception state instead of throwing
    active_exception_value_ = make_value("Object has no member '" + expr->member + "'");
    current_exception_ = script_exception("Object has no member '" + expr->member + "'", expr->location);
    is_unwinding_ = true;
    push_value(make_value());  // Push null for failed member access
}

void interpreter::visit_lambda_expr(lambda_expr* expr) {
    
    // Capture current environment for closure
    auto closure_env = environment_;
    
    // Check if we need a capture environment
    bool has_explicit_captures = !expr->captures.empty();
    bool has_default_capture = (expr->default_capture != lambda_expr::capture_default::none);
    
    
    // For default captures, analyze the lambda body to find which variables are actually used
    std::unordered_set<std::string> used_variables;
    if (has_default_capture) {
        // Helper to recursively find all identifiers in an expression
        std::function<void(expression*)> find_identifiers;
        find_identifiers = [&](expression* e) {
            if (auto* ident = dynamic_cast<identifier_expr*>(e)) {
                // Skip parameter names
                bool is_param = false;
                for (const auto& param : expr->parameters) {
                    if (param.name == ident->name) {
                        is_param = true;
                        break;
                    }
                }
                if (!is_param) {
                    used_variables.insert(ident->name);
                }
            } else if (auto* binary = dynamic_cast<binary_expr*>(e)) {
                find_identifiers(binary->left.get());
                find_identifiers(binary->right.get());
            } else if (auto* unary = dynamic_cast<unary_expr*>(e)) {
                find_identifiers(unary->operand.get());
            } else if (auto* call = dynamic_cast<call_expr*>(e)) {
                find_identifiers(call->callee.get());
                for (const auto& arg : call->arguments) {
                    find_identifiers(arg.get());
                }
            } else if (auto* member = dynamic_cast<member_expr*>(e)) {
                find_identifiers(member->object.get());
            } else if (auto* assign = dynamic_cast<assignment_expr*>(e)) {
                find_identifiers(assign->target.get());
                find_identifiers(assign->value.get());
            } else if (auto* ternary = dynamic_cast<ternary_expr*>(e)) {
                find_identifiers(ternary->condition.get());
                find_identifiers(ternary->then_expression.get());
                find_identifiers(ternary->else_expression.get());
            }
            // Add more expression types as needed
        };
        
        // Helper to find identifiers in statements
        std::function<void(statement*)> find_in_statement;
        find_in_statement = [&](statement* s) {
            if (auto* expr_stmt = dynamic_cast<expression_stmt*>(s)) {
                find_identifiers(expr_stmt->expression.get());
            } else if (auto* block = dynamic_cast<block_stmt*>(s)) {
                for (const auto& decl : block->declarations) {
                    if (auto* expr_decl = dynamic_cast<expression_decl*>(decl.get())) {
                        find_identifiers(expr_decl->expression.get());
                    } else if (auto* stmt_decl = dynamic_cast<statement_decl*>(decl.get())) {
                        find_in_statement(stmt_decl->statement.get());
                    }
                }
            } else if (auto* if_s = dynamic_cast<if_stmt*>(s)) {
                find_identifiers(if_s->condition.get());
                find_in_statement(if_s->then_statement.get());
                if (if_s->else_statement) {
                    find_in_statement(if_s->else_statement.get());
                }
            } else if (auto* while_s = dynamic_cast<while_stmt*>(s)) {
                find_identifiers(while_s->condition.get());
                find_in_statement(while_s->body.get());
            } else if (auto* return_s = dynamic_cast<return_stmt*>(s)) {
                if (return_s->value) {
                    find_identifiers(return_s->value.get());
                }
            }
            // Add more statement types as needed
        };
        
        // Analyze the lambda body
        find_in_statement(expr->body.get());
        
    }
    
    // Determine if we actually need a capture environment
    bool needs_capture_env = has_explicit_captures || (has_default_capture && !used_variables.empty());
    
    
    std::shared_ptr<environment> final_closure_env;
    
    if (needs_capture_env) {
        // Create captured variables in the closure environment
        std::shared_ptr<environment> captureEnv = std::make_shared<environment>(closure_env, string_symbolizer_);
        
        // Process default captures first ([=] or [&])
        if (has_default_capture && !used_variables.empty()) {
            bool capture_by_ref = (expr->default_capture == lambda_expr::capture_default::by_reference);
            
            for (const auto& varName : used_variables) {
                // Check if this variable is explicitly overridden in the capture list
                bool is_overridden = false;
                for (const auto& capture : expr->captures) {
                    if (capture.name == varName) {
                        is_overridden = true;
                        break;
                    }
                }
                
                if (!is_overridden && environment_->contains(varName)) {
                    if (capture_by_ref) {
                        // Capture by reference - create reference to original variable
                        script_value* targetPtr = environment_->get_value_ptr(string_symbolizer_->intern(varName));
                        if (targetPtr) {
                            script_value refValue = script_value::make_reference(targetPtr, environment_);
                            captureEnv->define(varName, std::move(refValue));
                        }
                    } else {
                        // Capture by value - deep copy at capture time
                        script_value capturedValue = environment_->get(varName);
                        captureEnv->define(varName, capturedValue.clone());
                    }
                }
            }
        }
        
        // Process explicit captures
        for (const auto& capture : expr->captures) {
            if (environment_->contains(capture.name)) {
                if (capture.by_reference) {
                    // Capture by reference - create reference to original variable
                    uint64_t symbolId = string_symbolizer_->intern(capture.name);
                    script_value* targetPtr = environment_->get_value_ptr(symbolId);
                    if (targetPtr) {
                        script_value refValue = script_value::make_reference(targetPtr, environment_);
                        captureEnv->define(capture.name, std::move(refValue));
                    } else {
                        throw runtime_error("Cannot capture variable by reference: " + capture.name);
                    }
                } else {
                    // Capture by value - deep copy at capture time
                    script_value capturedValue = environment_->get(capture.name);
                    captureEnv->define(capture.name, capturedValue.clone());
                }
            } else {
                throw runtime_error("Cannot capture undefined variable: " + capture.name);
            }
        }
        
        final_closure_env = captureEnv;
    } else {
        // No captures needed - use current environment directly (fast path)
        final_closure_env = closure_env;
        
    }
    
    // Convert the lambda body to a block_stmt if it's not already
    std::shared_ptr<block_stmt> lambdaBody;
    if (auto blockStmt = std::dynamic_pointer_cast<block_stmt>(expr->body)) {
        lambdaBody = blockStmt;
    } else {
        // Wrap single statement in a block
        std::vector<declaration_ptr> stmts;
        if (auto stmt = std::dynamic_pointer_cast<statement>(expr->body)) {
            auto stmtDecl = std::make_shared<statement_decl>(expr->location, stmt);
            stmts.push_back(stmtDecl);
        }
        lambdaBody = std::make_shared<block_stmt>(expr->location, std::move(stmts));
    }
    
    // Pre-cache parameter symbol IDs for optimization
    for (auto& param : expr->parameters) {
        if (param.symbol_id == UINT64_MAX) {
            param.symbol_id = string_symbolizer_->intern(param.name);
        }
    }
    
    // Create the script function
    // Use final_closure_env which is either the capture environment or current environment
    // This ensures lambdas can access variables from their creation context
    // IMPORTANT: If needs_capture_env is false, we pass nullptr as closure_env
    // This makes the lambda behave exactly like a regular function
    
    
    auto lambdaFunc = std::make_shared<script_defined_function>(
        "<lambda>",  // Anonymous function name
        expr->parameters,
        expr->return_type,
        lambdaBody,
        needs_capture_env ? final_closure_env : nullptr  // Only use closure env if we have captures
    );
    
    // Create a script_function wrapper
    // capture lambdaFunc by value to ensure it stays alive
    script_function funcWrapper = [this, lambdaFunc](const std::vector<script_value>& args) -> script_value {
        return call_function(*lambdaFunc, args);
    };
    
    // Push the lambda as a function value
    push_value(script_value::make_function(funcWrapper, engine_ref_));
}

void interpreter::visit_new_expr(new_expr* expr) {
    // This handles expressions like: new Point(), new Point(3.0, 4.0), etc.
    // The new_expr contains a type and arguments
    
    // std::cerr << "DEBUG: visit_new_expr called for type: " << (expr->type ? expr->type->type_name : "NULL") << std::endl;
    
    if (!expr->type) {
        throw runtime_error("New expression missing type information");
    }
    
    // Handle built-in types specially
    if (expr->type->base_type == script_value_type::jai_array_type) {
        // array<T>{} constructor
        if (!expr->arguments.empty()) {
            throw runtime_error("array{} constructor does not take arguments");
        }
        
        // Create empty array with the specified element type
        auto element_type = expr->type->element_type();
        if (!element_type) {
            element_type = type_info::make_int(); // Default to int if no type specified
        }
        push_value(script_value::make_array(element_type));
        return;
    }
    
    if (expr->type->base_type == script_value_type::jai_map_type) {
        // map<K,V>{} constructor
        if (!expr->arguments.empty()) {
            throw runtime_error("map{} constructor does not take arguments");
        }
        
        // Create empty map with the specified key/value types
        auto key_type = expr->type->key_type();
        auto value_type = expr->type->value_type();
        if (!key_type) key_type = type_info::make_string();
        if (!value_type) value_type = type_info::make_int();
        push_value(script_value::make_map(key_type, value_type));
        return;
    }
    
    if (expr->type->base_type == script_value_type::jai_weak_ptr_type) {
        // weak_ptr<T>() or weak_ptr<T>(obj) constructor
        if (expr->arguments.empty()) {
            // No arguments - create empty weak_ptr
            push_value(script_value::make_empty_weak_ptr(expr->type, engine_ref_));
        } else if (expr->arguments.size() == 1) {
            // One argument - create weak_ptr from object
            expr->arguments[0]->accept(this);
            script_value obj = pop_value();
            
            // Handle null objects
            if (obj.is_null()) {
                push_value(script_value::make_empty_weak_ptr(expr->type, engine_ref_));
                return;
            }
            
            // Allow creating weak_ptr from:
            // 1. Another weak_ptr (copy constructor)
            // 2. A shared_ptr (which in JaiScript is any object)
            // 3. A raw object (jai_object_type)
            
            if (obj.is_weak_ptr()) {
                // Copy constructor - just return the weak_ptr as-is
                push_value(obj);
                return;
            }
            
            // shared_ptr and objects are both jai_object_type internally
            if (obj.type() != script_value_type::jai_object_type) {
                auto type_info = obj.get_type_info();
                std::string type_name = type_info ? type_info->type_name : "unknown";
                throw runtime_error("weak_ptr can only be created from objects, shared_ptr, or other weak_ptrs, got " + type_name);
            }
            
            // Type validation is optional - in C++ you can have weak_ptr<Base> from shared_ptr<Derived>
            // For now, we'll create the weak_ptr without strict type checking
            push_value(script_value::make_weak_ptr(obj, engine_ref_));
        } else {
            throw runtime_error("weak_ptr() expects 0 or 1 arguments, got " + std::to_string(expr->arguments.size()));
        }
        return;
    }
    
    if (expr->type->base_type == script_value_type::jai_shared_ptr_type) {
        // shared_ptr<T>() or shared_ptr<T>(obj) constructor
        if (expr->arguments.empty()) {
            // No arguments - create empty shared_ptr (null)
            push_value(script_value::make_null(engine_ref_));
        } else if (expr->arguments.size() == 1) {
            // One argument - validate and return
            expr->arguments[0]->accept(this);
            script_value obj = pop_value();
            
            // Handle null
            if (obj.is_null()) {
                push_value(obj);
                return;
            }
            
            // Allow creating shared_ptr from:
            // 1. Another shared_ptr (which is any object in JaiScript)
            // 2. A raw object (jai_object_type)
            // Note: Cannot create shared_ptr from weak_ptr directly - need to use lock()
            
            if (obj.is_weak_ptr()) {
                throw runtime_error("Cannot create shared_ptr directly from weak_ptr - use weak.lock() instead");
            }
            
            // Only allow objects (which are already internally shared_ptr)
            if (obj.type() != script_value_type::jai_object_type) {
                auto type_info = obj.get_type_info();
                std::string type_name = type_info ? type_info->type_name : "unknown";
                throw runtime_error("shared_ptr can only be created from objects or other shared_ptrs, got " + type_name);
            }
            
            // The object is already a shared_ptr internally
            // Update its type info to reflect that it's explicitly a shared_ptr
            // This ensures reference semantics in assignments
            obj.set_type_info(expr->type);
            push_value(obj);
        } else {
            throw runtime_error("shared_ptr() expects 0 or 1 arguments, got " + std::to_string(expr->arguments.size()));
        }
        return;
    }
    
    std::string className = expr->type->type_name;
    
    // Evaluate all arguments
    std::vector<script_value> args;
    for (const auto& argExpr : expr->arguments) {
        argExpr->accept(this);
        args.push_back(std::move(pop_value()));
    }
    
    // Look for a constructor function registered with this class name
    // The class builder registers constructors as overloaded functions
    try {
        // std::cerr << "DEBUG: Looking for constructor: " << className << std::endl;
        script_value constructorFunc = environment_->get(className);
        if (constructorFunc.is_function()) {
            // std::cerr << "DEBUG: Found constructor function for: " << className << std::endl;
            const script_function& func = constructorFunc.as_function();
            script_value instance = func(args);
            push_value(std::move(instance));
            return;
        }
        // std::cerr << "DEBUG: Constructor found but not a function for: " << className << std::endl;
    } catch (const runtime_error& e) {
        // Constructor function not found, fall through to error
        // std::cerr << "DEBUG: Constructor not found for: " << className << " - " << e.what() << std::endl;
    }
    
    throw runtime_error("No constructor found for class: " + className);
}

void interpreter::visit_ternary_expr(ternary_expr* expr) {
    // Evaluate the condition
    expr->condition->accept(this);
    script_value conditionValue = pop_value();
    
    // Check if condition is truthy
    bool conditionIsTruthy = is_truthy(conditionValue);
    
    // Evaluate only the selected branch (short-circuit evaluation)
    if (conditionIsTruthy) {
        expr->then_expression->accept(this);
    } else {
        expr->else_expression->accept(this);
    }
}

void interpreter::visit_array_literal_expr(array_literal_expr* expr) {
    // Create array script_value with mixed element type (for now)
    auto element_type = type_info::make_int(); // TODO: Better type inference
    script_value arrayValue = script_value::make_array(element_type, engine_ref_);
    
    // Get the internal vector to populate
    auto& array = const_cast<std::vector<script_value>&>(arrayValue.as_array());
    
    // Evaluate each element and add to array
    for (const auto& element : expr->elements) {
        element->accept(this);
        array.push_back(pop_value());
    }
    
    push_value(std::move(arrayValue));
}

void interpreter::visit_map_literal_expr(map_literal_expr* expr) {
    // Create map script_value with mixed key/value types (for now)
    auto keyType = type_info::make_string(); // TODO: Better type inference
    auto valueType = type_info::make_int(); // TODO: Better type inference
    script_value mapValue = script_value::make_map(keyType, valueType, engine_ref_);
    
    // Get the internal map to populate
    auto& map = const_cast<std::map<script_value, script_value>&>(mapValue.as_map());
    
    // Evaluate each key-value pair and add to map
    for (const auto& entry : expr->entries) {
        // Evaluate key
        entry.first->accept(this);
        script_value key = pop_value();
        
        // Evaluate value
        entry.second->accept(this);
        script_value value = pop_value();
        
        // Insert into map
        map.insert_or_assign(std::move(key), std::move(value));
    }
    
    push_value(std::move(mapValue));
}

void interpreter::visit_this_expr(this_expr* expr) {
    // If we're in a class method context during parsing, allow 'this'
    if (current_class_context_ && current_class_context_->in_method) {
        // Push a placeholder value to continue parsing
        push_value(make_value());
        return;
    }
    
    // Try to get 'this' from the current environment
    try {
        script_value this_val = environment_->get("this");
        push_value(this_val);
    } catch (const runtime_error& e) {
        throw runtime_error("'this' can only be used inside methods");
    }
}

void interpreter::visit_super_expr(super_expr* expr) {
    // Super expression is used for accessing parent class methods: super::method()
    // Constructor delegation (Enemy() : super()) is handled in the parser/constructor
    
    // Get 'this' from the environment - super only makes sense in instance methods
    auto this_value = environment_->get("this");
    if (this_value.is_null()) {
        throw runtime_error("'super' used outside of class method");
    }
    
    // Push 'this' onto the stack - visit_member_expr will handle the parent lookup
    // when it detects that the object expression is a super_expr
    push_value(this_value);
}

void interpreter::visit_throw_expr(throw_expr* expr) {
    if (expr->value) {
        // Evaluate the expression to throw
        expr->value->accept(this);
        script_value val = pop_value();
        
        // Store the exception value and convert to string for exception message
        active_exception_value_ = val;
        std::string message = val.to_string();
        current_exception_ = script_exception(message, expr->location);
    } else {
        // Re-throw current exception
        if (!current_exception_) {
            throw script_exception("No exception to re-throw", expr->location);
        }
        // Keep the existing active_exception_value_
    }
    
    is_unwinding_ = true;
}

void interpreter::visit_if_stmt(if_stmt* stmt) {
    // Evaluate the condition
    stmt->condition->accept(this);
    script_value conditionValue = pop_value();
    
    // Execute appropriate branch based on truthiness
    if (is_truthy(conditionValue)) {
        stmt->then_statement->accept(this);
    } else if (stmt->else_statement) {
        stmt->else_statement->accept(this);
    }
}

void interpreter::visit_while_stmt(while_stmt* stmt) {
    while (true) {
        // Evaluate the condition
        stmt->condition->accept(this);
        script_value conditionValue = pop_value();
        
        // Check if we should continue the loop
        if (!is_truthy(conditionValue)) {
            break;
        }
        
        try {
            // Execute the loop body
            stmt->body->accept(this);
        } catch (const break_exception&) {
            // Break out of the loop
            break;
        } catch (const continue_exception&) {
            // Continue to next iteration
            continue;
        }
        
        // Check if a return statement was executed
        if (hasReturnValue_) {
            break;
        }
    }
}

void interpreter::visit_for_stmt(for_stmt* stmt) {
    // Create new scope for the for loop (initialization variables should be scoped)
    auto previous = environment_;
    environment_ = std::make_shared<environment>(environment_, string_symbolizer_);
    
    try {
        // Execute initialization (if present)
        if (stmt->initializer) {
            stmt->initializer->accept(this);
        }
        
        while (true) {
            // Check condition (if present, default to true)
            if (stmt->condition) {
                stmt->condition->accept(this);
                script_value conditionValue = pop_value();
                if (!is_truthy(conditionValue)) {
                    break;
                }
            }
            
            try {
                // Execute the loop body
                stmt->body->accept(this);
            } catch (const break_exception&) {
                // Break out of the loop
                break;
            } catch (const continue_exception&) {
                // Continue to next iteration, but execute update first
                if (stmt->update) {
                    stmt->update->accept(this);
                    // Pop the update result if it leaves a value on the stack
                    if (!valueStack_.empty()) {
                        pop_value();
                    }
                }
                continue;
            }
            
            // Check if a return statement was executed
            if (hasReturnValue_) {
                break;
            }
            
            // Execute update expression (if present)
            if (stmt->update) {
                stmt->update->accept(this);
                // Pop the update result if it leaves a value on the stack
                if (!valueStack_.empty()) {
                    pop_value();
                }
            }
        }
    } catch (...) {
        // Restore environment even if an error occurs
        environment_ = previous;
        throw;
    }
    
    // Restore previous environment
    environment_ = previous;
}

void interpreter::visit_range_for_stmt(range_for_stmt* stmt) {
    // Evaluate the container expression
    stmt->container->accept(this);
    script_value container = pop_value();
    
    // Create a new scope for the loop variable
    push_scope();
    
    try {
        if (container.is_array()) {
            // Iterate over array
            auto& array_storage = get_array_storage(container);
            
            for (size_t i = 0; i < array_storage->size(); ++i) {
                script_value loop_var = script_value::make_null(engine_ref_);
                
                if (stmt->is_reference) {
                    // Create a reference to the actual array element
                    loop_var = script_value::make_reference(&(*array_storage)[i], environment_, engine_ref_);
                } else {
                    // Make a copy of the element
                    loop_var = (*array_storage)[i].clone();
                }
                
                // Define the loop variable in current scope
                environment_->define(stmt->variable_name, std::move(loop_var));
                
                // Execute loop body
                try {
                    stmt->body->accept(this);
                } catch (const continue_exception&) {
                    continue; // Skip to next iteration
                } catch (const break_exception&) {
                    break; // Exit loop
                }
                
                // Check for return or exception
                if (hasReturnValue_ || is_unwinding_) {
                    break;
                }
            }
            
        } else if (container.is_map()) {
            // Iterate over map - return key-value pairs with first/second access
            auto& map_storage = get_map_storage(container);
            
            for (auto it = map_storage->begin(); it != map_storage->end(); ++it) {
                // Create a pair object using the registered stdlib::script_pair type
                script_value loop_var = script_value::make_null(engine_ref_);
                
                try {
                    if (stmt->is_reference) {
                        // For references, create a pair with a reference to the map value
                        // Cast away const to get a pointer (safe because we own the map)
                        script_value* value_ptr = const_cast<script_value*>(&it->second);
                        
                        // Create pair using the constructor with first as copy and second as reference
                        std::vector<script_value> args;
                        args.push_back(it->first);  // Don't clone - just pass the key
                        args.push_back(script_value::make_reference(value_ptr, environment_, engine_ref_));
                        
                        // Look up the pair constructor function
                        uint64_t pair_symbol_id = string_symbolizer_->intern("pair");
                        script_value pairConstructor = environment_->get_ref(pair_symbol_id);
                        
                        if (pairConstructor.is_function()) {
                            const script_function& func = pairConstructor.as_function();
                            loop_var = func(args);
                        } else {
                            throw runtime_error("pair type not registered - make sure stdlib is loaded");
                        }
                    } else {
                        // For copies, use regular pair constructor
                        std::vector<script_value> args;
                        args.push_back(it->first.clone());
                        args.push_back(it->second.clone());
                        
                        // Look up the pair constructor function using symbol ID
                        uint64_t pair_symbol_id = string_symbolizer_->intern("pair");
                        script_value pairConstructor = environment_->get_ref(pair_symbol_id);
                        
                        if (pairConstructor.is_function()) {
                            const script_function& func = pairConstructor.as_function();
                            loop_var = func(args);
                        } else {
                            throw runtime_error("pair type not registered - make sure stdlib is loaded");
                        }
                    }
                } catch (const runtime_error& e) {
                    throw runtime_error("Failed to create pair for map iteration: " + std::string(e.what()));
                }
                
                // Define the loop variable in current scope
                environment_->define(stmt->variable_name, std::move(loop_var));
                
                // Execute loop body
                try {
                    stmt->body->accept(this);
                } catch (const continue_exception&) {
                    continue; // Skip to next iteration
                } catch (const break_exception&) {
                    break; // Exit loop
                }
                
                // Check for return or exception
                if (hasReturnValue_ || is_unwinding_) {
                    break;
                }
            }
            
        } else {
            throw runtime_error("Range-based for loop requires an array or map, got unsupported type");
        }
        
    } catch (const break_exception&) {
        // Break caught from inner loop
    } catch (const continue_exception&) {
        // Continue should not escape the loop
        throw runtime_error("'continue' statement not in loop");
    }
    
    // Pop the loop scope
    pop_scope();
}

void interpreter::visit_return_stmt(return_stmt* stmt) {
    if (stmt->value) {
        // Evaluate the return expression
        stmt->value->accept(this);
        returnValue_ = std::move(pop_value());
    } else {
        // Return null if no expression
        returnValue_ = make_value();
    }
    
    hasReturnValue_ = true;
}

void interpreter::visit_break_stmt(break_stmt* stmt) {
    throw break_exception();
}

void interpreter::visit_continue_stmt(continue_stmt* stmt) {
    throw continue_exception();
}

void interpreter::visit_try_stmt(try_stmt* stmt) {
    // Save exception state
    auto saved_exception = current_exception_;
    auto saved_unwinding = is_unwinding_;
    auto saved_exception_value = active_exception_value_;
    auto saved_catch_var = current_catch_var_;
    
    // Reset state for try block
    // Don't reset exception state if we're in a catch block (allows re-throw)
    if (current_catch_var_.empty()) {
        current_exception_.reset();
        active_exception_value_ = make_value();
    }
    is_unwinding_ = false;
    current_catch_var_.clear();
    
    // Execute try block
    stmt->try_block->accept(this);
    
    // Check if exception was thrown
    if (is_unwinding_ && current_exception_) {
        // Reset unwinding flag
        is_unwinding_ = false;
        
        // Set the current catch variable name so identifier lookup can find it
        current_catch_var_ = stmt->catch_var;
        
        // Execute catch block
        stmt->catch_block->accept(this);
        
        // Clear catch variable
        current_catch_var_.clear();
        
        // Only clear exception if it wasn't re-thrown
        if (!is_unwinding_) {
            current_exception_.reset();
            active_exception_value_ = make_value();
        }
    }
    
    // If still unwinding after catch, we need to be careful about state restoration
    // Don't restore if a new exception was thrown in the catch block
    if (is_unwinding_ && saved_unwinding) {
        // We were already unwinding before this try/catch, restore that state
        current_exception_ = saved_exception;
        active_exception_value_ = saved_exception_value;
    }
    // If is_unwinding_ is true but saved_unwinding was false, 
    // it means a new exception was thrown in the catch block - keep it
    
    // Always restore the catch variable state
    current_catch_var_ = saved_catch_var;
}

void interpreter::visit_switch_stmt(switch_stmt* stmt) {
    // Evaluate the switch condition
    stmt->condition->accept(this);
    script_value switch_value = pop_value();
    
    // Save and set switch state
    bool old_in_switch = in_switch_;
    bool old_should_fallthrough = should_fallthrough_;
    in_switch_ = true;
    should_fallthrough_ = false;
    
    try {
        bool matched = false;
        bool executed_case = false;
        
        // Check each case
        for (const auto& case_stmt : stmt->cases) {
            // Evaluate case value
            case_stmt->value->accept(this);
            script_value case_value = pop_value();
            
            // Check if values match using operator==
            bool case_matches = false;
            try {
                case_matches = (switch_value == case_value);
            } catch (const std::exception& e) {
                // If comparison fails, treat as non-match
                case_matches = false;
            }
            
            // Execute case if it matches OR if we're falling through from a previous case
            if (case_matches || (executed_case && should_fallthrough_)) {
                matched = true;
                executed_case = true;

                // Reset fallthrough flag for this case (will be set again if case contains fallthrough statement)
                should_fallthrough_ = false;

                // Create a new scope for the case body (like an if statement)
                auto previous = environment_;
                environment_ = std::make_shared<environment>(environment_, string_symbolizer_);

                try {
                    // Execute case body
                    case_stmt->accept(this);

                    // Restore the previous environment
                    environment_ = previous;

                    // Check if we should continue to next case (implicit break by default)
                    if (!should_fallthrough_) {
                        break;  // Stop executing further cases
                    }
                    // If should_fallthrough_ is true, continue to next iteration
                } catch (...) {
                    // Restore environment before re-throwing
                    environment_ = previous;
                    throw;
                }
            }
        }

        // Execute default if no case matched OR if we're falling through from the last case
        if ((!matched || (executed_case && should_fallthrough_)) && stmt->default_case) {
            // Create a new scope for the default body
            auto previous = environment_;
            environment_ = std::make_shared<environment>(environment_, string_symbolizer_);

            try {
                stmt->default_case->accept(this);

                // Restore the previous environment
                environment_ = previous;
            } catch (...) {
                // Restore environment before re-throwing
                environment_ = previous;
                throw;
            }
        }
    } catch (const break_exception&) {
        // Break out of switch - this is expected behavior
    }
    
    // Restore switch state
    in_switch_ = old_in_switch;
    should_fallthrough_ = old_should_fallthrough;
}

void interpreter::visit_case_stmt(case_stmt* stmt) {
    // Execute all statements in the case body
    // Note: The scope is created by visit_switch_stmt when it decides to execute this case
    for (const auto& s : stmt->body) {
        s->accept(this);
        
        // Check for break or return
        if (hasReturnValue_ || is_unwinding_) {
            break;
        }
    }
}

void interpreter::visit_default_stmt(default_stmt* stmt) {
    // Execute all statements in the default body
    // Note: The scope is created by visit_switch_stmt when it decides to execute the default
    for (const auto& s : stmt->body) {
        s->accept(this);
        
        // Check for break or return
        if (hasReturnValue_ || is_unwinding_) {
            break;
        }
    }
}

void interpreter::visit_fallthrough_stmt(fallthrough_stmt* stmt) {
    // Set flag to continue to next case
    should_fallthrough_ = true;
}

void interpreter::visit_function_decl(function_decl* decl) {
    // Pre-cache symbol IDs for all parameters (parameter binding optimization)
    for (auto& param : decl->parameters) {
        if (param.symbol_id == UINT64_MAX) {
            param.symbol_id = string_symbolizer_->intern(param.name);
        }
    }
    
    // Don't capture any environment in the closure - just use nullptr
    // The environment stack will handle variable lookup naturally
    auto scriptFunc = std::make_shared<script_defined_function>(
        decl->name,
        decl->parameters,
        decl->return_type,
        decl->body,
        nullptr  // No closure needed - environment stack handles everything
    );
    
    // Create wrapper function
    script_value functionValue = script_value::make_function([this, scriptFunc](const std::vector<script_value>& args) -> script_value {
        return call_function(*scriptFunc, args);
    }, engine_ref_);
    
    // Define the function in current environment
    environment_->define(decl->name, functionValue);
}

void interpreter::visit_class_decl(class_decl* decl) {
    // Set up class parsing context
    class_context prev_context;
    bool had_context = false;
    if (current_class_context_) {
        prev_context = *current_class_context_;
        had_context = true;
    }
    
    // Create new context for this class
    current_class_context_ = class_context{decl->name, {}, false};
    
    // Restore previous context on exit
    auto context_guard = std::shared_ptr<void>(nullptr, [this, prev_context, had_context](void*) {
        if (had_context) {
            current_class_context_ = prev_context;
        } else {
            current_class_context_.reset();
        }
    });
    
    // Check if class already exists (for hot reloading)
    std::shared_ptr<script_class_definition> class_def = nullptr;
    bool is_redefinition = false;
    
    // Use a static prefix to avoid repeated allocations
    static const std::string CLASS_PREFIX = "__class_";
    std::string class_var_name = CLASS_PREFIX + decl->name;
    
    try {
        auto existing = environment_->get(class_var_name);
        if (!existing.is_null() && existing.is_object()) {
            // Class already exists - extract from object holder
            auto objHolder = existing.get_object_holder();
            if (objHolder && objHolder->type_name == "class_definition") {
                class_def = std::static_pointer_cast<script_class_definition>(objHolder->data);
                is_redefinition = true;
            }
        }
    } catch (...) {
        // Class doesn't exist yet
    }
    
    if (!class_def) {
        // Create a new script class definition
        class_def = std::make_shared<script_class_definition>(decl->name, engine_ref_);
    } else if (is_redefinition) {
        // Clear old ASTs for hot reload
        class_def->clear_asts();
    }
    
    // Collect new field defaults and methods
    std::unordered_map<std::string, script_value> new_field_defaults;
    std::unordered_map<std::string, script_value> new_methods;
    std::unordered_map<std::string, script_value> new_static_methods;
    
    // Reserve capacity based on member count for efficiency
    if (!decl->members.empty()) {
        new_field_defaults.reserve(decl->members.size());
        new_methods.reserve(decl->members.size());
        new_static_methods.reserve(decl->members.size());
    }
    
    // Debug output
    // std::cerr << "DEBUG: Processing class declaration: " << decl->name << std::endl;
    
    // Handle base classes (single inheritance for now)
    if (!decl->base_classes.empty()) {
        // For now, only support single inheritance
        if (decl->base_classes.size() > 1) {
            throw runtime_error("Multiple inheritance not supported");
        }
        
        // Look up base class definition
        const std::string& base_name = decl->base_classes[0];

        // First try to find a script class
        script_value base_class_var = script_value::make_null(engine_ref_);
        try {
            base_class_var = environment_->get("__class_" + base_name);
        } catch (...) {
            // Script class not found, will try C++ class below
        }

        if (!base_class_var.is_null() && base_class_var.is_object()) {
            // Found a script class - extract from object holder
            auto objHolder = base_class_var.get_object_holder();
            if (objHolder && objHolder->type_name == "class_definition") {
                auto base_class_def = std::static_pointer_cast<class_definition>(objHolder->data);
                class_def->set_parent(base_class_def);
            } else {
                throw runtime_error("Base class '" + base_name + "' is not a valid class definition");
            }
        } else {
            // Try to find a C++ class using the class lookup callback
            if (class_lookup_callback_) {
                auto cpp_class_def = class_lookup_callback_(base_name);
                if (cpp_class_def) {
                    // Found a C++ class! Set it as the base
                    class_def->set_cpp_base_class(cpp_class_def);
                    
                    // Also set as regular parent for method resolution
                    class_def->set_parent(cpp_class_def);
                } else if (environment_->contains(base_name)) {
                    // Constructor exists but no class definition found
                    // This shouldn't happen with proper engine integration
                    throw runtime_error("Constructor found for '" + base_name + "' but no class definition available");
                } else {
                    throw runtime_error("Base class not found: " + base_name);
                }
            } else {
                // No class lookup callback set - check if constructor exists
                if (environment_->contains(base_name)) {
                    throw runtime_error("Script class inheriting from C++ class requires engine integration");
                } else {
                    throw runtime_error("Base class not found: " + base_name);
                }
            }
        }
    }
    
    // Track whether we found an explicit constructor
    bool found_constructor = false;
    
    // Process class members
    for (const auto& member : decl->members) {
        // Extract the actual declaration from the member
        auto* var_decl = dynamic_cast<variable_decl*>(member.declaration.get());
        auto* func_decl = dynamic_cast<function_decl*>(member.declaration.get());

        if (var_decl) {
            // Field declaration
            script_value default_val(std::monostate{}, engine_ref_);  // Ensure engine reference
            std::string field_name = var_decl->name;
            
            if (var_decl->initializer) {
                // Check if the initializer is an assignment expression
                // This happens when the parser sees "x = 0" and creates assignment_expr
                auto* assign_expr = dynamic_cast<assignment_expr*>(var_decl->initializer.get());
                if (assign_expr) {
                    // For field declarations like "x = 0", we need to get the field name from the assignment
                    if (auto* ident_expr = dynamic_cast<identifier_expr*>(assign_expr->target.get())) {
                        field_name = ident_expr->name;
                    }
                    // Get the RHS value
                    assign_expr->value->accept(this);
                    default_val = pop_value();
                    
                    // Ensure the default value has an engine reference
                    if (default_val.get_engine_ref().expired() && !engine_ref_.expired()) {
                        default_val.set_engine_ref(engine_ref_);
                    }
                } else {
                    // Normal initializer expression
                    var_decl->initializer->accept(this);
                    default_val = pop_value();
                }
                
                // Ensure the default value has an engine reference
                if (default_val.get_engine_ref().expired() && !engine_ref_.expired()) {
                    default_val.set_engine_ref(engine_ref_);
                }
            }
            
            // Check if field is static
            if (var_decl->is_static) {
                // Add static field directly to the class
                if (!field_name.empty()) {
                    class_def->add_static_field(field_name, default_val);
                }
            } else {
                // Collect instance field for later processing
                if (!field_name.empty()) {
                    new_field_defaults[field_name] = default_val;
                }
            }
            
        } else if (func_decl) {
            // Method declaration
            auto method_name = func_decl->name;
            
            // Check for constructor
            if (method_name == decl->name) {
                // Constructor
                found_constructor = true;
                
                // Pre-cache symbol IDs for constructor parameters
                for (auto& param : func_decl->parameters) {
                    if (param.symbol_id == UINT64_MAX) {
                        param.symbol_id = string_symbolizer_->intern(param.name);
                    }
                }
                
                // Set in_method flag while processing constructor body (for static field access)
                if (current_class_context_) {
                    current_class_context_->in_method = true;
                }
                
                try {
                    class_def->add_constructor_from_ast(
                        std::static_pointer_cast<function_decl>(member.declaration),
                        this
                    );
                } catch (const runtime_error& e) {
                    // Reset in_method flag on error
                    if (current_class_context_) {
                        current_class_context_->in_method = false;
                    }
                    throw;
                }
                
                // Reset in_method flag
                if (current_class_context_) {
                    current_class_context_->in_method = false;
                }
                
                // Constructor will be registered after all members are processed
                
            } else if (method_name.size() > 0 && method_name[0] == '~') {
                // Destructor
                // Set in_method flag while processing destructor body
                if (current_class_context_) {
                    current_class_context_->in_method = true;
                }
                
                try {
                    class_def->add_destructor_from_ast(
                        std::static_pointer_cast<function_decl>(member.declaration),
                        this
                    );
                } catch (const runtime_error& e) {
                    // Reset in_method flag on error
                    if (current_class_context_) {
                        current_class_context_->in_method = false;
                    }
                    throw;
                }
                
                // Reset in_method flag
                if (current_class_context_) {
                    current_class_context_->in_method = false;
                }
                
            } else {
                // Regular method or static method
                auto method_ast = std::static_pointer_cast<function_decl>(member.declaration);
                
                if (is_redefinition) {
                    // For redefinition, just collect the method function
                    // We'll add it to the class via redefine_class later
                    
                    if (method_ast->is_static) {
                        // Static method - no 'this' parameter
                        auto static_method_func = [weak_self = std::weak_ptr<interpreter>(shared_from_this()), 
                                                  method_ast, 
                                                  class_def, 
                                                  class_name = decl->name](const std::vector<script_value>& args) -> script_value {
                            auto self = weak_self.lock();
                            if (!self) {
                                throw runtime_error("Interpreter was destroyed before static method call");
                            }
                            
                            // Create a static method environment (C++ scope rules for static members)
                            // This environment automatically resolves unqualified static member access
                            auto static_env = std::make_shared<static_method_environment>(
                                self->environment_,
                                self->string_symbolizer_,
                                class_def
                            );

                            // Call the interpreter method directly without 'this'
                            return self->execute_method_ast(method_ast, static_env, args);
                        };
                        
                        new_static_methods[method_name] = script_value::make_function(static_method_func, engine_ref_);
                    } else {
                        // Instance method - has 'this' parameter
                        auto method_func = [weak_self = std::weak_ptr<interpreter>(shared_from_this()), 
                                           method_ast, 
                                           class_def, 
                                           class_name = decl->name](const std::vector<script_value>& args) -> script_value {
                            auto self = weak_self.lock();
                            if (!self) {
                                throw runtime_error("Interpreter was destroyed before method call");
                            }
                            
                            // First argument should be 'this' object
                            if (args.empty()) {
                                throw runtime_error("Method called without 'this' object");
                            }
                            
                            // Extract 'this' from first argument
                            script_value this_obj = args[0];
                            
                            // Create remaining arguments (excluding 'this')
                            std::vector<script_value> method_args(args.begin() + 1, args.end());
                            
                            // Create a method environment that provides implicit 'this' field access
                            auto method_env = std::make_shared<method_environment>(
                                self->environment_, 
                                self->string_symbolizer_,
                                this_obj
                            );
                            method_env->define("this", this_obj);
                            
                            // Call the interpreter method directly
                            return self->execute_method_ast(method_ast, method_env, method_args);
                        };
                        
                        new_methods[method_name] = script_value::make_function(method_func, engine_ref_);
                    }
                } else {
                    // For new classes, add method normally
                    try {
                        // Set in_method flag while processing the method
                        if (current_class_context_) {
                            current_class_context_->in_method = true;
                        }

                        if (method_ast->is_static) {
                            // Add static method
                            class_def->add_static_script_method(method_name, method_ast, this);
                        } else {
                            // Add instance method
                            class_def->add_method_from_ast(method_name, method_ast, this, is_redefinition);
                        }
                        
                        // Reset in_method flag
                        if (current_class_context_) {
                            current_class_context_->in_method = false;
                        }
                    } catch (const runtime_error& e) {
                        // Reset in_method flag on error
                        if (current_class_context_) {
                            current_class_context_->in_method = false;
                        }
                        // Don't re-throw "Undefined variable" errors - they'll be validated later
                        std::string error_msg = e.what();
                        if (error_msg.find("Undefined variable") == std::string::npos) {
                            // Re-throw other errors
                            throw;
                        }
                        // For undefined variable errors, we've already collected them in unresolved_identifiers
                    }
                }
            }
        }
    }
    
    // After processing all members, create a dispatcher for constructors if any were found
    if (found_constructor) {
        // Create a constructor dispatcher that selects based on argument count
        auto ctor_dispatcher = [weak_self = std::weak_ptr<interpreter>(shared_from_this()), 
                               class_def, 
                               class_name = decl->name](const std::vector<script_value>& args) -> script_value {
            auto self = weak_self.lock();
            if (!self) {
                throw runtime_error("Interpreter was destroyed before constructor call");
            }
            
            // Get all constructor ASTs
            const auto& ctor_asts = class_def->get_constructor_asts();
            
            // Find constructor with matching parameter count
            std::shared_ptr<function_decl> matching_ctor;
            for (const auto& ctor_ast : ctor_asts) {
                if (ctor_ast->parameters.size() == args.size()) {
                    matching_ctor = ctor_ast;
                    break;
                }
            }
            
            if (!matching_ctor) {
                throw runtime_error("No constructor found for " + class_name + 
                                  " with " + std::to_string(args.size()) + " arguments");
            }
            
            // Create instance
            auto instance = class_def->create_instance();
            // Instance created
            
            // Create 'this' value
            auto this_value = script_value::make_object(class_name, instance, self->engine_ref_);
            
            // Create a temporary environment with 'this' and constructor parameters for evaluating initializer arguments
            auto init_env = std::make_shared<environment>(self->environment_, self->string_symbolizer_);
            init_env->define("this", this_value);

            // Bind constructor parameters so they're available in initializer expressions
            if (matching_ctor->parameters.size() != args.size()) {
                throw runtime_error("Constructor parameter count mismatch");
            }
            for (size_t i = 0; i < matching_ctor->parameters.size(); ++i) {
                init_env->define(matching_ctor->parameters[i].name, args[i]);
            }

            // Process constructor initializers (: super(args), : this(args))
            for (const auto& initializer : matching_ctor->initializers) {
                if (initializer.target == "super") {
                    // Call base class constructor
                    if (class_def->get_parent()) {
                        // Evaluate initializer arguments in init environment
                        std::vector<script_value> init_args;
                        init_args.reserve(initializer.arguments.size());
                        
                        // Temporarily switch to init environment for argument evaluation
                        auto old_env = self->environment_;
                        self->environment_ = init_env;
                        
                        for (const auto& arg_expr : initializer.arguments) {
                            arg_expr->accept(self.get());
                            init_args.push_back(self->pop_value());
                        }
                        
                        // Restore environment
                        self->environment_ = old_env;
                        
                        // Call parent constructor to initialize parent fields
                        auto parent_class = class_def->get_parent();
                        if (parent_class) {
                            // Check if parent is a script class
                            auto parent_script_class = std::dynamic_pointer_cast<script_class_definition>(parent_class);
                            if (parent_script_class) {
                                // Find matching parent constructor
                                const auto& parent_ctor_asts = parent_script_class->get_constructor_asts();
                                std::shared_ptr<function_decl> parent_ctor;
                                for (const auto& ctor_ast : parent_ctor_asts) {
                                    if (ctor_ast->parameters.size() == init_args.size()) {
                                        parent_ctor = ctor_ast;
                                        break;
                                    }
                                }

                                if (parent_ctor) {
                                    // Execute parent constructor with method environment
                                    auto parent_method_env = std::make_shared<method_environment>(
                                        self->environment_,
                                        self->string_symbolizer_,
                                        this_value
                                    );
                                    parent_method_env->define("this", this_value);

                                    self->execute_method_ast(parent_ctor, parent_method_env, init_args);
                                } else {
                                    throw runtime_error("No matching parent constructor found for super(" +
                                                      std::to_string(init_args.size()) + " arguments)");
                                }
                            } else {
                                // Parent is a C++ class - call its constructor
                                try {
                                    // Get the C++ class constructor function
                                    auto parent_name = parent_class->get_name();
                                    script_value cpp_ctor = self->environment_->get(parent_name);
                                    if (cpp_ctor.is_function()) {
                                        // Call C++ constructor with init_args
                                        script_value cpp_obj = cpp_ctor.as_function()(init_args);

                                        // Extract the C++ object and store it in _cpp_object field
                                        if (cpp_obj.is_object()) {
                                            auto cpp_instance = cpp_obj.as<std::shared_ptr<class_instance>>();
                                            if (cpp_instance && cpp_instance->has_field("_cpp_object")) {
                                                // Copy _cpp_object from parent to derived instance
                                                instance->set_field("_cpp_object", cpp_instance->get_field("_cpp_object"));
                                            }
                                        }
                                    }
                                } catch (const runtime_error& e) {
                                    throw runtime_error("Failed to call C++ parent constructor: " + std::string(e.what()));
                                }
                            }
                        }
                    } else {
                        throw runtime_error("Cannot call super() - class has no base class");
                    }
                } else if (initializer.target == "this") {
                    // Delegate to another constructor in the same class
                    // Evaluate initializer arguments in constructor environment
                    std::vector<script_value> init_args;
                    init_args.reserve(initializer.arguments.size());
                    
                    // Temporarily switch to init environment for argument evaluation
                    auto old_env = self->environment_;
                    self->environment_ = init_env;
                    
                    for (const auto& arg_expr : initializer.arguments) {
                        arg_expr->accept(self.get());
                        init_args.push_back(self->pop_value());
                    }
                    
                    // Restore environment
                    self->environment_ = old_env;
                    
                    // Find matching constructor in same class
                    const auto& ctor_asts = class_def->get_constructor_asts();
                    std::shared_ptr<function_decl> target_ctor;
                    for (const auto& ctor_ast : ctor_asts) {
                        if (ctor_ast->parameters.size() == init_args.size() && ctor_ast != matching_ctor) {
                            target_ctor = ctor_ast;
                            break;
                        }
                    }
                    
                    if (!target_ctor) {
                        throw runtime_error("No matching constructor found for this(" + 
                                          std::to_string(init_args.size()) + " arguments)");
                    }
                    
                    // Call the target constructor on this instance with method environment
                    auto target_method_env = std::make_shared<method_environment>(
                        self->environment_, 
                        self->string_symbolizer_,
                        this_value
                    );
                    target_method_env->define("this", this_value);
                    
                    self->execute_method_ast(target_ctor, target_method_env, init_args);
                }
            }
            
            // Execute the matching constructor with method environment
            // Create a method environment that provides implicit 'this' field access
            auto method_env = std::make_shared<method_environment>(
                self->environment_, 
                self->string_symbolizer_,
                this_value
            );
            method_env->define("this", this_value);
            
            // Execute constructor as a method so it has access to 'this' and fields
            self->execute_method_ast(matching_ctor, method_env, args);
            // Constructor executed
            
            auto result = script_value::make_object(class_name, instance, self->engine_ref_);
            // Object wrapped
            return result;
        };
        
        // Register the dispatcher
        environment_->define(decl->name, script_value::make_function(ctor_dispatcher, engine_ref_));
    }
    
    // If no constructor was found, create a default constructor
    else {
        // Create a default constructor that just initializes the instance
        auto default_ctor_func = [weak_self = std::weak_ptr<interpreter>(shared_from_this()), class_def, class_name = decl->name](const std::vector<script_value>& args) -> script_value {
            // Get strong reference from weak_ptr
            auto self = weak_self.lock();
            if (!self) {
                throw runtime_error("Interpreter was destroyed before constructor call");
            }
            
            // Default constructor shouldn't have arguments
            if (!args.empty()) {
                throw runtime_error("Default constructor for class " + class_name + " takes no arguments");
            }
            
            // Create instance using inherited create_instance()!
            // This will initialize all fields with their default values
            auto instance = class_def->create_instance();
            // Default constructor instance created
            
            auto result = script_value::make_object(class_name, instance, self->engine_ref_);
            // Default constructor object wrapped
            return result;
        };
        
        // Register default constructor
        environment_->define(decl->name, script_value::make_function(default_ctor_func, engine_ref_));
        // std::cerr << "DEBUG: Registered default constructor for class: " << decl->name << std::endl;
    }
    
    // If this is a redefinition, we need to call redefine_class to update all instances
    if (is_redefinition) {
        // Ensure all field defaults have engine references before passing to redefine_class
        std::unordered_map<std::string, script_value> field_defaults_with_engine;
        field_defaults_with_engine.reserve(new_field_defaults.size());
        
        for (const auto& [name, value] : new_field_defaults) {
            if (value.get_engine_ref().expired() && !engine_ref_.expired()) {
                // Create a copy with engine reference
                script_value value_with_engine(value);
                value_with_engine.set_engine_ref(engine_ref_);
                field_defaults_with_engine[name] = value_with_engine;
            } else {
                field_defaults_with_engine[name] = value;
            }
        }
        
        // Generate getter and setter methods for all fields (including new ones)
        // This is needed for hot reload to work properly with property access
        for (const auto& [field_name, default_val] : field_defaults_with_engine) {
            // Add getter method
            auto getter = [field_name, weak_eng = engine_ref_](const std::vector<script_value>& args) -> script_value {
                if (args.empty()) {
                    throw runtime_error("Property getter called without 'this' object");
                }
                
                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();
                
                // Get the field value
                return instance->get_field(field_name);
            };
            new_methods["_get_" + field_name] = script_value::make_function(getter, engine_ref_);
            
            // Add setter method
            auto setter = [field_name, weak_eng = engine_ref_](const std::vector<script_value>& args) -> script_value {
                if (args.size() != 2) {
                    throw runtime_error("Property setter requires 'this' object and value");
                }
                
                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();
                
                // Set the field value
                instance->set_field(field_name, args[1]);
                
                // Return the value that was set
                return args[1];
            };
            new_methods["_set_" + field_name] = script_value::make_function(setter, engine_ref_);
        }
        
        
        // Call redefine_class with the new field defaults and methods
        // Call redefine_class to migrate existing instances
        class_def->redefine_class(field_defaults_with_engine, new_methods, new_static_methods, engine_ref_);
    } else {
        // For new classes, add the fields normally
        for (const auto& [field_name, default_val] : new_field_defaults) {
            class_def->add_field(field_name, default_val);
            
            // Generate getter and setter methods for script class fields
            // This enables property-style access (obj.field) to work properly
            
            // Add getter method
            auto getter = [field_name, weak_eng = engine_ref_](const std::vector<script_value>& args) -> script_value {
                if (args.empty()) {
                    throw runtime_error("Property getter called without 'this' object");
                }
                
                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();
                
                // Get the field value
                return instance->get_field(field_name);
            };
            class_def->add_method("_get_" + field_name, getter);
            
            // Add setter method
            auto setter = [field_name, weak_eng = engine_ref_](const std::vector<script_value>& args) -> script_value {
                if (args.size() != 2) {
                    throw runtime_error("Property setter requires 'this' object and value");
                }
                
                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();
                
                // Set the field value
                instance->set_field(field_name, args[1]);
                
                // Return the value that was set
                return args[1];
            };
            class_def->add_method("_set_" + field_name, setter);
        }
        // Initialize fingerprint for future comparisons
        class_def->initialize_fingerprint();
    }
    
    // Validate unresolved identifiers before finalizing the class
    if (current_class_context_ && !current_class_context_->unresolved_identifiers.empty()) {
        // Get all fields including inherited ones (already efficient)
        auto all_fields = class_def->get_all_field_defaults();
        
        // Check each unresolved identifier
        std::vector<std::string> undefined_identifiers;
        for (const auto& identifier : current_class_context_->unresolved_identifiers) {
            // Skip special keywords that are always valid in methods
            if (identifier == "this" || identifier == "super") continue;
            
            // Check if it's a field
            if (all_fields.find(identifier) != all_fields.end()) continue;
            
            // Check if it's a method (including inherited)
            if (class_def->find_method(identifier).owner_class != nullptr) continue;
            
            // Not found as field or method
            undefined_identifiers.push_back(identifier);
        }
        
        // If there are still undefined identifiers, throw an error
        if (!undefined_identifiers.empty()) {
            std::string error_msg = "Undefined identifiers in class '" + decl->name + "': ";
            for (size_t i = 0; i < undefined_identifiers.size(); ++i) {
                if (i > 0) error_msg += ", ";
                error_msg += "'" + undefined_identifiers[i] + "'";
            }
            throw runtime_error(error_msg);
        }
    }
    
    // Store the class definition in a special variable for later retrieval
    // This allows inheritance and other features to work
    environment_->define(class_var_name, script_value::make_object("class_definition", class_def, engine_ref_));

    // The constructor function is already registered in the environment
    // which allows "new ClassName()" syntax to work
}

void interpreter::visit_expression_decl(expression_decl* decl) {
    // Evaluate the expression and leave the result on the stack
    // This allows top-level expressions to return values
    decl->expression->accept(this);
}

void interpreter::visit_include_decl(include_decl* decl) {
    // Get the engine reference
    auto engine_ptr = engine_ref_.lock();
    if (!engine_ptr) {
        throw runtime_error("Engine reference expired during include processing");
    }
    
    // Get the path either from literal or expression
    std::string path;
    if (decl->path_expr) {
        // Evaluate the expression to get the path
        decl->path_expr->accept(this);
        script_value path_value = pop_value();
        
        // Convert to string
        if (path_value.type() != script_value_type::jai_string_type) {
            throw runtime_error("Include path expression must evaluate to a string");
        }
        path = path_value.as<std::string>();
    } else {
        // Use the literal path
        path = decl->path;
    }
    
    // Resolve the file path
    std::string resolved_path = resolve_include_path(path, engine_ptr);
    
    // Read the file contents
    std::ifstream file(resolved_path);
    if (!file.is_open()) {
        throw runtime_error("Failed to open include file: " + path + " (resolved to: " + resolved_path + ")");
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // Execute the included file
    // Note: include always parses and executes the file
    auto result = engine_ptr->execute(content);
    
    // Push the result onto the value stack
    push_value(result);
}

void interpreter::visit_import_decl(import_decl* decl) {
    // Get the engine reference
    auto engine_ptr = engine_ref_.lock();
    if (!engine_ptr) {
        throw runtime_error("Engine reference expired during import processing");
    }
    
    // Get the path either from literal or expression
    std::string path;
    if (decl->path_expr) {
        // Evaluate the expression to get the path
        decl->path_expr->accept(this);
        script_value path_value = pop_value();
        
        // Convert to string
        if (path_value.type() != script_value_type::jai_string_type) {
            throw runtime_error("Import path expression must evaluate to a string");
        }
        path = path_value.as<std::string>();
    } else {
        // Use the literal path
        path = decl->path;
    }
    
    // Resolve the file path
    std::string resolved_path = resolve_include_path(path, engine_ptr);
    
    // Use the engine's public API to handle import with tracking
    auto result = engine_ptr->execute_import(resolved_path);
    
    // Push the result onto the value stack
    push_value(result);
}

// Execute a method AST with a given environment
script_value interpreter::execute_method_ast(std::shared_ptr<function_decl> ast,
                                           std::shared_ptr<environment> method_env,
                                           const std::vector<script_value>& args) {
    // Create a script_defined_function with the method environment
    script_defined_function script_func(
        ast->name,
        ast->parameters, 
        ast->return_type,
        ast->body,
        method_env  // Method environment with 'this'
    );
    
    // Execute method with the arguments
    return call_function(script_func, args);
}

// Function call implementation
script_value interpreter::call_function(const script_defined_function& function, const std::vector<script_value>& args) {
    // Validate arguments
    validate_function_arguments(function.parameters, args);
    
    
    // Create new environment for function execution using pool optimization
    // Both lambdas and functions need a fresh environment for their parameters
    auto previousEnv = environment_;
    
    // For lambdas with closures, the execution environment needs to chain:
    // [parameter env] -> [closure env] -> [global env]
    // For regular functions:
    // [parameter env] -> [current env]
    if (function.closure_env) {
        // Check if the closure is a method_environment
        if (auto method_env = std::dynamic_pointer_cast<method_environment>(function.closure_env)) {
            // Create a new method_environment that preserves implicit 'this' lookups
            environment_ = get_pooled_method_environment(method_env->get_parent(), method_env->get_this_object());
        } else {
            // Regular closure - create new environment for parameters
            environment_ = get_pooled_environment(function.closure_env);
        }
    } else {
        // Regular function: create fresh environment with current as parent
        environment_ = get_pooled_environment(previousEnv);
    }
    
    // Store previous return state
    bool previousHasReturn = hasReturnValue_;
    std::optional<script_value> previousReturn = returnValue_;
    hasReturnValue_ = false;
    
    try {
        
        // Bind parameters to arguments
        for (size_t i = 0; i < function.parameters.size(); ++i) {
            const auto& param = function.parameters[i];
            const auto& arg = args[i];
            
            
            // Use pre-cached symbol ID (parameter binding optimization)
            // Symbol IDs are cached at function definition time in visit_function_decl
            if (param.is_reference) {
                // For reference parameters, create a reference value
                if (!current_arg_metadata_.empty() && i < current_arg_metadata_.size()) {
                    auto symbol_id = current_arg_metadata_[i].first;
                    auto env = current_arg_metadata_[i].second;
                    
                    if (symbol_id != UINT64_MAX && env != nullptr) {
                        // Get pointer to the argument
                        script_value* argPtr = env->get_value_ptr(symbol_id);
                        if (!argPtr) {
                            throw runtime_error("Cannot take reference of undefined variable");
                        }
                        
                        // If the argument is itself a reference, get the final target
                        if (argPtr->is_reference()) {
                            auto refHolder = argPtr->get_reference_holder();
                            if (!refHolder || !refHolder->target) {
                                throw runtime_error("Reference target is null");
                            }
                            // Create reference to the final target
                            script_value refValue = script_value::make_reference(refHolder->target, refHolder->sourceEnv.lock());
                            if (param.symbol_id != UINT64_MAX) {
                                environment_->define(param.symbol_id, std::move(refValue));
                            } else {
                                environment_->define(param.name, std::move(refValue));
                            }
                        } else {
                            // Create reference to the argument
                            script_value refValue = script_value::make_reference(argPtr, env);
                            if (param.symbol_id != UINT64_MAX) {
                                environment_->define(param.symbol_id, std::move(refValue));
                            } else {
                                environment_->define(param.name, std::move(refValue));
                            }
                        }
                    } else {
                        // No metadata - can't create reference
                        throw runtime_error("Cannot pass non-lvalue to reference parameter");
                    }
                } else {
                    // No metadata - can't create reference
                    throw runtime_error("Cannot pass non-lvalue to reference parameter");
                }
            } else {
                // Non-reference parameter - deep copy the argument
                if (param.symbol_id != UINT64_MAX) {
                    environment_->define(param.symbol_id, arg.clone());
                } else {
                    // Fallback to parameter name if symbol_id not set
                    environment_->define(param.name, arg.clone());
                }
            }
        }
        
        // Execute function body without creating another environment
        // (since we already created one for the function call)
        for (const auto& decl : function.body->declarations) {
            decl->accept(this);
            // Check if we hit a return statement and break early
            if (hasReturnValue_) {
                break;
            }
        }
        
        // Get return value
        script_value result = script_value::make_null(engine_ref_);
        if (hasReturnValue_) {
            result = std::move(returnValue_.value());
        } else {
            // If no return statement, return null
            result = make_value();
        }
        
        // Restore previous state
        environment_ = previousEnv;
        hasReturnValue_ = previousHasReturn;
        returnValue_ = previousReturn;
        
        return result;
        
    } catch (...) {
        // Restore state on exception
        environment_ = previousEnv;
        hasReturnValue_ = previousHasReturn;
        returnValue_ = previousReturn;
        throw;
    }
}

void interpreter::validate_function_arguments(const std::vector<parameter>& params, const std::vector<script_value>& args) {
    if (params.size() != args.size()) {
        throw runtime_error("Function expected " + std::to_string(params.size()) + 
                         " arguments but got " + std::to_string(args.size()));
    }
    
    // TODO: Add type checking for parameters
    // For now, we'll just check argument count
}

script_value interpreter::make_function(std::shared_ptr<script_defined_function> func) {
    // Create a wrapper that handles reference parameters properly
    script_function wrapper = [this, func](const std::vector<script_value>& args) -> script_value {
        // For functions with reference parameters, we need special handling
        bool hasRefParams = false;
        for (const auto& param : func->parameters) {
            if (param.is_reference) {
                hasRefParams = true;
                break;
            }
        }
        
        if (!hasRefParams) {
            // No reference parameters - use normal call
            return call_function(*func, args);
        }
        
        // Has reference parameters - we need to handle them specially
        // For now, just call normally - we'll implement proper reference handling later
        return call_function(*func, args);
    };
    return script_value::make_function(wrapper, engine_ref_);
}

// Function call optimization helpers
std::shared_ptr<environment> interpreter::get_pooled_environment(std::shared_ptr<environment> parent) {
    if (environment_pool_index_ < environment_pool_.size()) {
        // Reuse existing environment from pool
        auto env = environment_pool_[environment_pool_index_++];
        env->reset(parent);
        return env;
    } else {
        // Pool is exhausted, create new environment and add to pool
        auto newEnv = std::make_shared<environment>(parent, string_symbolizer_);
        environment_pool_.push_back(newEnv);
        ++environment_pool_index_;
        return newEnv;
    }
}

std::shared_ptr<method_environment> interpreter::get_pooled_method_environment(std::shared_ptr<environment> parent, script_value this_obj) {
    if (method_environment_pool_index_ < method_environment_pool_.size()) {
        // Reuse existing method environment from pool
        auto env = method_environment_pool_[method_environment_pool_index_++];
        // Reset with new parent and this object
        env->reset(parent, std::move(this_obj));
        return env;
    } else {
        // Pool is exhausted, create new method environment and add to pool
        auto newEnv = std::make_shared<method_environment>(parent, string_symbolizer_, std::move(this_obj));
        method_environment_pool_.push_back(newEnv);
        ++method_environment_pool_index_;
        return newEnv;
    }
}

void interpreter::reset_environment_pool() {
    environment_pool_index_ = 0;
    method_environment_pool_index_ = 0;

    // Clear the environment values to release references
    for (auto& env : environment_pool_) {
        // Reset the environment by clearing its parent and values
        env->reset(nullptr);
    }
    for (auto& env : method_environment_pool_) {
        // Reset the method environment (clear values and this_object to release references)
        env->reset(nullptr, script_value::make_null(engine_ref_));
    }
}

} // namespace jai
