#include "../../include/jaiscript/core/type_info.hpp"
#include <sstream>

namespace jai {

std::string type_info::to_string() const {
    std::stringstream ss;
    
    switch (base_type) {
        case script_value_type::jai_null_type:
            return "null";
        case script_value_type::jai_int_type:
            return "int";
        case script_value_type::jai_float_type:
            return "float";
        case script_value_type::jai_string_type:
            return "string";
        case script_value_type::jai_char_type:
            return "char";
        case script_value_type::jai_bool_type:
            return "bool";
            
        case script_value_type::jai_array_type:
            if (!type_params.empty()) {
                ss << "array<" << type_params[0]->to_string() << ">";
            } else {
                ss << "array<unknown>";
            }
            return ss.str();
            
        case script_value_type::jai_map_type:
            if (type_params.size() >= 2) {
                ss << "map<" << type_params[0]->to_string() 
                   << ", " << type_params[1]->to_string() << ">";
            } else {
                ss << "map<unknown, unknown>";
            }
            return ss.str();
            
        case script_value_type::jai_object_type:
            return type_name.empty() ? "object" : type_name;
            
        case script_value_type::jai_function_type:
            ss << "function<";
            if (!type_params.empty()) {
                ss << type_params[0]->to_string() << "(";
                for (size_t i = 1; i < type_params.size(); ++i) {
                    if (i > 1) ss << ", ";
                    ss << type_params[i]->to_string();
                }
                ss << ")";
            } else {
                ss << "unknown()";
            }
            ss << ">";
            return ss.str();
            
        case script_value_type::jai_reference_type:
            if (!type_params.empty()) {
                ss << type_params[0]->to_string() << "&";
            } else {
                ss << "unknown&";
            }
            return ss.str();
            
        case script_value_type::jai_shared_ptr_type:
            if (!type_params.empty()) {
                ss << "shared_ptr<" << type_params[0]->to_string() << ">";
            } else {
                ss << "shared_ptr<unknown>";
            }
            return ss.str();
            
        case script_value_type::jai_weak_ptr_type:
            if (!type_params.empty()) {
                ss << "weak_ptr<" << type_params[0]->to_string() << ">";
            } else {
                ss << "weak_ptr<unknown>";
            }
            return ss.str();
            
        default:
            return "unknown";
    }
}

bool type_info::equals(const type_info& other) const {
    if (base_type != other.base_type) {
        return false;
    }
    
    if (type_name != other.type_name) {
        return false;
    }
    
    if (type_params.size() != other.type_params.size()) {
        return false;
    }
    
    for (size_t i = 0; i < type_params.size(); ++i) {
        if (!type_params[i] || !other.type_params[i]) {
            if (type_params[i] != other.type_params[i]) {
                return false;
            }
        } else if (!type_params[i]->equals(*other.type_params[i])) {
            return false;
        }
    }
    
    return true;
}

} // namespace jai