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
class CerealJaiScriptSerializer : public jai::ISerializer {
public:
    CerealJaiScriptSerializer() {
        // Register types that can be serialized
        registerSerializableType<GameCreature>("GameCreature");
    }
    
    std::vector<uint8_t> serializescript_value(const jai::script_value& value) override {
        std::stringstream ss;
        {
            cereal::BinaryOutputArchive ar(ss);
            serializeValueToArchive(ar, value);
        }
        
        std::string str = ss.str();
        return std::vector<uint8_t>(str.begin(), str.end());
    }
    
    jai::script_value deserializescript_value(const std::vector<uint8_t>& data) override {
        std::string str(data.begin(), data.end());
        std::stringstream ss(str);
        cereal::BinaryInputArchive ar(ss);
        
        return deserializeValueFromArchive(ar);
    }
    
private:
    template<typename T>
    void registerSerializableType(const std::string& type_name) {
        objectSerializers_[type_name] = {
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
    void serializeValueToArchive(Archive& ar, const jai::script_value& value) {
        // First serialize the type info
        auto type_info = value.get_type_info();
        if (!type_info) {
            int nullType = -1;
            ar(nullType);
            return;
        }
        
        // Serialize base type
        int base_type = static_cast<int>(type_info->base_type);
        ar(base_type);
        
        // For complex types, also serialize type parameters
        if (type_info->is_array() || type_info->is_shared_ptr() || type_info->is_weak_ptr() || type_info->is_reference()) {
            // Has one type parameter
            serializeTypeInfo(ar, type_info->get_element_type());
        } else if (type_info->is_map()) {
            // Has two type parameters
            serializeTypeInfo(ar, type_info->get_key_type());
            serializeTypeInfo(ar, type_info->get_value_type());
        } else if (type_info->is_function()) {
            // Has return type + arg types
            serializeTypeInfo(ar, type_info->get_return_type());
            auto arg_types = type_info->get_arg_types();
            size_t argCount = arg_types.size();
            ar(argCount);
            for (const auto& argType : arg_types) {
                serializeTypeInfo(ar, argType);
            }
        } else if (type_info->is_object()) {
            // Serialize the class name
            ar(type_info->type_name);
        }
        
        // Now serialize the actual value based on type
        switch (type_info->base_type) {
            case jai::value_type::jai_null_type:
                // Nothing to serialize
                break;
            case jai::value_type::jai_int_type:
                ar(value.as_int());
                break;
            case jai::value_type::jai_float_type:
                ar(value.as_float());
                break;
            case jai::value_type::jai_string_type:
                ar(value.as_string());
                break;
            case jai::value_type::jai_char_type:
                ar(value.as_char());
                break;
            case jai::value_type::jai_bool_type:
                ar(value.as_bool());
                break;
            case jai::value_type::jai_array_type: {
                // Serialize vector of Values
                // This would need access to the internal vector
                // auto& vec = value.as_array();
                // size_t size = vec.size();
                // ar(size);
                // for (const auto& elem : vec) {
                //     serializeValueToArchive(ar, elem);
                // }
                break;
            }
            case jai::value_type::jai_map_type: {
                // Serialize map of Values
                // Similar to array
                break;
            }
            case jai::value_type::jai_object_type: {
                // Use the type registry to serialize
                // auto holder = value.getObjectHolder();
                // Find serializer in objectSerializers_[holder->type_name]
                // Serialize the data
                break;
            }
            case jai::value_type::jai_shared_ptr_type: {
                // Serialize the pointed-to value
                // auto ptr = value.asshared_ptr();
                // bool hasscript_value = (ptr != nullptr);
                // ar(hasValue);
                // if (hasValue) {
                //     serializeValueToArchive(ar, *ptr);
                // }
                break;
            }
            case jai::value_type::jai_weak_ptr_type: {
                // Weak pointers can't be directly serialized
                // We'd need to handle this specially
                break;
            }
            case jai::value_type::jai_reference_type: {
                // References can't be directly serialized
                // We'd need to handle this specially (maybe serialize as a path/id)
                break;
            }
            case jai::value_type::jai_function_type: {
                // Serialize function info (script text, captures, etc.)
                // Only script-defined functions can be serialized
                // Native functions would be re-registered on deserialization
                break;
            }
        }
    }
    
    template<class Archive>
    void serializeTypeInfo(Archive& ar, jai::type_info_ptr type_info) {
        if (!type_info) {
            int nullType = -1;
            ar(nullType);
            return;
        }
        
        int base_type = static_cast<int>(type_info->base_type);
        ar(base_type);
        
        // Recursively serialize type parameters as needed
        // (implementation depends on the type)
    }
    
    template<class Archive>
    jai::script_value deserializeValueFromArchive(Archive& ar) {
        // Deserialize type info first
        int baseTypeInt;
        ar(baseTypeInt);
        
        if (baseTypeInt == -1) {
            return jai::script_value(); // Null
        }
        
        auto base_type = static_cast<jai::value_type>(baseTypeInt);
        
        // Based on type, deserialize the value
        switch (base_type) {
            case jai::value_type::jai_int_type: {
                jai::int_keyword val;
                ar(val);
                return jai::script_value(val);
            }
            case jai::value_type::jai_float_type: {
                jai::float_keyword val;
                ar(val);
                return jai::script_value(val);
            }
            case jai::value_type::jai_string_type: {
                jai::string_keyword val;
                ar(val);
                return jai::script_value(val);
            }
            case jai::value_type::jai_char_type: {
                jai::char_keyword val;
                ar(val);
                return jai::script_value(val);
            }
            case jai::value_type::jai_bool_type: {
                jai::bool_keyword val;
                ar(val);
                return jai::script_value(val);
            }
            // Handle complex types...
            default:
                return jai::script_value();
        }
    }
};

// Example usage
int main() {
    auto engine = jai::createEngine();
    auto serializer = std::make_unique<CerealJaiScriptSerializer>();
    
    // Create a creature in C++
    auto creature = std::make_shared<GameCreature>();
    creature->name = "FireElemental";
    creature->health = 100;
    creature->speed = 5.5f;
    
    // Pass it to JaiScript as a typed object
    // auto creaturescript_value = jai::make_object(creature, "GameCreature");
    // engine->add_global("creature", creatureValue);
    
    // Execute script that modifies the creature
    engine->eval(R"(
        // creature.health = 75;
        // creature.name = "Wounded " + creature.name;
        // 
        // // Create a shared pointer to the creature
        // shared_ptr<GameCreature> sharedCreature = make_shared_ptr(creature);
        // 
        // // Create an array of creatures
        // array<GameCreature> creatures = {creature};
        // 
        // // Create a map of creature names to health
        // map<string, int> healthMap;
        // healthMap[creature.name] = creature.health;
    )");
    
    // Serialize the entire engine state
    // auto state = engine->get_state();
    // auto serialized = serializer->serializeState(state);
    
    // Later, deserialize and restore
    // auto newState = serializer->deserializeState(serialized);
    // engine->set_state(newState);
    
    return 0;
}