#include <iostream>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

int main() {
    std::string code = "fallthrough;";
    jai::lexer lex(code);
    auto tokens = lex.tokenize();
    
    std::cout << "Tokens:" << std::endl;
    for (const auto& tok : tokens) {
        std::cout << "  Type: " << static_cast<int>(tok.type) << " Lexeme: '" << tok.lexeme << "'" << std::endl;
    }
    
    try {
        jai::parser p(tokens);
        auto ast = p.parse();
        std::cout << "ERROR: Should have thrown parse_error" << std::endl;
    } catch (const jai::parse_error& e) {
        std::cout << "SUCCESS: Got parse_error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR: Got wrong exception: " << e.what() << std::endl;
    }
    
    return 0;
}
