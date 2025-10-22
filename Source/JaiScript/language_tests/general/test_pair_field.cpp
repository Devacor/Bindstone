#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        // Test accessing pair fields
        std::cout << "Testing pair field access..." << std::endl;
        
        // First test non-reference iteration
        auto result1 = engine->execute(R"(
            auto m = {"key": 42};
            auto sum = 0;
            for (auto kv : m) {  // Non-reference iteration
                sum = kv.second + 10;
            }
            sum
        )");
        std::cout << "Non-reference iteration result: " << result1.as<int>() << std::endl;
        
        // Now test reference iteration
        auto result2 = engine->execute(R"(
            auto m = {"key": 42};
            auto found = false;
            for (auto& kv : m) {  // Reference iteration
                found = true;
                print("In reference loop");
                print("kv is: " + type_of(kv));
                
                // Try to access second directly
                auto val = kv.second;
                print("Got kv.second");
                print("Value is: " + to_string(val));
            }
            found
        )");
        std::cout << "Reference iteration result: " << result2.as<bool>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}