#include <iostream>
#include <jaiscript/detail/lexer.hpp>

using namespace jai;

int main() {
    std::string code = "void nested(map<string, array<int>>& data) { }";
    std::cout << "Code: " << code << "\n\n";
    
    lexer lexer(code, "test.jai");
    auto tokens = lexer.tokenize();
    
    std::cout << "Tokens:\n";
    int i = 0;
    for (const auto& token : tokens) {
        std::cout << "  [" << i << "] " << token.to_string() << " at position " << token.location.column << "\n";
        i++;
    }
    
    return 0;
}