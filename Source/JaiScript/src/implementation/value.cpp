#include "../../include/jaiscript/core/value.hpp"
#include <sstream>

namespace JaiScript {

// Factory methods
Value Value::makeArray(TypeInfoPtr elementType) {
    Value v;
    v.typeInfo_ = TypeInfo::makeArray(elementType);
    v.storage_ = std::make_shared<std::vector<Value>>();
    return v;
}

Value Value::makeMap(TypeInfoPtr keyType, TypeInfoPtr valueType) {
    Value v;
    v.typeInfo_ = TypeInfo::makeMap(keyType, valueType);
    v.storage_ = std::make_shared<std::map<Value, Value>>();
    return v;
}

Value Value::makeSharedPtr(const Value& value) {
    Value v;
    v.typeInfo_ = TypeInfo::makeSharedPtr(value.getTypeInfo());
    v.storage_ = std::make_shared<Value>(value);
    return v;
}

Value Value::makeWeakPtr(const Value& value) {
    // This is tricky - we need a shared_ptr to create a weak_ptr
    // For now, return null
    Value v;
    v.typeInfo_ = TypeInfo::makeWeakPtr(value.getTypeInfo());
    v.storage_ = std::weak_ptr<Value>();
    return v;
}

Value Value::makeReference(Value& target) {
    Value v;
    v.typeInfo_ = TypeInfo::makeReference(target.getTypeInfo());
    auto ref = std::make_shared<ReferenceHolder>();
    ref->target = &target;
    v.storage_ = ref;
    return v;
}

Value Value::makeObject(const std::string& typeName, std::shared_ptr<void> data) {
    Value v;
    v.typeInfo_ = TypeInfo::makeObject(typeName);
    auto obj = std::make_shared<ObjectHolder>();
    obj->typeName = typeName;
    obj->data = data;
    v.storage_ = obj;
    return v;
}

Value Value::makeFunction(const ScriptFunction& func) {
    Value v;
    v.typeInfo_ = TypeInfo::makeFunction(TypeInfo::makeVoid(), {}); // TODO: Proper type info
    v.storage_ = func;
    return v;
}


const ScriptFunction& Value::asFunction() const {
    if (type() != ValueType::Function) {
        throw RuntimeError("Value is not a function");
    }
    return std::get<ScriptFunction>(storage_);
}

std::string Value::toString() const {
    switch (type()) {
        case ValueType::Null:
            return "null";
        case ValueType::Int:
            return std::to_string(asInt());
        case ValueType::Float:
            return std::to_string(asFloat());
        case ValueType::String:
            return asString();
        case ValueType::Char:
            return std::string(1, asChar());
        case ValueType::Bool:
            return asBool() ? "true" : "false";
        case ValueType::Array:
            return "[array]";
        case ValueType::Map:
            return "[map]";
        case ValueType::Object:
            return "[object]";
        case ValueType::Function:
            return "[function]";
        default:
            return "[unknown]";
    }
}

bool Value::operator==(const Value& other) const {
    if (type() != other.type()) {
        return false;
    }
    
    switch (type()) {
        case ValueType::Null:
            return true;
        case ValueType::Int:
            return asInt() == other.asInt();
        case ValueType::Float:
            return asFloat() == other.asFloat();
        case ValueType::String:
            return asString() == other.asString();
        case ValueType::Char:
            return asChar() == other.asChar();
        case ValueType::Bool:
            return asBool() == other.asBool();
        default:
            // TODO: Implement for complex types
            return false;
    }
}

std::strong_ordering Value::operator<=>(const Value& other) const {
    // First compare types
    if (auto cmp = type() <=> other.type(); cmp != 0) {
        return cmp;
    }
    
    // Then compare values for same types
    switch (type()) {
        case ValueType::Null:
            return std::strong_ordering::equal; // All nulls are equal
        case ValueType::Int:
            return asInt() <=> other.asInt();
        case ValueType::Float:
            // Float comparison returns partial_ordering, convert to strong
            if (auto cmp = asFloat() <=> other.asFloat(); cmp < 0)
                return std::strong_ordering::less;
            else if (cmp > 0)
                return std::strong_ordering::greater;
            else
                return std::strong_ordering::equal;
        case ValueType::String:
            return asString() <=> other.asString();
        case ValueType::Char:
            return asChar() <=> other.asChar();
        case ValueType::Bool:
            return asBool() <=> other.asBool();
        default:
            // For complex types, compare by address for now
            return &storage_ <=> &other.storage_;
    }
}

} // namespace JaiScript