#pragma once

#include "jaiscript/jvm/vm_class.hpp"
#include "jaiscript/jvm/vm_executor.hpp"
#include "jaiscript/jvm/vm_compiler.hpp"
#include "jaiscript/core/class_integration.hpp"
#include <unordered_set>
#include <string>
#include <unordered_map>

namespace jai {
namespace jvm {

// VM class system integration - bridges VM and interpreter class systems
class vm_class_system_integration {
public:
    explicit vm_class_system_integration(vm_executor& executor, vm_compiler& compiler);
    
    // Initialize VM class system
    void initialize();
    
    // Register VM-specific opcodes
    void register_class_opcodes();
    
    // Integration with interpreter class system
    void integrate_with_interpreter(class_system_integration* interpreter_integration);
    
    // Enable cross-calling between VM and interpreter classes
    void enable_cross_calling();
    
    // Register built-in VM optimizations
    void register_optimizations();
    
    // Hot reload support
    void enable_hot_reload();
    
private:
    vm_executor& executor_;
    vm_compiler& compiler_;
    std::unique_ptr<vm_class_executor> class_executor_;
    std::unique_ptr<vm_class_compiler> class_compiler_;
    class_system_integration* interpreter_integration_ = nullptr;
    
    // Register individual optimizations
    void register_inline_caching();
    void register_field_access_optimization();
    void register_method_devirtualization();
    void register_constructor_inlining();
};

// VM-optimized class builder for maximum performance
template<typename T>
class vm_optimized_dynamic_binder : public enhanced_dynamic_binder<T> {
public:
    using enhanced_dynamic_binder<T>::enhanced_dynamic_binder;
    
    // Mark methods for aggressive inlining
    vm_optimized_dynamic_binder& inline_method(const std::string& name) {
        inline_hints_.insert(name);
        return *this;
    }
    
    // Mark methods as non-virtual (final) for devirtualization
    vm_optimized_dynamic_binder& final_method(const std::string& name) {
        final_methods_.insert(name);
        return *this;
    }
    
    // Enable field access optimization
    vm_optimized_dynamic_binder& optimize_field_access() {
        optimize_fields_ = true;
        return *this;
    }
    
    // Build with VM optimizations
    void build() {
        enhanced_dynamic_binder<T>::build();
        apply_vm_optimizations();
    }
    
private:
    std::unordered_set<std::string> inline_hints_;
    std::unordered_set<std::string> final_methods_;
    bool optimize_fields_ = false;
    
    void apply_vm_optimizations();
};

// VM class performance profiler
class vm_class_profiler {
public:
    struct method_stats {
        uint64_t call_count = 0;
        uint64_t total_cycles = 0;
        uint64_t cache_hits = 0;
        uint64_t cache_misses = 0;
        bool is_hot = false;
    };
    
    struct class_stats {
        std::unordered_map<std::string, method_stats> methods;
        uint64_t instance_count = 0;
        uint64_t total_allocations = 0;
    };
    
    // Start/stop profiling
    void start_profiling();
    void stop_profiling();
    
    // Get statistics
    const class_stats& get_class_stats(const std::string& class_name) const;
    std::vector<std::string> get_hot_methods() const;
    
    // Record events
    void record_method_call(const std::string& class_name, const std::string& method_name);
    void record_cache_hit(const std::string& class_name, const std::string& method_name);
    void record_cache_miss(const std::string& class_name, const std::string& method_name);
    void record_instance_creation(const std::string& class_name);
    
    // Generate optimization report
    std::string generate_optimization_report() const;
    
private:
    std::unordered_map<std::string, class_stats> stats_;
    bool profiling_enabled_ = false;
    
    // Hot method detection
    void update_hot_methods();
};

// Global profiler instance
extern vm_class_profiler g_class_profiler;

// VM class optimization hints
struct vm_optimization_hints {
    // Methods that should be inlined
    std::unordered_set<std::string> inline_methods;
    
    // Methods that are guaranteed non-virtual
    std::unordered_set<std::string> final_methods;
    
    // Classes that should use fast field access
    std::unordered_set<std::string> optimize_field_classes;
    
    // Constructor delegation patterns to optimize
    std::map<std::string, std::vector<uint16_t>> constructor_patterns;
    
    // Apply hints to a class definition
    void apply_to_class(vm_class_definition* class_def);
};

// JIT compilation support for hot methods
class vm_class_jit {
public:
    // Check if method is eligible for JIT
    bool should_jit_compile(
        const std::string& class_name,
        const std::string& method_name
    ) const;
    
    // JIT compile a method
    void jit_compile_method(
        vm_class_definition* class_def,
        const std::string& method_name
    );
    
    // Execute JIT-compiled method
    bool execute_jit_method(
        vm_class_instance* instance,
        const std::string& method_name,
        vm_context& ctx
    );
    
private:
    struct jit_method {
        void* native_code = nullptr;
        size_t code_size = 0;
    };
    
    std::unordered_map<std::string, jit_method> jit_cache_;
    
    // JIT compilation threshold
    static constexpr uint64_t JIT_THRESHOLD = 10000;
};

// Utility functions for VM class operations
namespace vm_class_utils {
    // Fast instance creation with pre-allocated fields
    std::shared_ptr<vm_class_instance> create_instance_fast(
        const std::string& class_name,
        size_t field_count
    );
    
    // Batch field initialization
    void initialize_fields_batch(
        vm_class_instance* instance,
        const std::vector<std::pair<uint16_t, vm_value>>& field_values
    );
    
    // Fast method lookup with hint
    vm_class_definition::vtable_entry* lookup_method_fast(
        vm_class_definition* class_def,
        const std::string& method_name,
        uint32_t hint = 0
    );
    
    // Optimize class for specific usage pattern
    void optimize_for_pattern(
        vm_class_definition* class_def,
        const std::string& pattern  // "field_heavy", "method_heavy", "inheritance"
    );
}

// Macros for VM class operations
#define VM_INVOKE_METHOD(instance, method, ...) \
    vm_class_utils::invoke_optimized(instance, #method, {__VA_ARGS__})

#define VM_GET_FIELD_FAST(instance, field_index) \
    static_cast<vm_class_instance*>(instance.get())->get_field_by_index(field_index)

#define VM_SET_FIELD_FAST(instance, field_index, value) \
    static_cast<vm_class_instance*>(instance.get())->get_field_by_index(field_index) = value

// VM class compilation flags
enum class vm_class_compile_flags : uint32_t {
    none = 0,
    optimize_fields = 1 << 0,
    optimize_methods = 1 << 1,
    inline_constructors = 1 << 2,
    devirtualize_methods = 1 << 3,
    enable_jit = 1 << 4,
    profile_enabled = 1 << 5,
    
    // Preset combinations
    default_flags = optimize_fields | optimize_methods,
    performance_flags = optimize_fields | optimize_methods | inline_constructors | devirtualize_methods,
    maximum_performance = performance_flags | enable_jit
};

inline vm_class_compile_flags operator|(vm_class_compile_flags a, vm_class_compile_flags b) {
    return static_cast<vm_class_compile_flags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline vm_class_compile_flags operator&(vm_class_compile_flags a, vm_class_compile_flags b) {
    return static_cast<vm_class_compile_flags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

} // namespace jvm
} // namespace jai