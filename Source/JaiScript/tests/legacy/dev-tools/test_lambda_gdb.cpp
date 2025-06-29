#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    
    std::cout << "About to execute lambda with parameter...\n";
    
    try {
        auto result = engine.execute(R"(
            var f = fun(:x) -> { return x; };
            f(42);
        )");
        std::cout << "Result: " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}