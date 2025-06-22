#include <iostream>
#include <jaiscript/detail/lexer.hpp>

using namespace JaiScript;

int main() {
    std::string code = "void nested(map<string, array<int>>& data) { }";
    std::cout << "Code: " << code << "\n\n";
    
    Lexer lexer(code, "test.jai");
    auto tokens = lexer.tokenize();
    
    std::cout << "Tokens:\n";
    int i = 0;
    for (const auto& token : tokens) {
        std::cout << "  [" << i << "] " << token.toString() << " at position " << token.location.column << "\n";
        i++;
    }
    
    return 0;
}