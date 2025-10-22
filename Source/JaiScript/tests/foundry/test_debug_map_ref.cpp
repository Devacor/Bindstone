#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        std::cout << "Creating engine..." << std::endl;
        auto engine = jai::engine::make();
        
        std::cout << "Registering stdlib..." << std::endl;
        jai::stdlib::register_all(engine);
        
        // Test simple map iteration without reference first
        std::cout << "Testing simple map value iteration..." << std::endl;
        auto result1 = engine->execute(R"(
            auto scores = {"alice": 100};
            auto sum = 0;
            for (auto kv : scores) {
                sum += kv.second;
            }
            sum
        )");
        std::cout << "Simple iteration result: " << result1.as<int>() << std::endl;
        
        // Now test reference iteration
        std::cout << "Testing map reference iteration..." << std::endl;
        auto result2 = engine->execute(R"(
            auto scores = {"alice": 100};
            for (auto& kv : scores) {
                print("In loop, about to access kv.second");
                kv.second += 10;
            }
            scores["alice"]
        )");
        std::cout << "Reference iteration result: " << result2.as<int>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}