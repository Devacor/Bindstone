#include "../../include/jaiscript/jvm/compiler.hpp"
#include "../../include/jaiscript/detail/lexer.hpp"
#include "../../include/jaiscript/detail/parser.hpp"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cmath>

namespace jai {
namespace jvm {

    // Compiler implementation details
    struct compiler::implementation {
        // Compilation options
        compilation_options options;
        
        // Error collection
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        
        // Current compilation context
        std::unique_ptr<module> current_module;
        std::unique_ptr<function_context> current_function;
        
        // Statistics
        compilation_stats stats;
        std::chrono::high_resolution_clock::time_point compilation_start;
        
        implementation() {
            reset_for_new_compilation();
        }
        
        void reset_for_new_compilation() {
            errors.clear();
            warnings.clear();
            current_module = std::make_unique<module>();
            current_function = nullptr;
            stats = compilation_stats{};
        }
    };
    
    // Function compilation context implementation
    compiler::function_context::function_context(const std::string& name) 
        : func(std::make_unique<function>()), scope_depth(0), is_in_function(true) {
        func->name = name;
        func->local_count = 0;
        func->max_stack_size = 0;
        func->is_variadic = false;
        func->captures_upvalues = false;
    }
    
    compiler::compiler() : impl_(std::make_unique<implementation>()) {
    }
    
    compiler::compiler(const compilation_options& options) 
        : impl_(std::make_unique<implementation>()) {
        impl_->options = options;
    }
    
    compiler::~compiler() = default;
    
    compiler::compiler(compiler&&) noexcept = default;
    compiler& compiler::operator=(compiler&&) noexcept = default;
    
    std::unique_ptr<module> compiler::compile(const std::vector<declaration_ptr>& declarations) {
        
        impl_->compilation_start = std::chrono::high_resolution_clock::now();
        impl_->reset_for_new_compilation();
        
        try {
            // Create main function to contain top-level code
            begin_function("__main__");
            
            // Compile all declarations
            compile_declarations(declarations);
            
            // Ensure main function returns
            // The last expression's value should be on the stack if there was one
            // Only push null if the declarations didn't leave anything on the stack
            emit(opcode::RETURN_VALUE);
            
            // Finalize main function
            auto main_func = end_function();
            size_t main_function_index = impl_->current_module->functions.size();
            impl_->current_module->functions.push_back(std::move(main_func));
            impl_->current_module->main_function = main_function_index;
            
            // Apply optimizations if enabled
            if (impl_->options.opt_level != optimization_level::NONE) {
                optimize_module(impl_->current_module.get(), impl_->options.opt_level);
            }
            
            // Update statistics
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end_time - impl_->compilation_start);
            impl_->stats.compilation_time_ms = duration.count() / 1000.0;
            
            return std::move(impl_->current_module);
            
        } catch (const std::exception& e) {
            error("Compilation failed: " + std::string(e.what()));
            return nullptr;
        }
    }
    
    std::unique_ptr<module> compiler::compile_script(const std::string& source_code, const std::string& filename) {
        try {
            // Parse the source code
            lexer lex(source_code);
            auto tokens = lex.tokenize();
            
            parser parse(tokens, filename);
            auto declarations = parse.parse();
            
            // Set source file information
            impl_->current_module = std::make_unique<module>();
            impl_->current_module->source_file = filename;
            
            // Split source into lines for debug info
            std::stringstream ss(source_code);
            std::string line;
            while (std::getline(ss, line)) {
                impl_->current_module->source_lines.push_back(line);
            }
            
            return compile(declarations);
            
        } catch (const std::exception& e) {
            error("Failed to parse source: " + std::string(e.what()));
            return nullptr;
        }
    }
    
    std::unique_ptr<module> compiler::compile_file(const std::string& filename) {
        try {
            std::ifstream file(filename);
            if (!file.is_open()) {
                error("Cannot open file: " + filename);
                return nullptr;
            }
            
            std::stringstream buffer;
            buffer << file.rdbuf();
            return compile_script(buffer.str(), filename);
            
        } catch (const std::exception& e) {
            error("Failed to read file: " + std::string(e.what()));
            return nullptr;
        }
    }
    
    void compiler::compile_declarations(const std::vector<declaration_ptr>& declarations) {
        for (size_t i = 0; i < declarations.size(); ++i) {
            bool is_last = (i == declarations.size() - 1);
            compile_declaration(declarations[i], is_last);
        }
    }
    
    void compiler::compile_declaration(declaration_ptr decl, bool is_last) {
        if (auto* func_declaration = dynamic_cast<function_decl*>(decl.get())) {
            compile_function_decl(func_declaration);
        } else if (auto* var_declaration = dynamic_cast<variable_decl*>(decl.get())) {
            compile_variable_decl(var_declaration);
        } else if (auto* class_declaration = dynamic_cast<class_decl*>(decl.get())) {
            compile_class_decl(class_declaration);
        } else if (auto* expr_declaration = dynamic_cast<expression_decl*>(decl.get())) {
            compile_expression_decl(expr_declaration, is_last);
        } else if (auto* stmt_declaration = dynamic_cast<statement_decl*>(decl.get())) {
            // For statement declarations, check if it's an expression statement that should keep its value
            if (is_last && stmt_declaration->statement) {
                if (auto* expr_stmt = dynamic_cast<expression_stmt*>(stmt_declaration->statement.get())) {
                    // This is the last statement and it's an expression - keep the value on stack
                    compile_expression(expr_stmt->expression);
                    // Don't emit POP - keep the value for implicit return
                    return;
                }
            }
            // Otherwise compile normally, but pass is_last flag if this is the last declaration
            if (is_last) {
                compile_statement(stmt_declaration->statement, true);
            } else {
                compile_statement(stmt_declaration->statement);
            }
        } else {
            error("Unknown declaration type");
        }
    }
    
    void compiler::compile_declaration(declaration_ptr decl) {
        compile_declaration(decl, false);
    }
    
    void compiler::compile_function_decl(function_decl* decl) {
        
        // Save current function context
        auto saved_function = std::move(impl_->current_function);
        
        // Create new function context
        begin_function(decl->name);
        
        // Set return type
        if (decl->return_type) {
            impl_->current_function->func->return_type = 
                decl->return_type->base_type != script_value_type::jai_null_type ? 
                decl->return_type->base_type : script_value_type::jai_null_type;
        }
        
        // Add parameters as local variables
        for (const auto& param : decl->parameters) {
            script_value_type param_type = param.type ? param.type->base_type : script_value_type::jai_null_type;
            declare_local(param.name, param_type);
            impl_->current_function->func->parameter_names.push_back(param.name);
            impl_->current_function->func->parameter_types.push_back(param_type);
        }
        
        // Compile function body
        if (decl->body) {
            compile_statement(decl->body);
        }
        
        // Ensure function has return
        emit(opcode::PUSH_NULL);
        emit(opcode::RETURN_VALUE);
        
        // Finalize function
        auto compiled_function = end_function();
        impl_->current_module->functions.push_back(std::move(compiled_function));
        impl_->stats.functions_compiled++;
        
        // Restore previous function context
        impl_->current_function = std::move(saved_function);
        
        // If we're in main function, add this function to global scope
        bool in_main = !impl_->current_function || (impl_->current_function && impl_->current_function->func->name == "__main__");
        
        
        if (in_main) {
            uint16_t func_index = static_cast<uint16_t>(impl_->current_module->functions.size() - 1);
            emit(opcode::PUSH_FUNCTION, func_index);
            
            // Store in global variable
            uint16_t global_index = resolve_global(decl->name);
            emit(opcode::STORE_GLOBAL, global_index);
            // Note: STORE_GLOBAL already pops the function from stack
            
        }
    }
    
    void compiler::compile_variable_decl(variable_decl* decl) {
        // Compile initializer if present
        if (decl->initializer) {
            compile_expression(decl->initializer);
        } else {
            emit(opcode::PUSH_NULL);
        }
        
        // Determine storage location
        if (impl_->current_function && impl_->current_function->scope_depth > 0) {
            // Local variable
            script_value_type var_type = decl->type ? decl->type->base_type : script_value_type::jai_null_type;
            uint8_t local_slot = declare_local(decl->name, var_type);
            emit(opcode::STORE_LOCAL, local_slot);
        } else {
            // Global variable
            uint16_t global_index = resolve_global(decl->name);
            emit(opcode::STORE_GLOBAL, global_index);
        }
        
        // Note: STORE_LOCAL and STORE_GLOBAL already pop the value from stack
    }
    
    void compiler::compile_class_decl(class_decl* decl) {
        // TODO: Implement VM bytecode compilation for classes
        // 1. Generate bytecode for class definition (DEFINE_CLASS)
        // 2. Compile field declarations with default values
        // 3. Compile constructor bytecode (handle overloading)
        // 4. Compile method bodies to bytecode modules
        // 5. Generate vtable for virtual method dispatch
        // 6. Handle inheritance and base class resolution
        // 7. Register class in VM's class registry
        
        // Class compilation would go here
        // For now, just emit a warning
        warning("Class compilation not yet implemented: " + decl->name);
    }
    
    void compiler::compile_expression_decl(expression_decl* decl, bool is_last) {
        compile_expression(decl->expression);
        // If this is the last declaration in main function, treat as implicit return
        // Otherwise, check the declaration's implicit_return flag
        bool should_keep_result = is_last || decl->implicit_return;
        if (!should_keep_result) {
            emit(opcode::POP); // Discard expression result unless it's an implicit return
        }
        // Debug output
        if (is_last) {
            // Add a comment to the bytecode for debugging
            // std::cout << "DEBUG: Last expression compiled, kept on stack\n";
        }
    }
    
    void compiler::compile_expression_decl(expression_decl* decl) {
        compile_expression_decl(decl, false);
    }
    
    void compiler::compile_statement(statement_ptr stmt, bool is_last_in_main) {
        if (auto* block = dynamic_cast<block_stmt*>(stmt.get())) {
            compile_block_stmt(block);
        } else if (auto* expr_stmt = dynamic_cast<expression_stmt*>(stmt.get())) {
            compile_expression_stmt(expr_stmt, is_last_in_main);
        } else if (auto* if_statement = dynamic_cast<if_stmt*>(stmt.get())) {
            compile_if_stmt(if_statement);
        } else if (auto* while_statement = dynamic_cast<while_stmt*>(stmt.get())) {
            compile_while_stmt(while_statement);
        } else if (auto* for_statement = dynamic_cast<for_stmt*>(stmt.get())) {
            compile_for_stmt(for_statement);
        } else if (auto* return_statement = dynamic_cast<return_stmt*>(stmt.get())) {
            compile_return_stmt(return_statement);
        } else if (auto* break_statement = dynamic_cast<break_stmt*>(stmt.get())) {
            compile_break_stmt(break_statement);
        } else if (auto* continue_statement = dynamic_cast<continue_stmt*>(stmt.get())) {
            compile_continue_stmt(continue_statement);
        } else {
            error("Unknown statement type");
        }
    }
    
    void compiler::compile_statement(statement_ptr stmt) {
        compile_statement(stmt, false);
    }
    
    void compiler::compile_block_stmt(block_stmt* stmt) {
        begin_scope();
        
        for (const auto& decl : stmt->declarations) {
            compile_declaration(decl);
        }
        
        end_scope();
    }
    
    void compiler::compile_expression_stmt(expression_stmt* stmt, bool is_last_in_main) {
        compile_expression(stmt->expression);
        if (!is_last_in_main) {
            emit(opcode::POP); // Discard expression result unless it's the last statement in main
        }
    }
    
    void compiler::compile_expression_stmt(expression_stmt* stmt) {
        compile_expression_stmt(stmt, false);
    }
    
    void compiler::compile_if_stmt(if_stmt* stmt) {
        // Compile condition
        compile_expression(stmt->condition);
        
        // Jump to else branch if condition is false
        size_t else_jump = emit_jump(opcode::JUMP_IF_FALSE);
        
        // Compile then branch
        compile_statement(stmt->then_statement);
        
        if (stmt->else_statement) {
            // Jump over else branch
            size_t end_jump = emit_jump(opcode::JUMP);
            
            // Patch else jump to point here
            patch_jump(else_jump);
            
            // Compile else branch
            compile_statement(stmt->else_statement);
            
            // Patch end jump
            patch_jump(end_jump);
        } else {
            // Patch else jump to point here
            patch_jump(else_jump);
        }
    }
    
    void compiler::compile_while_stmt(while_stmt* stmt) {
        begin_loop();
        
        size_t loop_start = impl_->current_function->func->instructions.size();
        
        // Compile condition
        compile_expression(stmt->condition);
        
        // Jump out of loop if condition is false
        size_t exit_jump = emit_jump(opcode::JUMP_IF_FALSE);
        
        // Compile body
        compile_statement(stmt->body);
        
        // Jump back to start
        emit_loop(loop_start);
        
        // Patch exit jump
        patch_jump(exit_jump);
        
        end_loop();
    }
    
    void compiler::compile_for_stmt(for_stmt* stmt) {
        begin_scope();
        begin_loop();
        
        // Compile initializer
        if (stmt->initializer) {
            compile_declaration(stmt->initializer);
        }
        
        size_t loop_start = impl_->current_function->func->instructions.size();
        
        // Compile condition (if present)
        size_t exit_jump = SIZE_MAX;
        if (stmt->condition) {
            compile_expression(stmt->condition);
            exit_jump = emit_jump(opcode::JUMP_IF_FALSE);
        }
        
        // Compile body
        compile_statement(stmt->body);
        
        // Compile update expression
        if (stmt->update) {
            compile_expression(stmt->update);
            emit(opcode::POP); // Discard update result
        }
        
        // Jump back to start
        emit_loop(loop_start);
        
        // Patch exit jump if we had a condition
        if (exit_jump != SIZE_MAX) {
            patch_jump(exit_jump);
        }
        
        end_loop();
        end_scope();
    }
    
    void compiler::compile_return_stmt(return_stmt* stmt) {
        if (stmt->value) {
            compile_expression(stmt->value);
            emit(opcode::RETURN_VALUE);
        } else {
            emit(opcode::PUSH_NULL);
            emit(opcode::RETURN_VALUE);
        }
    }
    
    void compiler::compile_break_stmt(break_stmt* stmt) {
        emit_break();
    }
    
    void compiler::compile_continue_stmt(continue_stmt* stmt) {
        emit_continue();
    }
    
    void compiler::compile_expression(expression_ptr expr) {
        if (auto* literal = dynamic_cast<literal_expr*>(expr.get())) {
            compile_literal_expr(literal);
        } else if (auto* identifier = dynamic_cast<identifier_expr*>(expr.get())) {
            compile_identifier_expr(identifier);
        } else if (auto* binary = dynamic_cast<binary_expr*>(expr.get())) {
            compile_binary_expr(binary);
        } else if (auto* unary = dynamic_cast<unary_expr*>(expr.get())) {
            compile_unary_expr(unary);
        } else if (auto* assignment = dynamic_cast<assignment_expr*>(expr.get())) {
            compile_assignment_expr(assignment);
        } else if (auto* call = dynamic_cast<call_expr*>(expr.get())) {
            compile_call_expr(call);
        } else if (auto* member = dynamic_cast<member_expr*>(expr.get())) {
            compile_member_expr(member);
        } else if (auto* lambda = dynamic_cast<lambda_expr*>(expr.get())) {
            compile_lambda_expr(lambda);
        } else if (auto* new_expression = dynamic_cast<new_expr*>(expr.get())) {
            compile_new_expr(new_expression);
        } else if (auto* ternary = dynamic_cast<ternary_expr*>(expr.get())) {
            compile_ternary_expr(ternary);
        } else if (auto* array_literal = dynamic_cast<array_literal_expr*>(expr.get())) {
            compile_array_literal_expr(array_literal);
        } else if (auto* map_literal = dynamic_cast<map_literal_expr*>(expr.get())) {
            compile_map_literal_expr(map_literal);
        } else {
            error("Unknown expression type");
        }
    }
    
    void compiler::compile_literal_expr(literal_expr* expr) {
        const auto& value = expr->value;
        
        if (value.is_null()) {
            emit(opcode::PUSH_NULL);
        } else if (value.is_bool()) {
            emit(value.as_bool() ? opcode::PUSH_TRUE : opcode::PUSH_FALSE);
        } else if (value.is_int()) {
            emit(opcode::PUSH_INT, static_cast<uint32_t>(value.as_int()));
        } else if (value.is_float()) {
            emit(opcode::PUSH_FLOAT, value.as_float());
        } else if (value.is_char()) {
            emit(opcode::PUSH_CHAR, static_cast<uint8_t>(value.as_char()));
        } else if (value.is_string()) {
            uint16_t string_index = add_string_constant(value.as_string());
            emit(opcode::PUSH_STRING, string_index);
        } else {
            error("Unknown literal type");
        }
    }
    
    void compiler::compile_identifier_expr(identifier_expr* expr) {
        // Try to resolve as local variable first
        uint8_t local_slot = resolve_local(expr->name);
        if (local_slot != UINT8_MAX) {
            emit(opcode::LOAD_LOCAL, local_slot);
            return;
        }
        
        // Try to resolve as upvalue (for closures)
        uint8_t upvalue_slot = resolve_upvalue(expr->name);
        if (upvalue_slot != UINT8_MAX) {
            emit(opcode::LOAD_UPVALUE, upvalue_slot);
            return;
        }
        
        // Must be global variable
        uint16_t global_index = resolve_global(expr->name);
        emit(opcode::LOAD_GLOBAL, global_index);
    }
    
    void compiler::compile_binary_expr(binary_expr* expr) {
        // Special handling for short-circuit operators
        if (expr->op.type == token_type::ampersand_ampersand) {
            compile_expression(expr->left);
            emit(opcode::DUP); // Duplicate for result
            
            size_t short_circuit_jump = emit_jump(opcode::JUMP_IF_FALSE);
            emit(opcode::POP); // Remove duplicated value
            compile_expression(expr->right);
            patch_jump(short_circuit_jump);
            return;
        }
        
        if (expr->op.type == token_type::pipe_pipe) {
            compile_expression(expr->left);
            emit(opcode::DUP); // Duplicate for result
            
            size_t short_circuit_jump = emit_jump(opcode::JUMP_IF_TRUE);
            emit(opcode::POP); // Remove duplicated value
            compile_expression(expr->right);
            patch_jump(short_circuit_jump);
            return;
        }
        
        // Regular binary operations
        compile_expression(expr->left);
        compile_expression(expr->right);
        
        // Emit appropriate opcode
        switch (expr->op.type) {
            case token_type::plus:          emit(opcode::ADD); break;
            case token_type::minus:         emit(opcode::SUB); break;
            case token_type::star:          emit(opcode::MUL); break;
            case token_type::slash:         emit(opcode::DIV); break;
            case token_type::percent:       emit(opcode::MOD); break;
            case token_type::equal_equal:   emit(opcode::EQ); break;
            case token_type::bang_equal:    emit(opcode::NE); break;
            case token_type::less:          emit(opcode::LT); break;
            case token_type::less_equal:    emit(opcode::LE); break;
            case token_type::greater:       emit(opcode::GT); break;
            case token_type::greater_equal: emit(opcode::GE); break;
            case token_type::spaceship:     emit(opcode::CMP); break;
            case token_type::left_bracket:  emit(opcode::ARRAY_GET); break; // Array/map indexing
            default:
                error("Unknown binary operator: " + std::to_string(static_cast<int>(expr->op.type)));
        }
    }
    
    void compiler::compile_unary_expr(unary_expr* expr) {
        compile_expression(expr->operand);
        
        switch (expr->op.type) {
            case token_type::minus:     emit(opcode::NEG); break;
            case token_type::bang:      emit(opcode::NOT); break;
            case token_type::tilde:     emit(opcode::BIT_NOT); break;
            default:
                error("Unknown unary operator: " + std::to_string(static_cast<int>(expr->op.type)));
        }
    }
    
    void compiler::compile_assignment_expr(assignment_expr* expr) {
        // For simple assignment to identifier
        if (auto* identifier = dynamic_cast<identifier_expr*>(expr->target.get())) {
            compile_expression(expr->value);
            emit(opcode::DUP); // Duplicate value for expression result
            
            // Try local first
            uint8_t local_slot = resolve_local(identifier->name);
            if (local_slot != UINT8_MAX) {
                emit(opcode::STORE_LOCAL, local_slot);
                return;
            }
            
            // Try upvalue
            uint8_t upvalue_slot = resolve_upvalue(identifier->name);
            if (upvalue_slot != UINT8_MAX) {
                emit(opcode::STORE_UPVALUE, upvalue_slot);
                return;
            }
            
            // Must be global
            uint16_t global_index = resolve_global(identifier->name);
            emit(opcode::STORE_GLOBAL, global_index);
            return;
        }
        
        // For array/map assignment
        if (auto* binary = dynamic_cast<binary_expr*>(expr->target.get())) {
            if (binary->op.type == token_type::left_bracket) {
                compile_expression(binary->left);  // Container
                compile_expression(binary->right); // Index
                compile_expression(expr->value);   // Value
                emit(opcode::ARRAY_SET);
                return;
            }
        }
        
        error("Invalid assignment target");
    }
    
    void compiler::compile_call_expr(call_expr* expr) {
        // Check if this is a method call (callee is member_expr)
        if (auto* member = dynamic_cast<member_expr*>(expr->callee.get())) {
            // Method call: obj.method(args)
            compile_expression(member->object);  // Push object
            
            // Compile arguments
            for (const auto& arg : expr->arguments) {
                compile_expression(arg);
            }
            
            // Emit CALL_METHOD with method name and arg count
            uint16_t method_name_index = add_string_constant(member->member);
            uint8_t arg_count = static_cast<uint8_t>(expr->arguments.size());
            
            // CALL_METHOD needs both method name index and arg count
            // Pack both values into long_operand: method_name_index in upper 16 bits, arg_count in lower 8 bits
            uint64_t packed_operand = (static_cast<uint64_t>(method_name_index) << 16) | static_cast<uint64_t>(arg_count);
            instruction instr(opcode::CALL_METHOD, packed_operand);
            impl_->current_function->func->instructions.push_back(instr);
            impl_->stats.instructions_generated++;
            return;
        }
        
        // Check for built-in functions
        if (auto* identifier = dynamic_cast<identifier_expr*>(expr->callee.get())) {
            if (is_builtin_function(identifier->name)) {
                // Compile arguments
                for (const auto& arg : expr->arguments) {
                    compile_expression(arg);
                }
                
                uint16_t builtin_index = get_builtin_function_index(identifier->name);
                uint8_t arg_count = static_cast<uint8_t>(expr->arguments.size());
                emit_builtin_call(builtin_index, arg_count);
                return;
            }
        }
        
        // Regular function call
        compile_expression(expr->callee);
        
        // Compile arguments
        for (const auto& arg : expr->arguments) {
            compile_expression(arg);
        }
        
        uint8_t arg_count = static_cast<uint8_t>(expr->arguments.size());
        emit(opcode::CALL, arg_count);
    }
    
    void compiler::compile_member_expr(member_expr* expr) {
        // For now, compile member access as string literal and MAP_GET
        // This handles m["key"] style access
        compile_expression(expr->object);
        
        // Push member name as string
        emit(opcode::PUSH_STRING, add_string_constant(expr->member));
        
        // Use MAP_GET for now (would need runtime type checking for arrays vs maps)
        emit(opcode::MAP_GET);
    }
    
    void compiler::compile_lambda_expr(lambda_expr* expr) {
        // Lambda compilation would go here
        // For now, just emit a warning
        warning("Lambda compilation not yet implemented");
        emit(opcode::PUSH_NULL);
    }
    
    void compiler::compile_new_expr(new_expr* expr) {
        // Object construction compilation would go here
        // For now, just emit a warning
        warning("Object construction compilation not yet implemented");
        emit(opcode::PUSH_NULL);
    }
    
    void compiler::compile_ternary_expr(ternary_expr* expr) {
        compile_expression(expr->condition);
        
        size_t else_jump = emit_jump(opcode::JUMP_IF_FALSE);
        compile_expression(expr->then_expression);
        size_t end_jump = emit_jump(opcode::JUMP);
        
        patch_jump(else_jump);
        compile_expression(expr->else_expression);
        patch_jump(end_jump);
    }
    
    void compiler::compile_array_literal_expr(array_literal_expr* expr) {
        // Create new array
        emit(opcode::NEW_ARRAY, static_cast<uint8_t>(expr->elements.size()));
        
        // Add elements - array stays on stack throughout
        for (size_t i = 0; i < expr->elements.size(); ++i) {
            // Don't duplicate - ARRAY_SET should work with array deeper in stack
            emit(opcode::PUSH_INT, static_cast<uint32_t>(i)); // Index
            compile_expression(expr->elements[i]); // Value
            emit(opcode::ARRAY_SET);
        }
    }
    
    void compiler::compile_map_literal_expr(map_literal_expr* expr) {
        // Create new map
        emit(opcode::NEW_MAP);
        
        // Add key-value pairs
        for (const auto& [key, value] : expr->entries) {
            // Don't duplicate - MAP_SET should modify in place and leave map on stack
            compile_expression(key); // Key
            compile_expression(value); // Value
            emit(opcode::MAP_SET);
        }
    }
    
    // Code generation helpers
    void compiler::emit(opcode op) {
        if (!impl_->current_function) {
            error("No current function for instruction emission");
            return;
        }
        
        impl_->current_function->func->instructions.emplace_back(op);
        impl_->stats.instructions_generated++;
    }
    
    void compiler::emit(opcode op, uint8_t operand) {
        if (!impl_->current_function) {
            error("No current function for instruction emission");
            return;
        }
        
        impl_->current_function->func->instructions.emplace_back(op, operand);
        impl_->stats.instructions_generated++;
    }
    
    void compiler::emit(opcode op, uint16_t operand) {
        if (!impl_->current_function) {
            error("No current function for instruction emission");
            return;
        }
        
        impl_->current_function->func->instructions.emplace_back(op, operand);
        impl_->stats.instructions_generated++;
    }
    
    void compiler::emit(opcode op, uint32_t operand) {
        if (!impl_->current_function) {
            error("No current function for instruction emission");
            return;
        }
        
        impl_->current_function->func->instructions.emplace_back(op, operand);
        impl_->stats.instructions_generated++;
    }
    
    void compiler::emit(opcode op, uint64_t operand) {
        if (!impl_->current_function) {
            error("No current function for instruction emission");
            return;
        }
        
        impl_->current_function->func->instructions.emplace_back(op, operand);
        impl_->stats.instructions_generated++;
    }
    
    void compiler::emit(opcode op, double operand) {
        if (!impl_->current_function) {
            error("No current function for instruction emission");
            return;
        }
        
        impl_->current_function->func->instructions.emplace_back(op, operand);
        impl_->stats.instructions_generated++;
    }
    
    size_t compiler::emit_jump(opcode op) {
        emit(op, static_cast<uint16_t>(0)); // Placeholder offset
        return impl_->current_function->func->instructions.size() - 1;
    }
    
    void compiler::patch_jump(size_t jump_offset) {
        if (!impl_->current_function) {
            error("No current function for jump patching");
            return;
        }
        
        auto& instructions = impl_->current_function->func->instructions;
        if (jump_offset >= instructions.size()) {
            error("Jump offset out of bounds");
            return;
        }
        
        uint16_t target_offset = static_cast<uint16_t>(instructions.size());
        instructions[jump_offset].short_operand = target_offset;
    }
    
    void compiler::emit_loop(size_t loop_start) {
        uint16_t offset = static_cast<uint16_t>(loop_start);
        emit(opcode::JUMP, offset);
    }
    
    // Constant management
    uint16_t compiler::add_string_constant(const std::string& str) {
        // Check if string already exists
        auto& constants = impl_->current_module->string_constants;
        auto it = std::find(constants.begin(), constants.end(), str);
        if (it != constants.end()) {
            return static_cast<uint16_t>(std::distance(constants.begin(), it));
        }
        
        // Add new string constant
        constants.push_back(str);
        return static_cast<uint16_t>(constants.size() - 1);
    }
    
    uint16_t compiler::add_value_constant(const script_value& value) {
        auto& constants = impl_->current_module->value_constants;
        constants.push_back(value);
        return static_cast<uint16_t>(constants.size() - 1);
    }
    
    // Variable management
    uint8_t compiler::declare_local(const std::string& name, script_value_type type) {
        if (!impl_->current_function) {
            error("Cannot declare local variable outside function");
            return UINT8_MAX;
        }
        
        auto& locals = impl_->current_function->locals;
        uint8_t slot = static_cast<uint8_t>(locals.size());
        
        locals.push_back({name, slot, type, false, impl_->current_function->scope_depth});
        
        impl_->current_function->func->local_count = static_cast<uint8_t>(locals.size());
        
        return slot;
    }
    
    uint8_t compiler::resolve_local(const std::string& name) {
        if (!impl_->current_function) {
            return UINT8_MAX;
        }
        
        auto& locals = impl_->current_function->locals;
        for (auto it = locals.rbegin(); it != locals.rend(); ++it) {
            if (it->name == name) {
                return it->slot;
            }
        }
        
        return UINT8_MAX;
    }
    
    uint16_t compiler::resolve_global(const std::string& name) {
        auto& globals = impl_->current_module->global_names;
        auto it = std::find(globals.begin(), globals.end(), name);
        if (it != globals.end()) {
            return static_cast<uint16_t>(std::distance(globals.begin(), it));
        }
        
        // Add new global
        globals.push_back(name);
        return static_cast<uint16_t>(globals.size() - 1);
    }
    
    uint8_t compiler::resolve_upvalue(const std::string& name) {
        // Upvalue resolution for closures would go here
        // For now, return not found
        return UINT8_MAX;
    }
    
    // Scope management
    void compiler::begin_scope() {
        if (impl_->current_function) {
            impl_->current_function->scope_depth++;
        }
    }
    
    void compiler::end_scope() {
        if (!impl_->current_function) {
            return;
        }
        
        impl_->current_function->scope_depth--;
        
        // Remove locals from this scope
        auto& locals = impl_->current_function->locals;
        while (!locals.empty() && locals.back().scope_depth > impl_->current_function->scope_depth) {
            locals.pop_back();
        }
        
        // Don't reduce local_count - VM needs all slots that were ever allocated
        // impl_->current_function->func->local_count = static_cast<uint8_t>(locals.size());
    }
    
    // Loop management
    void compiler::begin_loop() {
        if (!impl_->current_function) {
            error("Cannot begin loop outside function");
            return;
        }
        
        size_t start_offset = impl_->current_function->func->instructions.size();
        impl_->current_function->loops.push_back({start_offset, {}, {}});
    }
    
    void compiler::end_loop() {
        if (!impl_->current_function || impl_->current_function->loops.empty()) {
            error("No loop to end");
            return;
        }
        
        auto& loop = impl_->current_function->loops.back();
        
        // Patch all break jumps to point here
        size_t end_offset = impl_->current_function->func->instructions.size();
        for (size_t jump_offset : loop.break_jumps) {
            impl_->current_function->func->instructions[jump_offset].short_operand = 
                static_cast<uint16_t>(end_offset);
        }
        
        // Continue jumps should already point to loop start
        
        impl_->current_function->loops.pop_back();
    }
    
    void compiler::emit_break() {
        if (!impl_->current_function || impl_->current_function->loops.empty()) {
            error("Break statement outside loop");
            return;
        }
        
        auto& loop = impl_->current_function->loops.back();
        size_t jump_offset = emit_jump(opcode::JUMP);
        loop.break_jumps.push_back(jump_offset);
    }
    
    void compiler::emit_continue() {
        if (!impl_->current_function || impl_->current_function->loops.empty()) {
            error("Continue statement outside loop");
            return;
        }
        
        auto& loop = impl_->current_function->loops.back();
        emit_loop(loop.start_offset);
    }
    
    void compiler::emit_builtin_call(uint16_t builtin_index, uint8_t arg_count) {
        if (!impl_->current_function) {
            error("No current function for instruction emission");
            return;
        }
        
        // Create instruction with both operands
        instruction instr(opcode::CALL_BUILTIN);
        instr.short_operand = builtin_index;
        // For the second operand (arg_count), we'll store it in the upper byte of long_operand
        instr.long_operand = (static_cast<uint64_t>(builtin_index) << 8) | arg_count;
        
        impl_->current_function->func->instructions.push_back(instr);
        impl_->stats.instructions_generated++;
    }
    
    // Function compilation context
    void compiler::begin_function(const std::string& name) {
        impl_->current_function = std::make_unique<function_context>(name);
    }
    
    std::unique_ptr<function> compiler::end_function() {
        if (!impl_->current_function) {
            error("No function to end");
            return nullptr;
        }
        
        auto result = std::move(impl_->current_function->func);
        impl_->current_function = nullptr;
        
        return result;
    }
    
    // Error handling
    void compiler::error(const std::string& message) {
        impl_->errors.push_back(message);
    }
    
    void compiler::error(const std::string& message, const source_location& location) {
        std::string full_message = location.to_string() + ": " + message;
        impl_->errors.push_back(full_message);
    }
    
    void compiler::warning(const std::string& message) {
        impl_->warnings.push_back(message);
    }
    
    // Built-in function handling
    bool compiler::is_builtin_function(const std::string& name) const {
        // Check common built-in functions
        return name == "print" || name == "to_string" || name == "size";
    }
    
    uint16_t compiler::get_builtin_function_index(const std::string& name) const {
        if (name == "print") return 0;
        if (name == "to_string") return 1;
        if (name == "size") return 2;
        return UINT16_MAX;
    }
    
    // Public interface methods
    void compiler::set_optimization_level(optimization_level level) {
        impl_->options.opt_level = level;
    }
    
    void compiler::set_debug_info_enabled(bool enabled) {
        impl_->options.include_debug_info = enabled;
    }
    
    void compiler::set_type_checking_enabled(bool enabled) {
        impl_->options.enable_type_checking = enabled;
    }
    
    bool compiler::has_errors() const {
        return !impl_->errors.empty();
    }
    
    std::vector<std::string> compiler::get_errors() const {
        return impl_->errors;
    }
    
    void compiler::clear_errors() {
        impl_->errors.clear();
        impl_->warnings.clear();
    }
    
    compilation_stats compiler::get_stats() const {
        return impl_->stats;
    }
    
    // High-level compilation functions
    compilation_result compile_script(const std::string& source_code, 
                                    const std::string& filename,
                                    const compilation_options& options) {
        compilation_result result;
        
        compiler comp(options);
        result.compiled_module = comp.compile_script(source_code, filename);
        result.errors = comp.get_errors();
        result.stats = comp.get_stats();
        
        return result;
    }
    
    compilation_result compile_file(const std::string& filename,
                                  const compilation_options& options) {
        compilation_result result;
        
        compiler comp(options);
        result.compiled_module = comp.compile_file(filename);
        result.errors = comp.get_errors();
        result.stats = comp.get_stats();
        
        return result;
    }
    
    // Module optimization (placeholder implementation)
    void optimize_module(module* mod, optimization_level level) {
        if (!mod) return;
        
        // For now, just a placeholder - no optimizations applied
        // Future optimizations might include:
        // - Dead code elimination
        // - Constant folding
        // - Jump threading
        // - Peephole optimizations
        
        switch (level) {
            case optimization_level::NONE:
                // No optimizations
                break;
            case optimization_level::BASIC:
                // Basic optimizations would go here
                break;
            case optimization_level::STANDARD:
                // Standard optimizations would go here
                break;
            case optimization_level::AGGRESSIVE:
                // Advanced optimizations would go here
                break;
        }
    }

} // namespace jvm
} // namespace jai