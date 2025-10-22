#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        auto result = engine->execute(R"(
            auto x = 2;
            auto result = "initial";
            // No switch, just return result
            result
        )");
        
        std::cout << "Result: " << result.to_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
