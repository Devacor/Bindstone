#include <iostream>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

using namespace JaiScript;

bool testSingleCase(const std::string& code, bool shouldSucceed) {
    Lexer lexer(code, "test.jai");
    auto tokens = lexer.tokenize();
    Parser parser(tokens, "test.jai");
    auto ast = parser.parse();
    
    bool success = !parser.hasErrors();
    bool testPassed = (success == shouldSucceed);
    
    if (!testPassed) {
        std::cout << "\nFAILED TEST:\n";
        std::cout << "Code: " << code << "\n";
        std::cout << "Expected: " << (shouldSucceed ? "SUCCESS" : "FAILURE") << "\n";
        std::cout << "Got: " << (success ? "SUCCESS" : "FAILURE") << "\n";
        if (!success) {
            std::cout << "Errors:\n";
            for (const auto& error : parser.getErrors()) {
                std::cout << "  " << error << "\n";
            }
        }
    }
    
    return testPassed;
}

int main() {
    int passed = 0;
    int failed = 0;
    
    // Test the specific nested generic case
    if (testSingleCase("void nested(map<string, array<int>>& data) { }", true)) {
        passed++;
    } else {
        failed++;
    }
    
    // Test more edge cases
    if (testSingleCase("void f(map<int, int>> x) { }", false)) { // Extra >
        passed++;
    } else {
        failed++;
    }
    
    if (testSingleCase("void f(map<int, int> x) { }", false)) { // Missing >
        passed++;
    } else {
        failed++;
    }
    
    if (testSingleCase("void f(array<array<int>> x) { }", true)) { // >> at end
        passed++;
    } else {
        failed++;
    }
    
    std::cout << "\nTest Results: " << passed << " passed, " << failed << " failed\n";
    
    return failed > 0 ? 1 : 0;
}