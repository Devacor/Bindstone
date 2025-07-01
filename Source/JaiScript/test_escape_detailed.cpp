#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    jai::stdlib::register_all(engine);
    
    std::cout << "Detailed escape testing:\n" << std::endl;
    
    // Test each escape pattern individually
    std::cout << "1. Single { escape (invalid, should stay as-is): ";
    engine.execute(R"(print("Result: '", format("{"), "'");)");
    
    std::cout << "2. Single } (should stay as-is): ";
    engine.execute(R"(print("Result: '", format("}"), "'");)");
    
    std::cout << "3. {{ escape (should become {): ";
    engine.execute(R"(print("Result: '", format("{{"), "'");)");
    
    std::cout << "4. }} escape (should become }): ";
    engine.execute(R"(print("Result: '", format("}}"), "'");)");
    
    std::cout << "5. {{}} (should become {}): ";
    engine.execute(R"(print("Result: '", format("{{}}"), "'");)");
    
    std::cout << "6. {{0}} (should become {0}): ";
    engine.execute(R"(print("Result: '", format("{{0}}"), "'");)");
    
    std::cout << "7. {{{0}}} with arg (should become {VALUE}): ";
    engine.execute(R"(print("Result: '", format("{{{0}}}", "TEST"), "'");)");
    
    std::cout << "8. {{{{}}}} (should become {{}}): ";
    engine.execute(R"(print("Result: '", format("{{{{}}}}"), "'");)");
    
    std::cout << "9. Mixed: a{{b}}c (should become a{b}c): ";
    engine.execute(R"(print("Result: '", format("a{{b}}c"), "'");)");
    
    std::cout << "10. Format with escapes: {{}} = {} (should become {} = VALUE): ";
    engine.execute(R"(print("Result: '", format("{{}} = {}", "REPLACED"), "'");)");
    
    return 0;
}