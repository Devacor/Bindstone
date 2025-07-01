#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    jai::stdlib::register_all(engine);
    
    std::cout << "Testing new format detection (only {} and {NUMBER}):\n" << std::endl;
    
    std::cout << "=== Should use FORMAT mode ===" << std::endl;
    engine.execute(R"(
        print("1. {} placeholder: ", format("Hello, {}!", "World"));
        print("2. {0} placeholder: ", format("{0} {0} {0}", "Test"));
        print("3. {1} {0} placeholders: ", format("{1} before {0}", "A", "B"));
        print("4. Mixed: ", format("Item {}: {} units", "apple", 5));
    )");
    
    std::cout << "\n=== Should use CONCAT mode ===" << std::endl;
    engine.execute(R"(
        print("1. JSON object: ", format("{\"key\": \"value\"}"));
        print("2. With args: ", format("{\"name\": \"", "John", "\"}"));
        print("3. Invalid placeholder: ", format("{abc}", "ignored"));
        print("4. Partial brace: ", format("{", "test"));
        print("5. Space in braces: ", format("{ }", "ignored"));
        print("6. Non-numeric: ", format("{1a}", "ignored"));
    )");
    
    std::cout << "\n=== Edge cases ===" << std::endl;
    engine.execute(R"(
        print("1. Escaped with number: ", format("{{0}}", "ignored"));
        print("2. Mixed escaped/real: ", format("{{}} has {}", "value"));
        print("3. Leading zeros: ", format("{001}", "A", "B"));
        print("4. Empty string number: ", format("{}", "works"));
        print("5. Just braces: ", format("{", "}"));
    )");
    
    std::cout << "\n=== Print function (same rules) ===" << std::endl;
    engine.execute(R"(
        print("Format mode: {} and {}", "A", "B");
        print("Concat mode: {not-a-number} ", "concatenated");
    )");
    
    return 0;
}