#include "../../include/jaiscript/core/conversion_registry.hpp"
#include "../../include/jaiscript/core/conversion_registry_templates.hpp"
#include "../../include/jaiscript/core/conversion_registry_impl.hpp"
#include "../../include/jaiscript/core/engine_impl.hpp"
#include "../../include/jaiscript/core/value.hpp"
#include "../../include/jaiscript/core/engine.hpp"
#include "../../include/jaiscript/core/bound_array.hpp"
#include "../../include/jaiscript/core/bound_map.hpp"
#include "../../include/jaiscript/core/class_builder.hpp"
#include <limits>
#include <stdexcept>
#include <algorithm>

namespace jai {
namespace conversions {

// Built-in type conversion implementations
void conversion_registry::register_builtin_conversion(script_value_type from, script_value_type to, int cost, 
                               std::function<script_value(const script_value&)> converter) {
    // Remove existing conversion if any
    auto it = std::find_if(builtin_conversions_.begin(), builtin_conversions_.end(),
        [from, to](const BuiltinConversion& c) { 
            return c.from == from && c.to == to; 
        });
    
    if (it != builtin_conversions_.end()) {
        *it = {from, to, cost, converter};
    } else {
        builtin_conversions_.push_back({from, to, cost, converter});
    }
}

bool conversion_registry::can_convert_builtin(script_value_type from, script_value_type to) const {
    if (from == to) return true;
    
    return std::any_of(builtin_conversions_.begin(), builtin_conversions_.end(),
        [from, to](const BuiltinConversion& c) { 
            return c.from == from && c.to == to; 
        });
}

int conversion_registry::get_builtin_conversion_cost(script_value_type from, script_value_type to) const {
    if (from == to) return 0;
    
    auto it = std::find_if(builtin_conversions_.begin(), builtin_conversions_.end(),
        [from, to](const BuiltinConversion& c) { 
            return c.from == from && c.to == to; 
        });
    
    return (it != builtin_conversions_.end()) ? it->cost : 1000;
}

script_value conversion_registry::convert_builtin(const script_value& value, script_value_type targetType) const {
    if (value.type() == targetType) return value;
    
    auto it = std::find_if(builtin_conversions_.begin(), builtin_conversions_.end(),
        [&](const BuiltinConversion& c) { 
            return c.from == value.type() && c.to == targetType; 
        });
    
    if (it != builtin_conversions_.end()) {
        return it->converter(value);
    }
    
    throw std::runtime_error("No conversion available from " + 
                          std::to_string(static_cast<int>(value.type())) + 
                          " to " + std::to_string(static_cast<int>(targetType)));
}

bool conversion_registry::try_custom_conversion(const script_value& v, const std::type_info& target_type, script_value& out) const {
    auto it = from_conversions_.find(type_id::of_type(target_type));
    if (it != from_conversions_.end()) {
        void* result_ptr = it->second(v);
        auto to_it = to_conversions_.find(type_id::of_type(target_type));
        if (to_it != to_conversions_.end()) {
            out = to_it->second(result_ptr);
            auto destructor_it = destructors_.find(type_id::of_type(target_type));
            if (destructor_it != destructors_.end()) {
                destructor_it->second(result_ptr);
            }
            return true;
        }
    }
    return false;
}

// conversion_manager implementation
void conversion_manager::add_builtin_conversion(script_value_type from, script_value_type to, int cost,
                           std::function<script_value(const script_value&)> converter) {
    if (registry_) {
        registry_->register_builtin_conversion(from, to, cost, converter);
    }
}

// Register standard vector conversions with smart int handling
void register_standard_vector_conversions(std::shared_ptr<conversion_registry> registry) {
    if (!registry) return;
    
    // Vector<int> with smart down-conversion from int64_t (script_int)
    registry->register_conversion<std::vector<int>>(
        [](const script_value& v) -> std::vector<int> {
            auto script_array = v.as<std::vector<script_value>>();
            std::vector<int> result;
            result.reserve(script_array.size());
            
            for (const auto& item : script_array) {
                if (item.is_int()) {
                    auto int64_val = item.as<int64_t>();
                    // Bounds check for down-conversion
                    if (int64_val < std::numeric_limits<int>::min() || 
                        int64_val > std::numeric_limits<int>::max()) {
                        throw std::runtime_error("Integer value out of range for int: " + std::to_string(int64_val));
                    }
                    result.push_back(static_cast<int>(int64_val));
                } else {
                    throw std::runtime_error("Array element is not an integer");
                }
            }
            
            return result;
        },
        [registry](const std::vector<int>& vec) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for vector conversion");
            }
            return conversions::convert_vector_to_script_array(vec, eng);
        }
    );
    
    // Vector<int64_t> - direct mapping to script_int
    registry->register_conversion<std::vector<int64_t>>(
        [](const script_value& v) -> std::vector<int64_t> {
            return v.as<std::vector<int64_t>>();
        },
        [registry](const std::vector<int64_t>& vec) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for vector conversion");
            }
            return conversions::convert_vector_to_script_array(vec, eng);
        }
    );
    
    // Vector<float>
    registry->register_conversion<std::vector<float>>(
        [](const script_value& v) -> std::vector<float> {
            auto script_array = v.as<std::vector<script_value>>();
            std::vector<float> result;
            result.reserve(script_array.size());
            
            for (const auto& item : script_array) {
                if (item.is_float()) {
                    result.push_back(item.as<float>());
                } else if (item.is_int()) {
                    // Allow int->float conversion
                    result.push_back(static_cast<float>(item.as<int64_t>()));
                } else {
                    throw std::runtime_error("Array element is not a number");
                }
            }
            
            return result;
        },
        [registry](const std::vector<float>& vec) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for vector conversion");
            }
            return conversions::convert_vector_to_script_array(vec, eng);
        }
    );
    
    // Vector<double>
    registry->register_conversion<std::vector<double>>(
        [](const script_value& v) -> std::vector<double> {
            auto script_array = v.as<std::vector<script_value>>();
            std::vector<double> result;
            result.reserve(script_array.size());
            
            for (const auto& item : script_array) {
                if (item.is_float()) {
                    result.push_back(item.as<double>());
                } else if (item.is_int()) {
                    // Allow int->double conversion
                    result.push_back(static_cast<double>(item.as<int64_t>()));
                } else {
                    throw std::runtime_error("Array element is not a number");
                }
            }
            
            return result;
        },
        [registry](const std::vector<double>& vec) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for vector conversion");
            }
            return conversions::convert_vector_to_script_array(vec, eng);
        }
    );
    
    // Vector<std::string>
    registry->register_conversion<std::vector<std::string>>(
        [](const script_value& v) -> std::vector<std::string> {
            return v.as<std::vector<std::string>>();
        },
        [registry](const std::vector<std::string>& vec) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for vector conversion");
            }
            return conversions::convert_vector_to_script_array(vec, eng);
        }
    );
    
    // Vector<bool>
    registry->register_conversion<std::vector<bool>>(
        [](const script_value& v) -> std::vector<bool> {
            auto script_array = v.as<std::vector<script_value>>();
            std::vector<bool> result;
            result.reserve(script_array.size());
            
            for (const auto& item : script_array) {
                if (item.is_bool()) {
                    result.push_back(item.as<bool>());
                } else if (item.is_int()) {
                    // Allow int->bool conversion (0 = false, non-zero = true)
                    result.push_back(item.as<int64_t>() != 0);
                } else {
                    throw std::runtime_error("Array element is not a boolean or integer");
                }
            }
            
            return result;
        },
        [registry](const std::vector<bool>& vec) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for vector conversion");
            }
            return conversions::convert_vector_to_script_array(vec, eng);
        }
    );
}

// Register standard map conversions with smart int handling
void register_standard_map_conversions(std::shared_ptr<conversion_registry> registry) {
    if (!registry) return;
    
    // Map<string, int> with smart down-conversion
    registry->register_conversion<std::map<std::string, int>>(
        [](const script_value& v) -> std::map<std::string, int> {
            auto script_map = v.as<std::map<script_value, script_value>>();
            std::map<std::string, int> result;
            
            for (const auto& [key, value] : script_map) {
                if (!key.is_string()) {
                    throw std::runtime_error("Map key is not a string");
                }
                
                if (!value.is_int()) {
                    throw std::runtime_error("Map value is not an integer");
                }
                
                auto int64_val = value.as<int64_t>();
                // Bounds check for down-conversion
                if (int64_val < std::numeric_limits<int>::min() || 
                    int64_val > std::numeric_limits<int>::max()) {
                    throw std::runtime_error("Integer value out of range for int: " + std::to_string(int64_val));
                }
                
                result[key.as<std::string>()] = static_cast<int>(int64_val);
            }
            
            return result;
        },
        [registry](const std::map<std::string, int>& map) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for map conversion");
            }
            auto eng_weak = eng->weak_from_this();
            auto script_map = script_value::make_map(nullptr, nullptr, eng_weak);
            auto& sm = const_cast<std::map<script_value, script_value>&>(script_map.as_map());
            
            for (const auto& [key, value] : map) {
                sm.insert_or_assign(script_value(key, eng_weak), script_value(static_cast<int64_t>(value), eng_weak));
            }
            
            return script_map;
        }
    );
    
    // Map<string, int64_t> - direct mapping
    registry->register_conversion<std::map<std::string, int64_t>>(
        [](const script_value& v) -> std::map<std::string, int64_t> {
            return v.as<std::map<std::string, int64_t>>();
        },
        [registry](const std::map<std::string, int64_t>& map) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for map conversion");
            }
            auto eng_weak = eng->weak_from_this();
            auto script_map = script_value::make_map(nullptr, nullptr, eng_weak);
            auto& sm = const_cast<std::map<script_value, script_value>&>(script_map.as_map());
            for (const auto& [key, value] : map) {
                sm.insert_or_assign(script_value(key, eng_weak), script_value(value, eng_weak));
            }
            return script_map;
        }
    );
    
    // Map<string, double> with int->double conversion
    registry->register_conversion<std::map<std::string, double>>(
        [](const script_value& v) -> std::map<std::string, double> {
            auto script_map = v.as<std::map<script_value, script_value>>();
            std::map<std::string, double> result;
            
            for (const auto& [key, value] : script_map) {
                if (!key.is_string()) {
                    throw std::runtime_error("Map key is not a string");
                }
                
                double converted_value;
                if (value.is_float()) {
                    converted_value = value.as<double>();
                } else if (value.is_int()) {
                    converted_value = static_cast<double>(value.as<int64_t>());
                } else {
                    throw std::runtime_error("Map value is not a number");
                }
                
                result[key.as<std::string>()] = converted_value;
            }
            
            return result;
        },
        [registry](const std::map<std::string, double>& map) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for map conversion");
            }
            auto eng_weak = eng->weak_from_this();
            auto script_map = script_value::make_map(nullptr, nullptr, eng_weak);
            auto& sm = const_cast<std::map<script_value, script_value>&>(script_map.as_map());
            for (const auto& [key, value] : map) {
                sm.insert_or_assign(script_value(key, eng_weak), script_value(value, eng_weak));
            }
            return script_map;
        }
    );
    
    // Map<string, float> with int->float conversion
    registry->register_conversion<std::map<std::string, float>>(
        [](const script_value& v) -> std::map<std::string, float> {
            auto script_map = v.as<std::map<script_value, script_value>>();
            std::map<std::string, float> result;
            
            for (const auto& [key, value] : script_map) {
                if (!key.is_string()) {
                    throw std::runtime_error("Map key is not a string");
                }
                
                float converted_value;
                if (value.is_float()) {
                    converted_value = value.as<float>();
                } else if (value.is_int()) {
                    converted_value = static_cast<float>(value.as<int64_t>());
                } else {
                    throw std::runtime_error("Map value is not a number");
                }
                
                result[key.as<std::string>()] = converted_value;
            }
            
            return result;
        },
        [registry](const std::map<std::string, float>& map) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for map conversion");
            }
            auto eng_weak = eng->weak_from_this();
            auto script_map = script_value::make_map(nullptr, nullptr, eng_weak);
            auto& sm = const_cast<std::map<script_value, script_value>&>(script_map.as_map());
            for (const auto& [key, value] : map) {
                sm.insert_or_assign(script_value(key, eng_weak), script_value(static_cast<double>(value), eng_weak));
            }
            return script_map;
        }
    );
    
    // Map<string, string>
    registry->register_conversion<std::map<std::string, std::string>>(
        [](const script_value& v) -> std::map<std::string, std::string> {
            return v.as<std::map<std::string, std::string>>();
        },
        [registry](const std::map<std::string, std::string>& map) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for map conversion");
            }
            auto eng_weak = eng->weak_from_this();
            auto script_map = script_value::make_map(nullptr, nullptr, eng_weak);
            auto& sm = const_cast<std::map<script_value, script_value>&>(script_map.as_map());
            for (const auto& [key, value] : map) {
                sm.insert_or_assign(script_value(key, eng_weak), script_value(value, eng_weak));
            }
            return script_map;
        }
    );
    
    // Map<string, bool> with int->bool conversion
    registry->register_conversion<std::map<std::string, bool>>(
        [](const script_value& v) -> std::map<std::string, bool> {
            auto script_map = v.as<std::map<script_value, script_value>>();
            std::map<std::string, bool> result;
            
            for (const auto& [key, value] : script_map) {
                if (!key.is_string()) {
                    throw std::runtime_error("Map key is not a string");
                }
                
                bool converted_value;
                if (value.is_bool()) {
                    converted_value = value.as<bool>();
                } else if (value.is_int()) {
                    converted_value = value.as<int64_t>() != 0;
                } else {
                    throw std::runtime_error("Map value is not a boolean or integer");
                }
                
                result[key.as<std::string>()] = converted_value;
            }
            
            return result;
        },
        [registry](const std::map<std::string, bool>& map) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for map conversion");
            }
            auto eng_weak = eng->weak_from_this();
            auto script_map = script_value::make_map(nullptr, nullptr, eng_weak);
            auto& sm = const_cast<std::map<script_value, script_value>&>(script_map.as_map());
            for (const auto& [key, value] : map) {
                sm.insert_or_assign(script_value(key, eng_weak), script_value(value, eng_weak));
            }
            return script_map;
        }
    );
    
    // Map<int, string> with bounds checking
    registry->register_conversion<std::map<int, std::string>>(
        [](const script_value& v) -> std::map<int, std::string> {
            auto script_map = v.as<std::map<script_value, script_value>>();
            std::map<int, std::string> result;
            
            for (const auto& [key, value] : script_map) {
                if (!key.is_int()) {
                    throw std::runtime_error("Map key is not an integer");
                }
                
                if (!value.is_string()) {
                    throw std::runtime_error("Map value is not a string");
                }
                
                auto int64_key = key.as<int64_t>();
                // Bounds check for down-conversion
                if (int64_key < std::numeric_limits<int>::min() || 
                    int64_key > std::numeric_limits<int>::max()) {
                    throw std::runtime_error("Integer key out of range for int: " + std::to_string(int64_key));
                }
                
                result[static_cast<int>(int64_key)] = value.as<std::string>();
            }
            
            return result;
        },
        [registry](const std::map<int, std::string>& map) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for map conversion");
            }
            auto eng_weak = eng->weak_from_this();
            auto script_map = script_value::make_map(nullptr, nullptr, eng_weak);
            auto& sm = const_cast<std::map<script_value, script_value>&>(script_map.as_map());
            
            for (const auto& [key, value] : map) {
                sm.insert_or_assign(script_value(static_cast<int64_t>(key), eng_weak), script_value(value, eng_weak));
            }
            
            return script_map;
        }
    );
    
    // Map<int64_t, string> - direct mapping
    registry->register_conversion<std::map<int64_t, std::string>>(
        [](const script_value& v) -> std::map<int64_t, std::string> {
            return v.as<std::map<int64_t, std::string>>();
        },
        [registry](const std::map<int64_t, std::string>& map) -> script_value {
            auto eng = registry->get_engine();
            if (!eng) {
                throw std::runtime_error("Engine reference required for map conversion");
            }
            auto eng_weak = eng->weak_from_this();
            auto script_map = script_value::make_map(nullptr, nullptr, eng_weak);
            auto& sm = const_cast<std::map<script_value, script_value>&>(script_map.as_map());
            for (const auto& [key, value] : map) {
                sm.insert_or_assign(script_value(key, eng_weak), script_value(value, eng_weak));
            }
            return script_map;
        }
    );
}

// Register bound_array conversions for zero-copy access
void register_bound_array_conversions(std::shared_ptr<conversion_registry> registry) {
    if (!registry) return;
    
    // Register conversions for common bound_array types
    // Note: These don't actually convert, they just verify the type
    // The actual zero-copy wrapping happens in the value_converter specializations
    
    // bound_array<int>
    registry->register_conversion<bound_array<int>>(
        [](const script_value& v) -> bound_array<int> {
            if (!v.is_array()) {
                throw std::runtime_error("Cannot convert non-array to bound_array<int>");
            }
            return bound_array<int>(v);
        },
        [](const bound_array<int>& arr) -> script_value {
            return arr.as_script_value();
        }
    );
    
    // bound_array<double>
    registry->register_conversion<bound_array<double>>(
        [](const script_value& v) -> bound_array<double> {
            if (!v.is_array()) {
                throw std::runtime_error("Cannot convert non-array to bound_array<double>");
            }
            return bound_array<double>(v);
        },
        [](const bound_array<double>& arr) -> script_value {
            return arr.as_script_value();
        }
    );
    
    // bound_array<std::string>
    registry->register_conversion<bound_array<std::string>>(
        [](const script_value& v) -> bound_array<std::string> {
            if (!v.is_array()) {
                throw std::runtime_error("Cannot convert non-array to bound_array<std::string>");
            }
            return bound_array<std::string>(v);
        },
        [](const bound_array<std::string>& arr) -> script_value {
            return arr.as_script_value();
        }
    );
    
    // bound_array<bool>
    registry->register_conversion<bound_array<bool>>(
        [](const script_value& v) -> bound_array<bool> {
            if (!v.is_array()) {
                throw std::runtime_error("Cannot convert non-array to bound_array<bool>");
            }
            return bound_array<bool>(v);
        },
        [](const bound_array<bool>& arr) -> script_value {
            return arr.as_script_value();
        }
    );
}

// Register bound_map conversions for zero-copy access
void register_bound_map_conversions(std::shared_ptr<conversion_registry> registry) {
    if (!registry) return;
    
    // Register conversions for common bound_map types
    
    // bound_map<std::string, int>
    registry->register_conversion<bound_map<std::string, int>>(
        [](const script_value& v) -> bound_map<std::string, int> {
            if (!v.is_map()) {
                throw std::runtime_error("Cannot convert non-map to bound_map<std::string, int>");
            }
            return bound_map<std::string, int>(v);
        },
        [](const bound_map<std::string, int>& map) -> script_value {
            return map.as_script_value();
        }
    );
    
    // bound_map<std::string, double>
    registry->register_conversion<bound_map<std::string, double>>(
        [](const script_value& v) -> bound_map<std::string, double> {
            if (!v.is_map()) {
                throw std::runtime_error("Cannot convert non-map to bound_map<std::string, double>");
            }
            return bound_map<std::string, double>(v);
        },
        [](const bound_map<std::string, double>& map) -> script_value {
            return map.as_script_value();
        }
    );
    
    // bound_map<std::string, std::string>
    registry->register_conversion<bound_map<std::string, std::string>>(
        [](const script_value& v) -> bound_map<std::string, std::string> {
            if (!v.is_map()) {
                throw std::runtime_error("Cannot convert non-map to bound_map<std::string, std::string>");
            }
            return bound_map<std::string, std::string>(v);
        },
        [](const bound_map<std::string, std::string>& map) -> script_value {
            return map.as_script_value();
        }
    );
}

// Register all standard conversions
void register_all_standard_conversions(std::shared_ptr<conversion_registry> registry) {
    register_standard_vector_conversions(registry);
    register_standard_map_conversions(registry);
    register_bound_array_conversions(registry);
    register_bound_map_conversions(registry);
}

// C++ type converter methods
void conversion_registry::register_cpp_type_converter(type_id tid, 
                                                     std::function<script_value(const void*)> converter) {
    cpp_type_converters_[tid] = converter;
}

std::function<script_value(const void*)> conversion_registry::get_cpp_type_converter(type_id tid) const {
    auto it = cpp_type_converters_.find(tid);
    return (it != cpp_type_converters_.end()) ? it->second : nullptr;
}

script_value conversion_registry::convert_cpp_type_from_void(type_id tid, const void* obj) const {
    auto it = cpp_type_converters_.find(tid);
    if (it != cpp_type_converters_.end()) {
        return it->second(obj);
    }
    throw std::runtime_error("No C++ type converter registered for type_id");
}

} // namespace conversions

// Explicit template instantiations for commonly used types
template void conversions::conversion_manager::add_custom_conversion<int>(
    std::function<int(const script_value&)>, 
    std::function<script_value(const int&)>
);

template void conversions::conversion_manager::add_custom_conversion<std::string>(
    std::function<std::string(const script_value&)>, 
    std::function<script_value(const std::string&)>
);

template void conversions::conversion_manager::add_vector_conversion<int>();
template void conversions::conversion_manager::add_vector_conversion<double>();
template void conversions::conversion_manager::add_vector_conversion<std::string>();

// Explicit instantiations for bound_array conversions
template void conversions::conversion_manager::add_bound_array_conversion<int>();
template void conversions::conversion_manager::add_bound_array_conversion<double>();
template void conversions::conversion_manager::add_bound_array_conversion<std::string>();
template void conversions::conversion_manager::add_bound_array_conversion<bool>();

// Explicit instantiations for bound_map conversions
template void conversions::conversion_manager::add_bound_map_conversion<std::string, int>();
template void conversions::conversion_manager::add_bound_map_conversion<std::string, double>();
template void conversions::conversion_manager::add_bound_map_conversion<std::string, std::string>();

} // namespace jai