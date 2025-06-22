#include <iostream>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

using namespace JaiScript;

bool testLine(const std::string& code) {
    Lexer lexer(code, "test.jai");
    auto tokens = lexer.tokenize();
    Parser parser(tokens, "test.jai");
    auto ast = parser.parse();
    bool success = !parser.hasErrors();
    
    if (!success) {
        std::cout << "FAILED: " << code << "\n";
        for (const auto& error : parser.getErrors()) {
            std::cout << "  " << error << "\n";
        }
    }
    
    return success;
}

int main() {
    std::vector<std::string> testCases = {
        "void foo(int& x) { }",
        "void bar(int& x, float& y) { }",
        "void process(MyClass& obj) { }",
        "void handle(string& str) { }",
        "void work(array<int>& arr) { }",
        "void mixed(int val, string& ref, bool flag) { }",
        "void complex(A& a, B& b, C c, D& d) { }",
        "void nested(map<string, array<int>>& data) { }",
        "void multi(array<float>& arr) { }",
        "int& getRef() { return x; }",
        "string& getName() { return name; }",
        "auto f = [](int& x) { return x * 2; };",
        "auto g = [](string& s1, string& s2) { return s1 + s2; };",
        "auto h = [](int a, float& b, char c) { return a + b; };",
        "auto i = [&total](int& x) { total += x; };",
        "int& x = getValue();",
        "auto& y = getValue();"
    };
    
    int passed = 0;
    int failed = 0;
    
    for (const auto& testCase : testCases) {
        if (testLine(testCase)) {
            passed++;
        } else {
            failed++;
        }
    }
    
    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}