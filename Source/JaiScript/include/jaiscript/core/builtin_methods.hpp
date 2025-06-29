#pragma once

#include "value.hpp"
#include <unordered_map>
#include <functional>
#include <vector>
#include <string>

namespace jai {

// Forward declaration
class interpreter;

// Type for built-in method implementations
// Takes interpreter (can be null for VM), self object, and arguments
using builtin_method = std::function<script_value(interpreter*, const script_value&, const std::vector<script_value>&)>;

// Helper to access array/map storage
namespace detail {
    // Since as_array() returns const&, we need to const_cast to modify
    inline std::vector<script_value>& get_mutable_array(const script_value& value) {
        return const_cast<std::vector<script_value>&>(value.as_array());
    }
    
    inline std::map<script_value, script_value>& get_mutable_map(const script_value& value) {
        return const_cast<std::map<script_value, script_value>&>(value.as_map());
    }
}

// Built-in method registry
class builtin_method_registry {
public:
    // Get the singleton instance
    static builtin_method_registry& instance() {
        static builtin_method_registry registry;
        return registry;
    }
    
    // Get array methods
    const std::unordered_map<std::string, builtin_method>& array_methods() const {
        return array_methods_;
    }
    
    // Get map methods
    const std::unordered_map<std::string, builtin_method>& map_methods() const {
        return map_methods_;
    }
    
    // Find array method
    builtin_method find_array_method(const std::string& name) const {
        auto it = array_methods_.find(name);
        return it != array_methods_.end() ? it->second : nullptr;
    }
    
    // Find map method
    builtin_method find_map_method(const std::string& name) const {
        auto it = map_methods_.find(name);
        return it != map_methods_.end() ? it->second : nullptr;
    }
    
private:
    builtin_method_registry() {
        initialize_array_methods();
        initialize_map_methods();
    }
    
    void initialize_array_methods() {
        array_methods_ = {
            {"size", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
                if (!args.empty()) {
                    throw runtime_error("size() takes no arguments");
                }
                return script_value(static_cast<script_int>(self.as_array().size()));
            }},
            
            {"push", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
                if (args.size() != 1) {
                    throw runtime_error("push() takes exactly one argument");
                }
                auto& array = detail::get_mutable_array(self);
                array.push_back(args[0].clone());  // Deep copy when pushing
                return script_value(); // void return
            }},
            
            {"pop", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
                if (!args.empty()) {
                    throw runtime_error("pop() takes no arguments");
                }
                auto& array = detail::get_mutable_array(self);
                if (array.empty()) {
                    throw runtime_error("Cannot pop from empty array");
                }
                script_value last = array.back();
                array.pop_back();
                return last;
            }},
            
            {"empty", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
                if (!args.empty()) {
                    throw runtime_error("empty() takes no arguments");
                }
                return script_value(self.as_array().empty());
            }},
            
            {"clear", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
                if (!args.empty()) {
                    throw runtime_error("clear() takes no arguments");
                }
                auto& array = detail::get_mutable_array(self);
                array.clear();
                return script_value(); // void return
            }},
            
            {"front", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
                if (!args.empty()) {
                    throw runtime_error("front() takes no arguments");
                }
                const auto& arr = self.as_array();
                if (arr.empty()) {
                    throw runtime_error("Cannot get front of empty array");
                }
                return arr.front();
            }},
            
            {"back", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
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
    }
    
    void initialize_map_methods() {
        map_methods_ = {
            {"size", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
                if (!args.empty()) {
                    throw runtime_error("size() takes no arguments");
                }
                return script_value(static_cast<script_int>(self.as_map().size()));
            }},
            
            {"empty", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
                if (!args.empty()) {
                    throw runtime_error("empty() takes no arguments");
                }
                return script_value(self.as_map().empty());
            }},
            
            {"clear", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
                if (!args.empty()) {
                    throw runtime_error("clear() takes no arguments");
                }
                auto& map = detail::get_mutable_map(self);
                map.clear();
                return script_value(); // void return
            }},
            
            {"contains", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
                if (args.size() != 1) {
                    throw runtime_error("contains() takes exactly one argument");
                }
                const auto& map = self.as_map();
                return script_value(map.find(args[0]) != map.end());
            }},
            
            {"erase", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
                if (args.size() != 1) {
                    throw runtime_error("erase() takes exactly one argument");
                }
                auto& map = detail::get_mutable_map(self);
                map.erase(args[0]);
                return script_value(); // void return
            }},
            
            {"keys", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
                if (!args.empty()) {
                    throw runtime_error("keys() takes no arguments");
                }
                const auto& map = self.as_map();
                script_value result = script_value::make_array(nullptr);
                auto& array = detail::get_mutable_array(result);
                array.reserve(map.size());
                for (const auto& [key, value] : map) {
                    array.push_back(key.clone());
                }
                return result;
            }},
            
            {"values", [](interpreter*, const script_value& self, const std::vector<script_value>& args) -> script_value {
                if (!args.empty()) {
                    throw runtime_error("values() takes no arguments");
                }
                const auto& map = self.as_map();
                script_value result = script_value::make_array(nullptr);
                auto& array = detail::get_mutable_array(result);
                array.reserve(map.size());
                for (const auto& [key, value] : map) {
                    array.push_back(value.clone());
                }
                return result;
            }}
        };
    }
    
    std::unordered_map<std::string, builtin_method> array_methods_;
    std::unordered_map<std::string, builtin_method> map_methods_;
};

} // namespace jai