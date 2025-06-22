#pragma once

#include "types.hpp"
#include "value.hpp"
#include <vector>
#include <functional>
#include <map>

namespace JaiScript {

    // Forward declaration
    class Engine;
    
    // Serialization support for JaiScript
    // This is designed to be implemented externally (e.g., with Cereal)
    // to keep JaiScript dependency-free
    
    class ISerializer {
    public:
        virtual ~ISerializer() = default;
        
        // Serialize a Value to bytes
        virtual std::vector<uint8_t> serializeValue(const Value& value) = 0;
        
        // Deserialize bytes to a Value  
        virtual Value deserializeValue(const std::vector<uint8_t>& data) = 0;
        
        // Register a type for serialization
        // This allows external code to tell JaiScript how to serialize custom types
        template<typename T>
        void registerType(const std::string& typeName) {
            objectSerializers_[typeName] = {
                // Serializer function
                [](const void* ptr, std::vector<uint8_t>& out) {
                    // Implementation would use Cereal here:
                    // std::stringstream ss;
                    // cereal::BinaryOutputArchive ar(ss);
                    // ar(*static_cast<const T*>(ptr));
                    // Copy ss to out
                },
                // Deserializer function
                [](const std::vector<uint8_t>& data) -> std::shared_ptr<void> {
                    // Implementation would use Cereal here:
                    // std::stringstream ss(data);
                    // cereal::BinaryInputArchive ar(ss);
                    // auto obj = std::make_shared<T>();
                    // ar(*obj);
                    // return obj;
                    return nullptr; // Placeholder
                }
            };
        }
        
    protected:
        // Registry of serialization functions for custom object types
        std::map<std::string, std::pair<
            std::function<void(const void*, std::vector<uint8_t>&)>,      // Serializer
            std::function<std::shared_ptr<void>(const std::vector<uint8_t>&)> // Deserializer
        >> objectSerializers_;
    };
    
    // Helper to create a Value that can be serialized
    // The serializer must have the type registered
    template<typename T>
    Value makeObject(std::shared_ptr<T> obj, const std::string& typeName) {
        // This will be implemented to create an ObjectHolder
        // that references the registered serialization functions
        Value result;
        // TODO: Implementation
        return result;
    }
    
    // Example of how this would be used with Cereal in Bindstone:
    /*
    class CerealJaiScriptSerializer : public JaiScript::ISerializer {
        template<typename T>
        void registerCerealType(const std::string& name) {
            registerType<T>(name);
            // The actual serialization would use Cereal archives
        }
        
        std::vector<uint8_t> serializeValue(const Value& value) override {
            std::stringstream ss;
            {
                cereal::BinaryOutputArchive ar(ss);
                // Serialize based on value.type()
                // For objects, look up typeName in registry
            }
            // Convert stringstream to vector<uint8_t>
        }
    };
    */
    
} // namespace JaiScript