#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        std::cout << "Creating engine..." << std::endl;
        auto engine = jai::engine::make();
        
        std::cout << "Registering stdlib..." << std::endl;
        jai::stdlib::register_all(engine);
        
        std::cout << "Executing test script..." << std::endl;
        auto result = engine->execute(R"(
            auto scores = {"alice": 100, "bob": 85, "charlie": 92};
            
            // This causes segfault - reference iteration over map
            for (auto& kv : scores) {
                kv.second += 10;
            }
            
            scores["alice"]
        )");
        
        std::cout << "Result: " << result.as<int>() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}