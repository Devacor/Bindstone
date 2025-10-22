#include "jaiscript/jvm/vm_function_integration.hpp"
#include "jaiscript/detail/parser.hpp"
#include <chrono>
#include <sstream>

namespace jai {
namespace jvm {


// vm_function_system_integration implementation

vm_function_system_integration::vm_function_system_integration(
    engine& js_engine,
    vm_executor& executor,
    vm_compiler& compiler
) : engine_(js_engine), executor_(executor), compiler_(compiler) {
    function_executor_ = std::make_unique<vm_function_executor>(executor);
    function_compiler_ = std::make_unique<vm_function_compiler>(compiler);
}

void vm_function_system_integration::initialize() {
    register_function_opcodes();
    enable_function_compilation();
    enable_cross_backend_calls();
    register_optimizations();
    
    // Set registry in executor
    function_executor_->set_registry(&registry_);
}

void vm_function_system_integration::register_function_opcodes() {
    // Register all function-related opcodes
    executor_.register_opcode_handler(
        static_cast<uint8_t>(function_opcode::define_function),
        [this](vm_context& ctx) {
            return function_executor_->execute_function_opcode(
                function_opcode::define_function, ctx
            );
        }
    );
    
    executor_.register_opcode_handler(
        static_cast<uint8_t>(function_opcode::call_function),
        [this](vm_context& ctx) {
            return function_executor_->execute_function_opcode(
                function_opcode::call_function, ctx
            );
        }
    );
    
    executor_.register_opcode_handler(
        static_cast<uint8_t>(function_opcode::call_function_fast),
        [this](vm_context& ctx) {
            return function_executor_->execute_function_opcode(
                function_opcode::call_function_fast, ctx
            );
        }
    );
    
    executor_.register_opcode_handler(
        static_cast<uint8_t>(function_opcode::return_value),
        [this](vm_context& ctx) {
            return function_executor_->execute_function_opcode(
                function_opcode::return_value, ctx
            );
        }
    );
    
    // Register all other function opcodes...
    // (In full implementation, would register all opcodes)
}

void vm_function_system_integration::enable_function_compilation() {
    // Hook into the compiler to compile functions to bytecode
    compiler_.set_function_compiler(function_compiler_.get());
    
    // Set up function definition interception
    compiler_.add_pre_compilation_pass(
        [this](ast_node* ast) {
            // Visit AST to find function definitions
            vm_function_definition_visitor visitor(registry_, compiler_);
            ast->accept(visitor);
            
            // Register compiled functions
            for (const auto& func : visitor.get_compiled_functions()) {
                registry_.register_function(func);
                
                // Also register in interpreter for cross-backend calls
                auto wrapper = vm_function_bridge::create_interpreter_wrapper(func);
                engine_.add_function(func->name, wrapper);
            }
        }
    );
}

void vm_function_system_integration::enable_cross_backend_calls() {
    // Allow VM to call interpreter functions
    executor_.set_interpreter_function_resolver(
        [this](const std::string& name) -> script_function {
            return engine_.find_function(name);
        }
    );
    
    // Allow interpreter to call VM functions
    engine_.set_vm_function_resolver(
        [this](const std::string& name) -> script_value {
            auto func = registry_.find_function(name);
            if (func) {
                auto wrapper = vm_function_bridge::create_interpreter_wrapper(func);
                return script_value::make_function(wrapper, engine_.weak_from_this());
            }
            return script_value(std::monostate{}, engine_.weak_from_this());
        }
    );
}

void vm_function_system_integration::register_optimizations() {
    register_function_inlining();
    register_tail_call_optimization();
    register_closure_optimization();
}

void vm_function_system_integration::register_function_inlining() {
    compiler_.add_optimization_pass(
        [this](vm_bytecode& bytecode) {
            // Scan for function calls that can be inlined
            for (size_t i = 0; i < bytecode.code.size(); ++i) {
                if (bytecode.code[i] == static_cast<uint8_t>(function_opcode::call_function_fast)) {
                    uint16_t func_index = (bytecode.code[i + 1] << 8) | bytecode.code[i + 2];
                    auto func = registry_.get_by_index(func_index);
                    
                    if (func && vm_function_utils::should_inline(func.get())) {
                        vm_function_utils::inline_function(bytecode.code, i, func.get());
                    }
                }
            }
        }
    );
}

void vm_function_system_integration::register_tail_call_optimization() {
    compiler_.add_optimization_pass(
        [this](vm_bytecode& bytecode) {
            // Scan for tail call opportunities
            for (auto& func : registry_.get_all_functions()) {
                vm_function_utils::optimize_tail_call(func->bytecode, func.get());
            }
        }
    );
}

void vm_function_system_integration::register_closure_optimization() {
    compiler_.add_optimization_pass(
        [this](vm_bytecode& bytecode) {
            // Optimize closure creation and capture access
            // This would analyze capture patterns and optimize accordingly
        }
    );
}

// vm_aware_engine implementation

vm_aware_engine::vm_aware_engine() : engine() {
    vm_executor_ = std::make_unique<vm_executor>();
    vm_compiler_ = std::make_unique<vm_compiler>();
    
    function_integration_ = std::make_unique<vm_function_system_integration>(
        *this, *vm_executor_, *vm_compiler_
    );
    
    function_integration_->initialize();
}

script_value vm_aware_engine::execute(const std::string& script) {
    // Check if script should use VM
    if (compile_functions_to_bytecode_ && 
        vm_function_integration_utils::contains_function_definitions(script)) {
        return execute_with_vm(script);
    }
    
    // Fall back to base engine execution
    return engine::execute(script);
}

script_value vm_aware_engine::execute_with_interpreter(const std::string& script) {
    // Force interpreter execution
    auto saved_setting = compile_functions_to_bytecode_;
    compile_functions_to_bytecode_ = false;
    
    auto result = engine::execute(script);
    
    compile_functions_to_bytecode_ = saved_setting;
    return result;
}

script_value vm_aware_engine::execute_with_vm(const std::string& script) {
    // Parse script
    parser p(script);
    auto ast = p.parse();
    
    // Compile to bytecode
    vm_compiler_->compile(ast.get());
    auto bytecode = vm_compiler_->get_bytecode();
    
    // Execute bytecode
    vm_context ctx;
    vm_executor_->execute(bytecode, ctx);
    
    // Get result
    if (!ctx.empty()) {
        return vm_class_bridge::to_script_value(ctx.pop());
    }
    
    // TODO: Get engine reference from context
    return script_value(std::monostate{}, std::weak_ptr<engine>{});
}

// vm_function_definition_visitor implementation

vm_function_definition_visitor::vm_function_definition_visitor(
    vm_function_registry& registry,
    vm_compiler& compiler
) : registry_(registry), compiler_(compiler) {
}

void vm_function_definition_visitor::visit_function_decl(function_decl* decl) {
    // Compile function to bytecode
    vm_function_compiler func_compiler(compiler_);
    auto vm_func = func_compiler.compile_function(decl);
    
    // Register in VM registry
    registry_.register_function(vm_func);
    compiled_functions_.push_back(vm_func);
    
}

void vm_function_definition_visitor::visit_lambda_expr(lambda_expr* expr) {
    // Compile lambda to bytecode
    vm_function_compiler func_compiler(compiler_);
    auto vm_func = func_compiler.compile_lambda(expr);
    
    // Lambdas are registered differently (as values)
    compiled_functions_.push_back(vm_func);
}

// vm_function_profiler implementation

void vm_function_profiler::start_profiling() {
    profiling_enabled_ = true;
    profiles_.clear();
}

void vm_function_profiler::stop_profiling() {
    profiling_enabled_ = false;
    update_hot_functions();
}

void vm_function_profiler::record_function_call(
    const std::string& name,
    uint64_t cycles,
    bool was_bytecode
) {
    if (!profiling_enabled_) return;
    
    auto& profile = profiles_[name];
    profile.name = name;
    profile.total_calls++;
    profile.total_cycles += cycles;
    
    if (was_bytecode) {
        profile.bytecode_calls++;
    } else {
        profile.interpreter_fallbacks++;
    }
    
    profile.average_cycles_per_call = 
        static_cast<double>(profile.total_cycles) / profile.total_calls;
}

std::vector<vm_function_profiler::function_profile> 
vm_function_profiler::get_hot_functions(uint64_t threshold) const {
    std::vector<function_profile> hot_functions;
    
    for (const auto& [name, profile] : profiles_) {
        if (profile.total_calls >= threshold || profile.is_hot_function) {
            hot_functions.push_back(profile);
        }
    }
    
    // Sort by total calls
    std::sort(hot_functions.begin(), hot_functions.end(),
        [](const auto& a, const auto& b) {
            return a.total_calls > b.total_calls;
        }
    );
    
    return hot_functions;
}

std::string vm_function_profiler::generate_report() const {
    std::stringstream report;
    report << "VM Function Performance Report\n";
    report << "==============================\n\n";
    
    auto hot_functions = get_hot_functions(100);
    
    for (const auto& profile : hot_functions) {
        report << "Function: " << profile.name << "\n";
        report << "  Total Calls: " << profile.total_calls << "\n";
        report << "  Bytecode Calls: " << profile.bytecode_calls 
               << " (" << (profile.bytecode_calls * 100.0 / profile.total_calls) << "%)\n";
        report << "  Interpreter Fallbacks: " << profile.interpreter_fallbacks << "\n";
        report << "  Average Cycles: " << profile.average_cycles_per_call << "\n";
        if (profile.is_hot_function) {
            report << "  [HOT FUNCTION]\n";
        }
        report << "\n";
    }
    
    return report.str();
}

std::vector<std::string> vm_function_profiler::get_optimization_candidates() const {
    std::vector<std::string> candidates;
    
    for (const auto& [name, profile] : profiles_) {
        // Functions with high interpreter fallback rate
        if (profile.interpreter_fallbacks > profile.bytecode_calls * 0.1) {
            candidates.push_back(name + " (high fallback rate)");
        }
        
        // Very hot functions that might benefit from JIT
        if (profile.total_calls > 10000 && profile.average_cycles_per_call > 1000) {
            candidates.push_back(name + " (JIT candidate)");
        }
    }
    
    return candidates;
}

void vm_function_profiler::update_hot_functions() {
    for (auto& [name, profile] : profiles_) {
        if (profile.total_calls > 1000 || 
            profile.average_cycles_per_call < 100) {
            profile.is_hot_function = true;
        }
    }
}

// vm_function_integration_utils implementation

namespace vm_function_integration_utils {

bool contains_function_definitions(const std::string& script) {
    // Simple heuristic - look for function keywords
    return script.find("auto") != std::string::npos && 
           script.find("->") != std::string::npos;
}

std::vector<std::string> extract_function_definitions(const std::string& script) {
    std::vector<std::string> functions;
    
    // Parse script and extract function definitions
    // This is a simplified implementation
    size_t pos = 0;
    while ((pos = script.find("auto", pos)) != std::string::npos) {
        size_t end = script.find("}", pos);
        if (end != std::string::npos) {
            functions.push_back(script.substr(pos, end - pos + 1));
        }
        pos = end;
    }
    
    return functions;
}

std::shared_ptr<vm_bytecode_function> compile_function_string(
    const std::string& func_code,
    vm_compiler& compiler
) {
    // Parse function code
    parser p(func_code);
    auto ast = p.parse();
    
    // Find function declaration in AST
    vm_function_definition_visitor visitor(compiler.get_function_registry(), compiler);
    ast->accept(visitor);
    
    auto compiled = visitor.get_compiled_functions();
    return compiled.empty() ? nullptr : compiled[0];
}

benchmark_result benchmark_function(
    const std::string& func_name,
    const std::vector<script_value>& test_args,
    engine& engine,
    size_t iterations
) {
    benchmark_result result;
    
    // Benchmark interpreter execution
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        engine.call_function(func_name, test_args);
    }
    auto end = std::chrono::high_resolution_clock::now();
    result.interpreter_cycles = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end - start
    ).count();
    
    // Benchmark bytecode execution (if available)
    if (auto vm_engine = dynamic_cast<vm_aware_engine*>(&engine)) {
        auto& registry = vm_engine->get_function_registry();
        auto vm_func = registry.find_function(func_name);
        
        if (vm_func) {
            vm_context ctx;
            std::vector<vm_value> vm_args;
            for (const auto& arg : test_args) {
                vm_args.push_back(vm_class_bridge::to_vm_value(arg));
            }
            
            start = std::chrono::high_resolution_clock::now();
            for (size_t i = 0; i < iterations; ++i) {
                vm_func->execute(ctx, vm_args);
            }
            end = std::chrono::high_resolution_clock::now();
            result.bytecode_cycles = std::chrono::duration_cast<std::chrono::nanoseconds>(
                end - start
            ).count();
            
            result.speedup_factor = static_cast<double>(result.interpreter_cycles) / 
                                   result.bytecode_cycles;
        }
    }
    
    return result;
}

} // namespace vm_function_integration_utils


} // namespace jvm
} // namespace jai