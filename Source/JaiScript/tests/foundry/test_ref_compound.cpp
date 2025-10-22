#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        // Test compound assignment
        std::cout << "Testing compound assignment..." << std::endl;
        auto result = engine->execute(R"(
            auto m = {"key": 42};
            print("Initial value: " + to_string(m["key"]));
            
            for (auto& kv : m) {
                print("In loop, value: " + to_string(kv.second));
                kv.second += 10;  // Compound assignment
                print("After +=, value: " + to_string(kv.second));
            }
            
            print("Final value: " + to_string(m["key"]));
            m["key"]
        )");
        std::cout << "Result: " << result.as<int>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}