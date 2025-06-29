#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    
    // Test function with parameters
    try {
        engine.execute(R"(
            function add(:a, :b) -> {
                return a + b;
            }
        )");
        
        auto result = engine.execute("add(10, 20);");
        std::cout << "Function result: " << result.as_int() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Function error: " << e.what() << std::endl;
    }
    
    return 0;
}
