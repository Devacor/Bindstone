#include <iostream>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/ast.hpp>

int main() {
    std::string code = "switch (2) { case 2: 42; }";
    jai::lexer lex(code);
    auto tokens = lex.tokenize();
    jai::parser p(tokens);
    
    auto ast = p.parse();
    std::cout << "Number of declarations: " << ast.size() << std::endl;
    
    if (!ast.empty()) {
        auto first = ast[0].get();
        std::cout << "First declaration type: " << typeid(*first).name() << std::endl;
        
        // Try to cast to expression_decl
        if (auto expr_decl = dynamic_cast<jai::expression_decl*>(first)) {
            std::cout << "It's an expression_decl" << std::endl;
            if (expr_decl->expression) {
                std::cout << "Expression type: " << typeid(*expr_decl->expression).name() << std::endl;
            }
        }
    }
    
    return 0;
}
