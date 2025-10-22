#include <iostream>
#include <jaiscript/detail/lexer.hpp>

int main() {
    std::string code = R"(
        switch (x) {
            case 1:
                result = "one";
        }
    )";
    
    jai::lexer lex(code);
    auto tokens = lex.tokenize();
    
    for (const auto& token : tokens) {
        std::cout << "Token type: " << static_cast<int>(token.type) 
                  << " lexeme: '" << token.lexeme << "'" << std::endl;
        
        if (token.lexeme == "switch") {
            std::cout << "  -> switch token type is: " 
                      << (token.type == jai::token_type::switch_keyword ? "KEYWORD" : "NOT KEYWORD") 
                      << std::endl;
        }
    }
    
    return 0;
}