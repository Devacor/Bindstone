#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        // Create a simple test that should trigger the conversion error
        std::cout << "Testing problematic conversion..." << std::endl;
        
        // First, let's see what happens with a simpler case
        auto result1 = engine->execute(R"(
            auto val = 42;
            auto ref_val = val;  // This creates a copy, not a reference
            ref_val += 10;
            ref_val
        )");
        std::cout << "Simple case result: " << result1.as<int>() << std::endl;
        
        // Now test with the pair's reference field
        try {
            auto result2 = engine->execute(R"(
                auto m = {"key": 100};
                for (auto& kv : m) {
                    // kv.second is a reference to the map value
                    // Let's see what type it thinks it is
                    print("Type of kv.second: " + type_of(kv.second));
                    
                    // Try to use it in arithmetic
                    auto temp = kv.second + 10;
                    print("Addition worked: " + to_string(temp));
                    
                    // Now try compound assignment
                    kv.second += 10;
                    print("Compound assignment worked");
                }
                m["key"]
            )");
            std::cout << "Map iteration result: " << result2.as<int>() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Expected error: " << e.what() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Unexpected exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}