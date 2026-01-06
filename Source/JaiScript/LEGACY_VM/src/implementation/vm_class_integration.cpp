#include "jaiscript/jvm/vm_class_integration.hpp"
#include "jaiscript/core/class_registry.hpp"
#include <chrono>
#include <algorithm>

namespace jaiscript {
namespace jvm {

// Global profiler instance
vm_class_profiler g_class_profiler;

// vm_class_system_integration implementation

vm_class_system_integration::vm_class_system_integration(vm_executor& executor, vm_compiler& compiler)
    : executor_(executor), compiler_(compiler) {
    class_executor_ = std::make_unique<vm_class_executor>(executor);
    class_compiler_ = std::make_unique<vm_class_compiler>(compiler);
}

void vm_class_system_integration::initialize() {
    register_class_opcodes();
    register_optimizations();
    enable_cross_calling();
}

void vm_class_system_integration::register_class_opcodes() {
    // Register all class-related opcodes with the executor
    executor_.register_opcode_handler(
        static_cast<uint8_t>(class_opcode::define_class),
        [this](vm_context& ctx) {
            return class_executor_->execute_class_opcode(class_opcode::define_class, ctx);
        }
    );
    
    executor_.register_opcode_handler(
        static_cast<uint8_t>(class_opcode::new_instance),
        [this](vm_context& ctx) {
            return class_executor_->execute_class_opcode(class_opcode::new_instance, ctx);
        }
    );
    
    executor_.register_opcode_handler(
        static_cast<uint8_t>(class_opcode::invoke_method),
        [this](vm_context& ctx) {
            return class_executor_->execute_class_opcode(class_opcode::invoke_method, ctx);
        }
    );
    
    executor_.register_opcode_handler(
        static_cast<uint8_t>(class_opcode::get_field),
        [this](vm_context& ctx) {
            return class_executor_->execute_class_opcode(class_opcode::get_field, ctx);
        }
    );
    
    executor_.register_opcode_handler(
        static_cast<uint8_t>(class_opcode::set_field),
        [this](vm_context& ctx) {
            return class_executor_->execute_class_opcode(class_opcode::set_field, ctx);
        }
    );
    
    // Register fast field access opcodes
    executor_.register_opcode_handler(
        static_cast<uint8_t>(class_opcode::get_field_fast),
        [this](vm_context& ctx) {
            return class_executor_->execute_class_opcode(class_opcode::get_field_fast, ctx);
        }
    );
    
    executor_.register_opcode_handler(
        static_cast<uint8_t>(class_opcode::set_field_fast),
        [this](vm_context& ctx) {
            return class_executor_->execute_class_opcode(class_opcode::set_field_fast, ctx);
        }
    );
    
    // Register all other class opcodes...
    // (In a full implementation, would register all opcodes)
}

void vm_class_system_integration::integrate_with_interpreter(class_system_integration* interpreter_integration) {
    interpreter_integration_ = interpreter_integration;
}

void vm_class_system_integration::enable_cross_calling() {
    // Enable VM classes to be called from interpreter
    if (interpreter_integration_) {
        // Register VM class factory with interpreter
        interpreter_integration_->register_class_factory(
            [](const std::string& class_name) -> std::shared_ptr<script_class_instance> {
                auto vm_class = class_registry::instance().find_script_class(class_name);
                if (auto vm_def = std::dynamic_pointer_cast<vm_class_definition>(vm_class)) {
                    auto instance = std::make_shared<vm_class_instance>();
                    instance->class_name = class_name;
                    instance->class_def = vm_def;
                    return instance;
                }
                return nullptr;
            }
        );
    }
}

void vm_class_system_integration::register_optimizations() {
    register_inline_caching();
    register_field_access_optimization();
    register_method_devirtualization();
    register_constructor_inlining();
}

void vm_class_system_integration::enable_hot_reload() {
    // Clear caches on class redefinition
    class_registry::instance().add_redefinition_callback(
        [](const std::string& class_name) {
            g_method_cache.clear_cache();
        }
    );
}

void vm_class_system_integration::register_inline_caching() {
    // Inline caching is already implemented in the method dispatch
    // This would configure cache parameters
    
    // Set cache size limits, eviction policies, etc.
}

void vm_class_system_integration::register_field_access_optimization() {
    // Register compiler pass to optimize field access
    compiler_.add_optimization_pass(
        [this](vm_bytecode& bytecode) {
            // Scan for get_field/set_field and convert to get_field_fast/set_field_fast
            // when field indices can be determined at compile time
        }
    );
}

void vm_class_system_integration::register_method_devirtualization() {
    // Register compiler pass to devirtualize method calls
    compiler_.add_optimization_pass(
        [this](vm_bytecode& bytecode) {
            // Analyze method calls and convert invoke_virtual to invoke_direct
            // when the target class can be determined statically
        }
    );
}

void vm_class_system_integration::register_constructor_inlining() {
    // Register compiler pass to inline simple constructors
    compiler_.add_optimization_pass(
        [this](vm_bytecode& bytecode) {
            // Inline constructors that only set fields
        }
    );
}

// vm_class_profiler implementation

void vm_class_profiler::start_profiling() {
    profiling_enabled_ = true;
    stats_.clear();
}

void vm_class_profiler::stop_profiling() {
    profiling_enabled_ = false;
    update_hot_methods();
}

const vm_class_profiler::class_stats& vm_class_profiler::get_class_stats(const std::string& class_name) const {
    static class_stats empty_stats;
    auto it = stats_.find(class_name);
    return (it != stats_.end()) ? it->second : empty_stats;
}

std::vector<std::string> vm_class_profiler::get_hot_methods() const {
    std::vector<std::string> hot_methods;
    
    for (const auto& [class_name, class_stat] : stats_) {
        for (const auto& [method_name, method_stat] : class_stat.methods) {
            if (method_stat.is_hot) {
                hot_methods.push_back(class_name + "::" + method_name);
            }
        }
    }
    
    return hot_methods;
}

void vm_class_profiler::record_method_call(const std::string& class_name, const std::string& method_name) {
    if (!profiling_enabled_) return;
    
    auto& method_stat = stats_[class_name].methods[method_name];
    method_stat.call_count++;
    
    // Simple hot method detection
    if (method_stat.call_count > 1000) {
        method_stat.is_hot = true;
    }
}

void vm_class_profiler::record_cache_hit(const std::string& class_name, const std::string& method_name) {
    if (!profiling_enabled_) return;
    stats_[class_name].methods[method_name].cache_hits++;
}

void vm_class_profiler::record_cache_miss(const std::string& class_name, const std::string& method_name) {
    if (!profiling_enabled_) return;
    stats_[class_name].methods[method_name].cache_misses++;
}

void vm_class_profiler::record_instance_creation(const std::string& class_name) {
    if (!profiling_enabled_) return;
    auto& class_stat = stats_[class_name];
    class_stat.instance_count++;
    class_stat.total_allocations++;
}

std::string vm_class_profiler::generate_optimization_report() const {
    std::stringstream report;
    report << "VM Class Performance Report\n";
    report << "===========================\n\n";
    
    for (const auto& [class_name, class_stat] : stats_) {
        report << "Class: " << class_name << "\n";
        report << "  Instances: " << class_stat.instance_count << "\n";
        report << "  Total Allocations: " << class_stat.total_allocations << "\n";
        report << "  Methods:\n";
        
        // Sort methods by call count
        std::vector<std::pair<std::string, method_stats>> methods(
            class_stat.methods.begin(), class_stat.methods.end()
        );
        std::sort(methods.begin(), methods.end(),
            [](const auto& a, const auto& b) {
                return a.second.call_count > b.second.call_count;
            }
        );
        
        for (const auto& [method_name, method_stat] : methods) {
            report << "    " << method_name << ":\n";
            report << "      Calls: " << method_stat.call_count;
            if (method_stat.is_hot) {
                report << " [HOT]";
            }
            report << "\n";
            
            if (method_stat.cache_hits + method_stat.cache_misses > 0) {
                double hit_rate = static_cast<double>(method_stat.cache_hits) /
                                 (method_stat.cache_hits + method_stat.cache_misses) * 100.0;
                report << "      Cache Hit Rate: " << hit_rate << "%\n";
            }
        }
        report << "\n";
    }
    
    return report.str();
}

void vm_class_profiler::update_hot_methods() {
    // Additional hot method detection logic
    // Could use more sophisticated algorithms
}

// vm_optimization_hints implementation

void vm_optimization_hints::apply_to_class(vm_class_definition* class_def) {
    // Apply inline hints
    for (const auto& method_name : inline_methods) {
        auto it = class_def->methods.find(method_name);
        if (it != class_def->methods.end()) {
            // Mark method for inlining
            it->second.should_inline = true;
        }
    }
    
    // Apply final method hints
    for (const auto& method_name : final_methods) {
        auto it = class_def->methods.find(method_name);
        if (it != class_def->methods.end()) {
            // Mark method as non-virtual
            it->second.is_virtual = false;
            it->second.can_be_virtualized = false;
        }
    }
    
    // Apply field optimization hints
    if (optimize_field_classes.count(class_def->name)) {
        class_def->use_fast_field_access = true;
    }
}

// vm_class_jit implementation

bool vm_class_jit::should_jit_compile(
    const std::string& class_name,
    const std::string& method_name
) const {
    auto& stats = g_class_profiler.get_class_stats(class_name);
    auto it = stats.methods.find(method_name);
    
    if (it != stats.methods.end()) {
        return it->second.call_count >= JIT_THRESHOLD && it->second.is_hot;
    }
    
    return false;
}

void vm_class_jit::jit_compile_method(
    vm_class_definition* class_def,
    const std::string& method_name
) {
    // JIT compilation would happen here
    // This is a placeholder for the actual JIT compiler
    
    auto it = class_def->method_bytecodes.find(method_name);
    if (it != class_def->method_bytecodes.end()) {
        // Compile bytecode to native code
        jit_method jit;
        // jit.native_code = compile_to_native(it->second);
        // jit.code_size = ...;
        
        std::string key = class_def->name + "::" + method_name;
        jit_cache_[key] = jit;
    }
}

bool vm_class_jit::execute_jit_method(
    vm_class_instance* instance,
    const std::string& method_name,
    vm_context& ctx
) {
    std::string key = instance->class_name + "::" + method_name;
    auto it = jit_cache_.find(key);
    
    if (it != jit_cache_.end() && it->second.native_code) {
        // Execute native code
        // This would call the JIT-compiled function
        return true;
    }
    
    return false;
}

// vm_class_utils implementation

namespace vm_class_utils {

std::shared_ptr<vm_class_instance> create_instance_fast(
    const std::string& class_name,
    size_t field_count
) {
    auto instance = std::make_shared<vm_class_instance>();
    instance->class_name = class_name;
    instance->class_def = class_registry::instance().find_script_class(class_name);
    
    // Pre-allocate field storage
    instance->field_values.reserve(field_count);
    
    return instance;
}

void initialize_fields_batch(
    vm_class_instance* instance,
    const std::vector<std::pair<uint16_t, vm_value>>& field_values
) {
    for (const auto& [index, value] : field_values) {
        if (index < instance->field_values.size()) {
            instance->field_values[index] = value;
        }
    }
}

vm_class_definition::vtable_entry* lookup_method_fast(
    vm_class_definition* class_def,
    const std::string& method_name,
    uint32_t hint
) {
    // Try hint first (could be a hash or previous index)
    if (hint < class_def->vtable.size()) {
        auto& entry = class_def->vtable[hint];
        if (entry.method_name == method_name) {
            return &entry;
        }
    }
    
    // Fall back to regular lookup
    auto it = class_def->vtable_indices.find(method_name);
    if (it != class_def->vtable_indices.end()) {
        return &class_def->vtable[it->second];
    }
    
    return nullptr;
}

void optimize_for_pattern(
    vm_class_definition* class_def,
    const std::string& pattern
) {
    if (pattern == "field_heavy") {
        // Optimize for frequent field access
        class_def->use_fast_field_access = true;
        // Could also reorder fields for cache locality
    } else if (pattern == "method_heavy") {
        // Optimize for frequent method calls
        // Could pre-warm method cache
        for (const auto& [name, _] : class_def->methods) {
            class_def->get_method_for_dispatch(name, 0);
        }
    } else if (pattern == "inheritance") {
        // Optimize for deep inheritance hierarchies
        // Could flatten vtable or use other techniques
    }
}

} // namespace vm_class_utils

// Template implementations

template<typename T>
void vm_optimized_dynamic_binder<T>::apply_vm_optimizations() {
    auto class_def = this->class_def_;
    
    // Apply inline hints
    for (const auto& method_name : inline_hints_) {
        // Mark method for inlining in VM
    }
    
    // Apply final method optimizations
    for (const auto& method_name : final_methods_) {
        // Prevent virtualization
    }
    
    // Apply field optimizations
    if (optimize_fields_) {
        // Enable fast field access
    }
}

} // namespace jvm
} // namespace jaiscript