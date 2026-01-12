#pragma once

#include "types.hpp"
#include "value.hpp"
#include <vector>
#include <functional>
#include <map>

namespace jai {

    // Forward declaration
    class engine;
    
    // Serialization support for JaiScript
    // This is designed to be implemented externally
    // to keep JaiScript dependency-free
    
    class ISerializer {
    public:
        virtual ~ISerializer() = default;
        
        // Serialize a script_value to bytes
        virtual std::vector<uint8_t> serializescript_value(const script_value& value) = 0;
        
        // Deserialize bytes to a script_value  
        virtual script_value deserializescript_value(const std::vector<uint8_t>& data) = 0;
        
        // Register a type for serialization
        // This allows external code to tell JaiScript how to serialize custom types
        template<typename T>
        void registerType(const std::string& type_name) {
            objectSerializers_[type_name] = {
                // Serializer function
                [](const void* ptr, std::vector<uint8_t>& out) {
                    // Implement serialization for type T to binary data
                    // Copy result to out
                },
                // Deserializer function
                [](const std::vector<uint8_t>& data) -> std::shared_ptr<void> {
                    // Implement deserialization from binary data to type T
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
    
    // Helper to create a script_value that can be serialized
    // The serializer must have the type registered
    template<typename T>
    script_value make_object(std::shared_ptr<T> obj, const std::string& type_name) {
        // This will be implemented to create an object_holder
        // that references the registered serialization functions
        script_value result;
        // TODO: implementation
        return result;
    }
    
    // Example of how this would be used in external code:
    /*
    class CustomJaiScriptSerializer : public jai::ISerializer {
        template<typename T>
        void registerCustomType(const std::string& name) {
            registerType<T>(name);
            // Implement actual serialization using your chosen binary format
        }

        std::vector<uint8_t> serializescript_value(const script_value& value) override {
            // Serialize based on value.type()
            // For objects, look up type_name in registry
            // Return binary data
        }
    };
    */
    
} // namespace jai