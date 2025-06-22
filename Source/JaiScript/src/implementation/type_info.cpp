#include "../../include/jaiscript/core/type_info.hpp"
#include <sstream>

namespace JaiScript {

std::string TypeInfo::toString() const {
    std::stringstream ss;
    
    switch (baseType) {
        case ValueType::Null:
            return "null";
        case ValueType::Int:
            return "int";
        case ValueType::Float:
            return "float";
        case ValueType::String:
            return "string";
        case ValueType::Char:
            return "char";
        case ValueType::Bool:
            return "bool";
            
        case ValueType::Array:
            if (!typeParams.empty()) {
                ss << "array<" << typeParams[0]->toString() << ">";
            } else {
                ss << "array<unknown>";
            }
            return ss.str();
            
        case ValueType::Map:
            if (typeParams.size() >= 2) {
                ss << "map<" << typeParams[0]->toString() 
                   << ", " << typeParams[1]->toString() << ">";
            } else {
                ss << "map<unknown, unknown>";
            }
            return ss.str();
            
        case ValueType::Object:
            return typeName.empty() ? "object" : typeName;
            
        case ValueType::Function:
            ss << "function<";
            if (!typeParams.empty()) {
                ss << typeParams[0]->toString() << "(";
                for (size_t i = 1; i < typeParams.size(); ++i) {
                    if (i > 1) ss << ", ";
                    ss << typeParams[i]->toString();
                }
                ss << ")";
            } else {
                ss << "unknown()";
            }
            ss << ">";
            return ss.str();
            
        case ValueType::Reference:
            if (!typeParams.empty()) {
                ss << typeParams[0]->toString() << "&";
            } else {
                ss << "unknown&";
            }
            return ss.str();
            
        case ValueType::SharedPtr:
            if (!typeParams.empty()) {
                ss << "SharedPtr<" << typeParams[0]->toString() << ">";
            } else {
                ss << "SharedPtr<unknown>";
            }
            return ss.str();
            
        case ValueType::WeakPtr:
            if (!typeParams.empty()) {
                ss << "WeakPtr<" << typeParams[0]->toString() << ">";
            } else {
                ss << "WeakPtr<unknown>";
            }
            return ss.str();
            
        default:
            return "unknown";
    }
}

bool TypeInfo::equals(const TypeInfo& other) const {
    if (baseType != other.baseType) {
        return false;
    }
    
    if (typeName != other.typeName) {
        return false;
    }
    
    if (typeParams.size() != other.typeParams.size()) {
        return false;
    }
    
    for (size_t i = 0; i < typeParams.size(); ++i) {
        if (!typeParams[i] || !other.typeParams[i]) {
            if (typeParams[i] != other.typeParams[i]) {
                return false;
            }
        } else if (!typeParams[i]->equals(*other.typeParams[i])) {
            return false;
        }
    }
    
    return true;
}

} // namespace JaiScript