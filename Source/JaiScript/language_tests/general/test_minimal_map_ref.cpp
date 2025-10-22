#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        // Even simpler test - just iterate without modifying
        std::cout << "Testing minimal reference iteration..." << std::endl;
        auto result = engine->execute(R"(
            auto m = {"key": 42};
            auto found = false;
            for (auto& kv : m) {
                found = true;
            }
            found
        )");
        std::cout << "Result: " << result.as<bool>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}