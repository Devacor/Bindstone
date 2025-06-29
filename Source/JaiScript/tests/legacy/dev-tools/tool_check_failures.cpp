#include <iostream>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

using namespace jai;

void testParse(const std::string& code, const std::string& testName) {
    std::cout << "\nTesting: " << testName << "\n";
    std::cout << "Code: " << code << "\n";
    
    lexer lexer(code, "test.jai");
    auto tokens = lexer.tokenize();
    parser parser(tokens, "test.jai");
    auto ast = parser.parse();
    
    if (parser.hasErrors()) {
        std::cout << "PARSE FAILED:\n";
        for (const auto& error : parser.getErrors()) {
            std::cout << "  " << error << "\n";
        }
    } else {
        std::cout << "PARSE SUCCESS\n";
    }
}

int main() {
    // Test all the complex cases that might be failing
    
    // Generic parameters
    testParse("void simple(array<int> arr) { }", "simple generic param");
    testParse("void multi(array<float>& arr) { }", "generic param with ref");
    testParse("void nested(map<string, array<int>>& data) { }", "nested generic with ref");
    testParse("void veryNested(map<string, map<int, array<float>>>& data) { }", "very nested generic");
    
    // Edge cases
    testParse("void edgeCase(array<array<int>> arr) { }", "array of arrays");
    testParse("void mapMap(map<int, map<string, float>> m) { }", "map of maps");
    
    return 0;
}