#pragma once

#include <typeindex>
#include <unordered_map>
#include <string>
#include <memory>
#include <iostream>

namespace JaiScript {

// Global type name registry for mapping C++ types to their registered names
class TypeNameRegistry {
public:
    static TypeNameRegistry& instance() {
        static TypeNameRegistry registry;
        return registry;
    }
    
    // Register a type with its script name
    template<typename T>
    void registerType(const std::string& name) {
        std::cerr << "TypeRegistry: Registering type " << typeid(T).name() << " as " << name << std::endl;
        typeNames_[std::type_index(typeid(T))] = name;
        
        // Also register shared_ptr<T> variant
        typeNames_[std::type_index(typeid(std::shared_ptr<T>))] = name;
    }
    
    // Get the registered name for a type
    template<typename T>
    std::string getTypeName() const {
        auto it = typeNames_.find(std::type_index(typeid(T)));
        if (it != typeNames_.end()) {
            return it->second;
        }
        
        // If not found, return the raw type name as fallback
        std::cerr << "TypeRegistry: No registered name for type " << typeid(T).name() << std::endl;
        return typeid(T).name();
    }
    
    // Get name by type_index
    std::string getTypeName(std::type_index type) const {
        auto it = typeNames_.find(type);
        if (it != typeNames_.end()) {
            return it->second;
        }
        return type.name();
    }
    
private:
    TypeNameRegistry() = default;
    std::unordered_map<std::type_index, std::string> typeNames_;
};

// Helper function for registering types
template<typename T>
void registerTypeName(const std::string& name) {
    TypeNameRegistry::instance().registerType<T>(name);
}

// Helper function for getting type names
template<typename T>
std::string getRegisteredTypeName() {
    return TypeNameRegistry::instance().getTypeName<T>();
}

} // namespace JaiScript