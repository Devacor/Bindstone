#pragma once

#include "bytecode.hpp"
#include "../detail/ast.hpp"
#include "../detail/lexer.hpp"
#include "../detail/parser.hpp"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <stack>

namespace jai {
namespace jvm {

    // Compilation statistics
    struct compilation_stats {
        size_t functions_compiled = 0;
        size_t instructions_generated = 0;
        size_t optimizations_applied = 0;
        double compilation_time_ms = 0.0;
    };

    // Bytecode compiler - converts AST to bytecode
    class compiler {
    public:
        compiler();
        explicit compiler(const compilation_options& options);
        ~compiler();
        
        // Non-copyable, moveable
        compiler(const compiler&) = delete;
        compiler& operator=(const compiler&) = delete;
        compiler(compiler&&) noexcept;
        compiler& operator=(compiler&&) noexcept;
        
        // Compilation interface
        std::unique_ptr<module> compile(const std::vector<declaration_ptr>& declarations);
        std::unique_ptr<module> compile_script(const std::string& source_code, const std::string& filename = "");
        std::unique_ptr<module> compile_file(const std::string& filename);
        
        // Configuration
        void set_optimization_level(optimization_level level);
        void set_debug_info_enabled(bool enabled);
        void set_type_checking_enabled(bool enabled);
        
        // Error handling
        bool has_errors() const;
        std::vector<std::string> get_errors() const;
        void clear_errors();
        
        // Statistics
        compilation_stats get_stats() const;
        
    private:
        struct implementation;
        std::unique_ptr<implementation> impl_;
        
        // Core compilation methods
        void compile_declarations(const std::vector<declaration_ptr>& declarations);
        void compile_declaration(declaration_ptr decl);
        void compile_declaration(declaration_ptr decl, bool is_last);
        void compile_function_decl(function_decl* decl);
        void compile_variable_decl(variable_decl* decl);
        void compile_class_decl(class_decl* decl);
        void compile_expression_decl(expression_decl* decl);
        void compile_expression_decl(expression_decl* decl, bool is_last);
        
        // Statement compilation
        void compile_statement(statement_ptr stmt);
        void compile_statement(statement_ptr stmt, bool is_last_in_main);
        void compile_block_stmt(block_stmt* stmt);
        void compile_expression_stmt(expression_stmt* stmt);
        void compile_expression_stmt(expression_stmt* stmt, bool is_last_in_main);
        void compile_if_stmt(if_stmt* stmt);
        void compile_while_stmt(while_stmt* stmt);
        void compile_for_stmt(for_stmt* stmt);
        void compile_range_for_stmt(range_for_stmt* stmt);
        void compile_return_stmt(return_stmt* stmt);
        void compile_break_stmt(break_stmt* stmt);
        void compile_continue_stmt(continue_stmt* stmt);
        
        // Expression compilation
        void compile_expression(expression_ptr expr);
        void compile_literal_expr(literal_expr* expr);
        void compile_identifier_expr(identifier_expr* expr);
        void compile_binary_expr(binary_expr* expr);
        void compile_unary_expr(unary_expr* expr);
        void compile_assignment_expr(assignment_expr* expr);
        void compile_call_expr(call_expr* expr);
        void compile_member_expr(member_expr* expr);
        void compile_lambda_expr(lambda_expr* expr);
        void compile_new_expr(new_expr* expr);
        void compile_ternary_expr(ternary_expr* expr);
        void compile_array_literal_expr(array_literal_expr* expr);
        void compile_map_literal_expr(map_literal_expr* expr);
        
        // Optimization passes
        void optimize_function(function* func);
        void optimize_constant_folding(function* func);
        void optimize_dead_code_elimination(function* func);
        void optimize_peephole(function* func);
        void optimize_register_allocation(function* func);
        
        // Code generation helpers
        void emit(opcode op);
        void emit(opcode op, uint8_t operand);
        void emit(opcode op, uint16_t operand);
        void emit(opcode op, uint32_t operand);
        void emit(opcode op, uint64_t operand);
        void emit(opcode op, double operand);
        
        size_t emit_jump(opcode op);
        void patch_jump(size_t jump_offset);
        void emit_loop(size_t loop_start);
        void emit_builtin_call(uint16_t builtin_index, uint8_t arg_count);
        
        // Constant management
        uint16_t add_string_constant(const std::string& str);
        uint16_t add_value_constant(const script_value& value);
        
        // Variable management
        struct local_variable {
            std::string name;
            uint8_t slot;
            value_type type;
            bool is_captured;
            size_t scope_depth;
        };
        
        uint8_t declare_local(const std::string& name, value_type type = value_type::jai_null_type);
        uint8_t resolve_local(const std::string& name);
        uint16_t resolve_global(const std::string& name);
        uint8_t resolve_upvalue(const std::string& name);
        
        // Scope management
        void begin_scope();
        void end_scope();
        
        // Loop management
        struct loop_info {
            size_t start_offset;
            std::vector<size_t> break_jumps;
            std::vector<size_t> continue_jumps;
        };
        void begin_loop();
        void end_loop();
        void emit_break();
        void emit_continue();
        
        // Function compilation context
        struct function_context {
            std::unique_ptr<function> func;
            std::vector<local_variable> locals;
            std::vector<loop_info> loops;
            size_t scope_depth;
            bool is_in_function;
            
            function_context(const std::string& name);
        };
        
        void begin_function(const std::string& name);
        std::unique_ptr<function> end_function();
        
        // Error reporting
        void error(const std::string& message);
        void error(const std::string& message, const source_location& location);
        void warning(const std::string& message);
        
        // Type inference and checking
        value_type infer_expression_type(expression_ptr expr);
        void check_type_compatibility(value_type expected, value_type actual, const source_location& location);
        
        // Built-in function handling
        bool is_builtin_function(const std::string& name) const;
        uint16_t get_builtin_function_index(const std::string& name) const;
    };
    
    // Compilation result with module and any errors
    struct compilation_result {
        std::unique_ptr<module> compiled_module;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        compilation_stats stats;
        
        bool is_successful() const { return compiled_module && errors.empty(); }
    };
    
    // High-level compilation functions
    compilation_result compile_script(const std::string& source_code, 
                                    const std::string& filename = "",
                                    const compilation_options& options = {});
                                    
    compilation_result compile_file(const std::string& filename,
                                  const compilation_options& options = {});
    
    // Utility functions for working with bytecode
    std::string disassemble_instruction(const instruction& instr, const module* mod = nullptr);
    std::string disassemble_function(const function& func, const module* mod = nullptr);
    std::string disassemble_module(const module& mod);
    
    // Bytecode verification (for debugging)
    bool verify_bytecode(const function& func);
    bool verify_module(const module& mod);
    
    // Bytecode optimization utilities
    void optimize_module(module* mod, optimization_level level = optimization_level::STANDARD);
    
} // namespace jvm
} // namespace jai