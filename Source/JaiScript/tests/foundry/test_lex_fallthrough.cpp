#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <iostream>

int main() {
    std::string code = "fallthrough;";
    jai::lexer lex(code);
    auto tokens = lex.tokenize();

    for (const auto& tok : tokens) {
        std::cout << "Token: " << tok.lexeme << ", Type: " << static_cast<int>(tok.type) << "\n";
    }

    return 0;
}
