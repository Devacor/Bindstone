// Example of how to integrate Cereal with JaiScript
// This would live in Bindstone, not in JaiScript itself

#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/serialization.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/memory.hpp>
#include <sstream>

// Example game class that we want to use in scripts
struct GameCreature {
    std::string name;
    int health;
    float speed;
    
    template<class Archive>
    void serialize(Archive& ar) {
        ar(name, health, speed);
    }
};

// Cereal-based serializer implementation
class CerealJaiScriptSerializer : public JaiScript::ISerializer {
public:
    CerealJaiScriptSerializer() {
        // Register types that can be serialized
        registerSerializableType<GameCreature>("GameCreature");
    }
    
    std::vector<uint8_t> serializeValue(const JaiScript::Value& value) override {
        std::stringstream ss;
        {
            cereal::BinaryOutputArchive ar(ss);
            serializeValueToArchive(ar, value);
        }
        
        std::string str = ss.str();
        return std::vector<uint8_t>(str.begin(), str.end());
    }
    
    JaiScript::Value deserializeValue(const std::vector<uint8_t>& data) override {
        std::string str(data.begin(), data.end());
        std::stringstream ss(str);
        cereal::BinaryInputArchive ar(ss);
        
        return deserializeValueFromArchive(ar);
    }
    
private:
    template<typename T>
    void registerSerializableType(const std::string& typeName) {
        objectSerializers_[typeName] = {
            // Serializer
            [](const void* ptr, std::vector<uint8_t>& out) {
                std::stringstream ss;
                cereal::BinaryOutputArchive ar(ss);
                ar(*static_cast<const T*>(ptr));
                std::string str = ss.str();
                out.assign(str.begin(), str.end());
            },
            // Deserializer
            [](const std::vector<uint8_t>& data) -> std::shared_ptr<void> {
                std::string str(data.begin(), data.end());
                std::stringstream ss(str);
                cereal::BinaryInputArchive ar(ss);
                auto obj = std::make_shared<T>();
                ar(*obj);
                return std::static_pointer_cast<void>(obj);
            }
        };
    }
    
    template<class Archive>
    void serializeValueToArchive(Archive& ar, const JaiScript::Value& value) {
        // First serialize the type info
        auto typeInfo = value.getTypeInfo();
        if (!typeInfo) {
            int nullType = -1;
            ar(nullType);
            return;
        }
        
        // Serialize base type
        int baseType = static_cast<int>(typeInfo->baseType);
        ar(baseType);
        
        // For complex types, also serialize type parameters
        if (typeInfo->isArray() || typeInfo->isSharedPtr() || typeInfo->isWeakPtr() || typeInfo->isReference()) {
            // Has one type parameter
            serializeTypeInfo(ar, typeInfo->getElementType());
        } else if (typeInfo->isMap()) {
            // Has two type parameters
            serializeTypeInfo(ar, typeInfo->getKeyType());
            serializeTypeInfo(ar, typeInfo->getValueType());
        } else if (typeInfo->isFunction()) {
            // Has return type + arg types
            serializeTypeInfo(ar, typeInfo->getReturnType());
            auto argTypes = typeInfo->getArgTypes();
            size_t argCount = argTypes.size();
            ar(argCount);
            for (const auto& argType : argTypes) {
                serializeTypeInfo(ar, argType);
            }
        } else if (typeInfo->isObject()) {
            // Serialize the class name
            ar(typeInfo->typeName);
        }
        
        // Now serialize the actual value based on type
        switch (typeInfo->baseType) {
            case JaiScript::ValueType::Null:
                // Nothing to serialize
                break;
            case JaiScript::ValueType::Int:
                ar(value.asInt());
                break;
            case JaiScript::ValueType::Float:
                ar(value.asFloat());
                break;
            case JaiScript::ValueType::String:
                ar(value.asString());
                break;
            case JaiScript::ValueType::Char:
                ar(value.asChar());
                break;
            case JaiScript::ValueType::Bool:
                ar(value.asBool());
                break;
            case JaiScript::ValueType::Array: {
                // Serialize vector of Values
                // This would need access to the internal vector
                // auto& vec = value.asArray();
                // size_t size = vec.size();
                // ar(size);
                // for (const auto& elem : vec) {
                //     serializeValueToArchive(ar, elem);
                // }
                break;
            }
            case JaiScript::ValueType::Map: {
                // Serialize map of Values
                // Similar to array
                break;
            }
            case JaiScript::ValueType::Object: {
                // Use the type registry to serialize
                // auto holder = value.getObjectHolder();
                // Find serializer in objectSerializers_[holder->typeName]
                // Serialize the data
                break;
            }
            case JaiScript::ValueType::SharedPtr: {
                // Serialize the pointed-to value
                // auto ptr = value.asSharedPtr();
                // bool hasValue = (ptr != nullptr);
                // ar(hasValue);
                // if (hasValue) {
                //     serializeValueToArchive(ar, *ptr);
                // }
                break;
            }
            case JaiScript::ValueType::WeakPtr: {
                // Weak pointers can't be directly serialized
                // We'd need to handle this specially
                break;
            }
            case JaiScript::ValueType::Reference: {
                // References can't be directly serialized
                // We'd need to handle this specially (maybe serialize as a path/id)
                break;
            }
            case JaiScript::ValueType::Function: {
                // Serialize function info (script text, captures, etc.)
                // Only script-defined functions can be serialized
                // Native functions would be re-registered on deserialization
                break;
            }
        }
    }
    
    template<class Archive>
    void serializeTypeInfo(Archive& ar, JaiScript::TypeInfoPtr typeInfo) {
        if (!typeInfo) {
            int nullType = -1;
            ar(nullType);
            return;
        }
        
        int baseType = static_cast<int>(typeInfo->baseType);
        ar(baseType);
        
        // Recursively serialize type parameters as needed
        // (implementation depends on the type)
    }
    
    template<class Archive>
    JaiScript::Value deserializeValueFromArchive(Archive& ar) {
        // Deserialize type info first
        int baseTypeInt;
        ar(baseTypeInt);
        
        if (baseTypeInt == -1) {
            return JaiScript::Value(); // Null
        }
        
        auto baseType = static_cast<JaiScript::ValueType>(baseTypeInt);
        
        // Based on type, deserialize the value
        switch (baseType) {
            case JaiScript::ValueType::Int: {
                JaiScript::Int val;
                ar(val);
                return JaiScript::Value(val);
            }
            case JaiScript::ValueType::Float: {
                JaiScript::Float val;
                ar(val);
                return JaiScript::Value(val);
            }
            case JaiScript::ValueType::String: {
                JaiScript::String val;
                ar(val);
                return JaiScript::Value(val);
            }
            case JaiScript::ValueType::Char: {
                JaiScript::Char val;
                ar(val);
                return JaiScript::Value(val);
            }
            case JaiScript::ValueType::Bool: {
                JaiScript::Bool val;
                ar(val);
                return JaiScript::Value(val);
            }
            // Handle complex types...
            default:
                return JaiScript::Value();
        }
    }
};

// Example usage
int main() {
    auto engine = JaiScript::createEngine();
    auto serializer = std::make_unique<CerealJaiScriptSerializer>();
    
    // Create a creature in C++
    auto creature = std::make_shared<GameCreature>();
    creature->name = "FireElemental";
    creature->health = 100;
    creature->speed = 5.5f;
    
    // Pass it to JaiScript as a typed object
    // auto creatureValue = JaiScript::makeObject(creature, "GameCreature");
    // engine->addGlobal("creature", creatureValue);
    
    // Execute script that modifies the creature
    engine->eval(R"(
        // creature.health = 75;
        // creature.name = "Wounded " + creature.name;
        // 
        // // Create a shared pointer to the creature
        // SharedPtr<GameCreature> sharedCreature = makeSharedPtr(creature);
        // 
        // // Create an array of creatures
        // array<GameCreature> creatures = {creature};
        // 
        // // Create a map of creature names to health
        // map<string, int> healthMap;
        // healthMap[creature.name] = creature.health;
    )");
    
    // Serialize the entire engine state
    // auto state = engine->getState();
    // auto serialized = serializer->serializeState(state);
    
    // Later, deserialize and restore
    // auto newState = serializer->deserializeState(serialized);
    // engine->setState(newState);
    
    return 0;
}