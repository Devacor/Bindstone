#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        std::cout << "Testing pair creation and member access..." << std::endl;
        
        // Test creating a pair and accessing its members
        auto result = engine->execute(R"(
            // Create a pair using the constructor
            auto p = pair("key", 42);
            
            // Try to access the second member
            p.second
        )");
        
        std::cout << "Result: " << result.as<int>() << std::endl;
        
        // Now test in a range-based for with reference
        std::cout << "Testing range-based for with map..." << std::endl;
        
        result = engine->execute(R"(
            auto scores = {"alice": 100};
            
            for (auto& kv : scores) {
                kv.second += 10;
            }
            
            scores["alice"]
        )");
        
        std::cout << "Modified score: " << result.as<int>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}