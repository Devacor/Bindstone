#include <iostream>
#include <jaiscript/core/engine.hpp>

int main() {
    try {
        jai::engine eng;
        
        // Create and assign in one execute
        auto result = eng.execute(R"(
            auto myMap = {};
            myMap["key"] = 42;
            myMap["key"]
        )");
        
        std::cout << "Result type: " << static_cast<int>(result.type()) << "\n";
        if (!result.is_null()) {
            std::cout << "Value: " << result.as<int>() << "\n";
        } else {
            std::cout << "Result is null\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
    
    return 0;
}