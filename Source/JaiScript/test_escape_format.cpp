#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    jai::stdlib::register_all(engine);
    
    std::cout << "Testing format escaping:\n" << std::endl;
    
    // Test 1: {{0}} - should print {0}
    std::cout << "1. {{0}} escaping: ";
    engine.execute(R"(
        auto s = format("{{0}}");
        print("Result: '", s, "'");
    )");
    
    // Test 2: {{{0}}} - should print {value}
    std::cout << "2. {{{0}}} escaping: ";
    engine.execute(R"(
        auto s = format("{{{0}}}", "REPLACED");
        print("Result: '", s, "'");
    )");
    
    // Test 3: }} escaping
    std::cout << "3. }} escaping: ";
    engine.execute(R"(
        auto s = format("Test }}");
        print("Result: '", s, "'");
    )");
    
    // Test 4: Complex escaping
    std::cout << "4. Complex: ";
    engine.execute(R"(
        auto s = format("Use {{}} for {0} and }}}} for }}", "placeholders");
        print("Result: '", s, "'");
    )");
    
    return 0;
}