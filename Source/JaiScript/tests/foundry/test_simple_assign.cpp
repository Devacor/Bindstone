#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        // Test simple assignment to pair field
        std::cout << "Testing simple assignment..." << std::endl;
        
        auto result = engine->execute(R"(
            auto m = {"key": 42};
            
            for (auto& kv : m) {
                print("Initial: " + to_string(kv.second));
                kv.second = 100;
                print("After simple assign: " + to_string(kv.second));
                print("Map value: " + to_string(m["key"]));
            }
            
            m["key"]
        )");
        std::cout << "Result: " << result.as<int>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}