#include <iostream>
#include "../../../include/jaiscript/detail/parser.hpp"
#include "../../../include/jaiscript/detail/lexer.hpp"
#include "../../../include/jaiscript/detail/ast.hpp"

using namespace jai;

// Forward declarations are not needed - they're in ast.hpp

void print_declaration_type(const declaration_ptr& decl, int indent = 0) {
    std::string spaces(indent * 2, ' ');
    
    if (auto* func_decl = dynamic_cast<function_decl*>(decl.get())) {
        std::cout << spaces << "function_decl: " << func_decl->name << "\n";
    } else if (auto* var_decl = dynamic_cast<variable_decl*>(decl.get())) {
        std::cout << spaces << "variable_decl: " << var_decl->name << "\n";
    } else if (auto* expr_decl = dynamic_cast<expression_decl*>(decl.get())) {
        std::cout << spaces << "expression_decl\n";
    } else if (auto* stmt_decl = dynamic_cast<statement_decl*>(decl.get())) {
        std::cout << spaces << "statement_decl\n";
        if (stmt_decl->statement) {
            if (auto* expr_stmt = dynamic_cast<expression_stmt*>(stmt_decl->statement.get())) {
                std::cout << spaces << "  -> expression_stmt\n";
            } else if (auto* block = dynamic_cast<jai::block_stmt*>(stmt_decl->statement.get())) {
                std::cout << spaces << "  -> block_stmt\n";
            } else if (auto* forloop = dynamic_cast<jai::for_stmt*>(stmt_decl->statement.get())) {
                std::cout << spaces << "  -> for_stmt\n";
            } else {
                std::cout << spaces << "  -> other statement type\n";
            }
        }
    } else {
        std::cout << spaces << "unknown declaration type\n";
    }
}

int main() {
    try {
        // The script that's failing
        std::string script = R"(
            var sum = 0;
            for (var i = 0; i < 3; i = i + 1) {
                sum = sum + i;
            }
            sum;
        )";
        
        std::cout << "=== Analyzing AST structure ===\n";
        std::cout << "Script:\n" << script << "\n\n";
        
        // Parse the script
        lexer lex(script);
        auto tokens = lex.tokenize();
        parser parse(tokens, "test.jai");
        auto declarations = parse.parse();
        
        std::cout << "Total declarations: " << declarations.size() << "\n\n";
        
        // Print each declaration
        for (size_t i = 0; i < declarations.size(); ++i) {
            std::cout << "Declaration " << i << " (is_last=" << (i == declarations.size() - 1) << "):\n";
            print_declaration_type(declarations[i], 1);
            std::cout << "\n";
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}