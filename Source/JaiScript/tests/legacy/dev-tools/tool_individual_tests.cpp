#include <iostream>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

using namespace jai;

// Define the test framework
#define JAI_TEST(name) void name()

bool testSucceeds(const std::string& code) {
    lexer lexer(code, "test.jai");
    auto tokens = lexer.tokenize();
    parser parser(tokens, "test.jai");
    auto ast = parser.parse();
    return !parser.hasErrors();
}

void assertParseSucceeds(const std::string& code) {
    if (!testSucceeds(code)) {
        std::cout << "FAILED: Expected success for: " << code << "\n";
    }
}

void assertParseFails(const std::string& code) {
    if (testSucceeds(code)) {
        std::cout << "FAILED: Expected failure for: " << code << "\n";
    }
}

// The const_reference_parameters test
JAI_TEST(const_reference_parameters) {
    // Parameters without const
    assertParseSucceeds("void process(int& x) { }");
    assertParseSucceeds("void handle(string& str) { }");
    assertParseSucceeds("void work(array<int>& arr) { }");
    
    // Mix of parameter types
    assertParseSucceeds("void mixed(int val, string& ref, bool flag) { }");
    assertParseSucceeds("void complex(A& a, B& b, C c, D& d) { }");
    
    // Nested template types
    assertParseSucceeds("void nested(map<string, array<int>>& data) { }");
    assertParseSucceeds("void multi(array<float>& arr) { }");
    
    // Return ref
    assertParseSucceeds("int& getRef() { return x; }");
    assertParseSucceeds("string& get_name() { return name; }");
    
    // Lambda with ref
    assertParseSucceeds("auto f = [](int& x) { return x * 2; };");
    assertParseSucceeds("auto g = [](string& s1, string& s2) { return s1 + s2; };");
    assertParseSucceeds("auto h = [](int a, float& b, char c) { return a + b; };");
    assertParseSucceeds("auto i = [&total](int& x) { total += x; };");
    
    // Variable declarations with references
    assertParseSucceeds("int& x = getscript_value();");
    assertParseSucceeds("auto& y = getscript_value();");
}

int main() {
    std::cout << "Running const_reference_parameters test...\n";
    const_reference_parameters();
    std::cout << "\nTest completed.\n";
    return 0;
}