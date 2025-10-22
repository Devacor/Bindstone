#pragma once

#include "jaiscript/core/value.hpp"
#include <memory>
#include <variant>
#include <unordered_map>

namespace jai {
namespace jvm {

// VM-specific type aliases
using vm_value = script_value;
using vm_object = std::shared_ptr<class_instance>;
using vm_reference = std::shared_ptr<script_value>;

// VM function types
class vm_function;
using vm_function_ptr = std::shared_ptr<vm_function>;

// VM class types
class vm_class;
using vm_class_ptr = std::shared_ptr<vm_class>;

// VM execution context
struct execution_context {
    // Stack operations
    void push(const vm_value& value);
    vm_value pop();
    bool empty() const;
    
    // Call frame management
    void push_call_frame(uint16_t max_locals);
    void pop_call_frame();
    
    // Local variable access
    void set_local(uint16_t index, const vm_value& value);
    vm_value get_local(uint16_t index) const;
    
    // Captured variable access
    void set_captured(uint16_t index, const vm_value& value);
    vm_value get_captured(uint16_t index) const;
    
    // Bytecode execution
    void execute_bytecode(const std::vector<uint8_t>& bytecode, const std::vector<vm_value>& constants);
    
    // Reading bytecode
    uint8_t read_byte();
    uint16_t read_u16();
    
    // TODO: Full implementation needed
    // This is a minimal interface to allow compilation
};
using vm_context = execution_context;

// VM class management
class vm_class_manager;
class vm_class_system_integration;

// Method cache for optimization
struct inline_method_cache {
    struct cache_entry {
        std::string class_name;
        std::string method_name;
        uint32_t cached_offset = 0;
        uint32_t hit_count = 0;
        uint32_t miss_count = 0;
    };
    
    std::unordered_map<uint64_t, cache_entry> entries;
    
    void clear() { entries.clear(); }
};

} // namespace jvm
} // namespace jai