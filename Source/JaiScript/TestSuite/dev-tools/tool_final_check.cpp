#include <iostream>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

using namespace JaiScript;

void testCode(const std::string& code, const std::string& desc) {
    std::cout << "\nTesting " << desc << ": " << code << "\n";
    
    Lexer lexer(code, "test.jai");
    auto tokens = lexer.tokenize();
    Parser parser(tokens, "test.jai");
    auto ast = parser.parse();
    
    if (parser.hasErrors()) {
        std::cout << "FAILED:\n";
        for (const auto& error : parser.getErrors()) {
            std::cout << "  " << error << "\n";
        }
    } else {
        std::cout << "SUCCESS\n";
    }
}

int main() {
    // Test the specific cases that might be failing
    testCode("void nested(map<string, array<int>>& data) { }", "nested generic");
    testCode("void multi(array<float>& arr) { }", "array float");
    testCode("int& x = getValue();", "int ref decl");
    testCode("auto& y = getValue();", "auto ref decl");
    
    return 0;
}