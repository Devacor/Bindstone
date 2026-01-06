#pragma once

/**
 * JaiScript VM Class System - Complete Bytecode Implementation
 * 
 * This header provides the complete VM bytecode implementation of the class system,
 * with full interoperability between interpreter and VM classes.
 * 
 * Features:
 * - VM bytecode instructions for all class operations
 * - Centralized virtual dispatch matching interpreter design
 * - Constructor delegation with proper bytecode sequencing
 * - RAII destructor support integrated with VM garbage collection
 * - Per-class method caching with inline cache optimization
 * - Field access optimization with indexed access
 * - Cross-calling between VM and interpreter classes
 * - JIT compilation support for hot methods
 * - Performance profiling and optimization hints
 * 
 * Usage:
 * 
 *   // Initialize VM with class support
 *   jaiscript::engine js;
 *   jaiscript::jvm::vm_executor executor;
 *   jaiscript::jvm::vm_compiler compiler;
 *   
 *   // Enable class systems
 *   auto interpreter_classes = jaiscript::enable_class_system(js);
 *   auto vm_classes = jaiscript::jvm::enable_vm_class_system(executor, compiler);
 *   
 *   // Enable cross-calling
 *   vm_classes->integrate_with_interpreter(interpreter_classes.get());
 *   
 *   // Compile class to bytecode
 *   compiler.compile(R"(
 *     class Point {
 *       float x = 0.0;
 *       float y = 0.0;
 *       
 *       Point(float px, float py) {
 *         x = px; y = py;
 *       }
 *       
 *       float distance() {
 *         return sqrt(x*x + y*y);
 *       }
 *     }
 *   )");
 *   
 *   // Execute bytecode
 *   executor.execute(compiler.get_bytecode());
 * 
 * Performance Notes:
 * - Method calls use inline caching for monomorphic call sites
 * - Field access can be optimized to indexed access at compile time
 * - Virtual calls are optimized when types can be determined statically
 * - Hot methods can be JIT compiled for native performance
 */

// Core VM class system headers
#include "jaiscript/jvm/vm_class.hpp"
#include "jaiscript/jvm/vm_class_integration.hpp"

// Base JaiScript headers
#include "jaiscript/class_system.hpp"
#include "jaiscript/jvm/vm.hpp"

namespace jai {
namespace jvm {

/**
 * Easy initialization function for VM class system
 * 
 * @param executor The VM executor to add class support to
 * @param compiler The VM compiler to add class compilation to
 * @return Integration object for further customization
 */
inline std::unique_ptr<vm_class_system_integration> enable_vm_class_system(
    vm_executor& executor,
    vm_compiler& compiler
) {
    auto integration = std::make_unique<vm_class_system_integration>(executor, compiler);
    integration->initialize();
    return integration;
}

/**
 * Enable full integration between interpreter and VM class systems
 * 
 * @param js The JaiScript engine
 * @param executor The VM executor
 * @param compiler The VM compiler
 * @return Pair of integration objects (interpreter, VM)
 */
inline std::pair<std::unique_ptr<class_system_integration>, 
                 std::unique_ptr<vm_class_system_integration>>
enable_full_class_system(
    engine& js,
    vm_executor& executor,
    vm_compiler& compiler
) {
    // Enable interpreter classes
    auto interpreter_classes = enable_class_system(js);
    
    // Enable VM classes
    auto vm_classes = enable_vm_class_system(executor, compiler);
    
    // Enable cross-calling
    vm_classes->integrate_with_interpreter(interpreter_classes.get());
    
    return {std::move(interpreter_classes), std::move(vm_classes)};
}

/**
 * VM-specific class registration macro
 * 
 * Example:
 *   VM_REGISTER_CLASS(Point)
 *     .field("x", &Point::x)
 *     .field("y", &Point::y)
 *     .method("distance", &Point::distance)
 *     .optimize_field_access()
 *     .inline_method("distance");
 */
#define VM_REGISTER_CLASS(ClassName) \
    jaiscript::jvm::vm_optimized_dynamic_binder<ClassName>(js, #ClassName)

/**
 * Performance macros for VM class operations
 */

// Fast field access by compile-time index
#define VM_FIELD_INDEX(ClassName, FieldName) \
    jaiscript::jvm::vm_field_index<ClassName>::FieldName

// Optimized method call with caching
#define VM_CALL_METHOD_CACHED(instance, method_name, cache_id, ...) \
    jaiscript::jvm::vm_call_cached(instance, method_name, cache_id, {__VA_ARGS__})

// Direct (non-virtual) method call when type is known
#define VM_CALL_METHOD_DIRECT(instance, ClassName, method_name, ...) \
    static_cast<ClassName*>(instance.get())->method_name(__VA_ARGS__)

// Batch field initialization
#define VM_INIT_FIELDS(instance, ...) \
    jaiscript::jvm::vm_class_utils::initialize_fields_batch(instance, {__VA_ARGS__})

/**
 * VM class bytecode builder for direct bytecode generation
 */
class vm_class_bytecode_builder {
public:
    explicit vm_class_bytecode_builder(const std::string& class_name);
    
    // Define class structure
    vm_class_bytecode_builder& set_base_class(const std::string& base_name);
    vm_class_bytecode_builder& add_field(const std::string& name, const vm_value& default_val = {});
    vm_class_bytecode_builder& add_method(const std::string& name, const std::vector<uint8_t>& bytecode);
    vm_class_bytecode_builder& add_constructor(const std::vector<uint8_t>& bytecode);
    vm_class_bytecode_builder& add_destructor(const std::vector<uint8_t>& bytecode);
    
    // Constructor delegation
    vm_class_bytecode_builder& add_delegating_constructor(
        const std::vector<uint8_t>& bytecode,
        delegation_type type,
        const std::vector<vm_value>& args
    );
    
    // Optimization hints
    vm_class_bytecode_builder& mark_method_final(const std::string& name);
    vm_class_bytecode_builder& mark_method_inline(const std::string& name);
    vm_class_bytecode_builder& enable_fast_fields();
    
    // Build the bytecode
    std::vector<uint8_t> build() const;
    
private:
    std::string class_name_;
    std::string base_class_name_;
    std::vector<std::pair<std::string, vm_value>> fields_;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> methods_;
    std::vector<std::vector<uint8_t>> constructors_;
    std::unique_ptr<std::vector<uint8_t>> destructor_;
    vm_optimization_hints hints_;
};

/**
 * Example VM class usage patterns
 */

// Example 1: High-performance numeric class
inline void example_vm_vector_class(vm_compiler& compiler) {
    vm_class_bytecode_builder builder("Vector2D");
    
    builder
        .add_field("x", vm_value::make_float(0.0f))
        .add_field("y", vm_value::make_float(0.0f))
        .enable_fast_fields()
        .add_constructor({
            // Constructor bytecode: this.x = arg0; this.y = arg1;
            static_cast<uint8_t>(opcode::load_arg), 0,  // Load arg0
            static_cast<uint8_t>(class_opcode::set_field_fast), 0, 0,  // Set field 0 (x)
            static_cast<uint8_t>(opcode::load_arg), 1,  // Load arg1
            static_cast<uint8_t>(class_opcode::set_field_fast), 0, 1,  // Set field 1 (y)
            static_cast<uint8_t>(opcode::return_value)
        })
        .add_method("length", {
            // Method bytecode: return sqrt(x*x + y*y)
            static_cast<uint8_t>(class_opcode::get_field_fast), 0, 0,  // Get x
            static_cast<uint8_t>(opcode::dup),  // Duplicate x
            static_cast<uint8_t>(opcode::multiply),  // x * x
            static_cast<uint8_t>(class_opcode::get_field_fast), 0, 1,  // Get y
            static_cast<uint8_t>(opcode::dup),  // Duplicate y
            static_cast<uint8_t>(opcode::multiply),  // y * y
            static_cast<uint8_t>(opcode::add),  // x*x + y*y
            static_cast<uint8_t>(opcode::sqrt),  // sqrt(x*x + y*y)
            static_cast<uint8_t>(opcode::return_value)
        })
        .mark_method_inline("length")
        .mark_method_final("length");
    
    auto bytecode = builder.build();
    compiler.compile_bytecode(bytecode);
}

// Example 2: Inheritance with virtual dispatch
inline void example_vm_inheritance(vm_compiler& compiler) {
    // Base class
    vm_class_bytecode_builder shape_builder("Shape");
    shape_builder
        .add_method("area", {
            // Virtual method - returns 0.0 by default
            static_cast<uint8_t>(opcode::push_float), // Push 0.0
            0, 0, 0, 0,  // Float bytes
            static_cast<uint8_t>(opcode::return_value)
        });
    
    compiler.compile_bytecode(shape_builder.build());
    
    // Derived class
    vm_class_bytecode_builder circle_builder("Circle");
    circle_builder
        .set_base_class("Shape")
        .add_field("radius", vm_value::make_float(1.0f))
        .add_method("area", {
            // Override: return PI * radius * radius
            static_cast<uint8_t>(opcode::push_float), // Push PI
            0x40, 0x49, 0x0f, 0xdb,  // 3.14159...
            static_cast<uint8_t>(class_opcode::get_field_fast), 0, 0,  // Get radius
            static_cast<uint8_t>(opcode::multiply),  // PI * radius
            static_cast<uint8_t>(class_opcode::get_field_fast), 0, 0,  // Get radius again
            static_cast<uint8_t>(opcode::multiply),  // PI * radius * radius
            static_cast<uint8_t>(opcode::return_value)
        });
    
    compiler.compile_bytecode(circle_builder.build());
}

// Example 3: Constructor delegation
inline void example_vm_constructor_delegation(vm_compiler& compiler) {
    vm_class_bytecode_builder builder("Rectangle");
    
    builder
        .add_field("width", vm_value::make_float(0.0f))
        .add_field("height", vm_value::make_float(0.0f))
        // Main constructor
        .add_constructor({
            static_cast<uint8_t>(opcode::load_arg), 0,
            static_cast<uint8_t>(class_opcode::set_field_fast), 0, 0,
            static_cast<uint8_t>(opcode::load_arg), 1,
            static_cast<uint8_t>(class_opcode::set_field_fast), 0, 1,
            static_cast<uint8_t>(opcode::return_value)
        })
        // Delegating constructor (square)
        .add_delegating_constructor(
            {static_cast<uint8_t>(opcode::return_value)},  // Empty body
            delegation_type::same_class,
            {vm_value::make_float(0.0f), vm_value::make_float(0.0f)}  // Delegate args
        );
    
    compiler.compile_bytecode(builder.build());
}


} // namespace jvm
} // namespace jai