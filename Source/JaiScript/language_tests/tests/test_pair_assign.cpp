#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        // Test assignment to pair field
        std::cout << "Testing pair field assignment..." << std::endl;
        
        auto result = engine->execute(R"(
            auto m = {"key": 42};
            
            for (auto& kv : m) {
                print("Initial value: " + to_string(kv.second));
                
                // Try simple assignment first
                kv.second = 100;
                print("After assignment: " + to_string(kv.second));
                
                // Try compound assignment
                try {
                    kv.second += 10;
                    print("After compound: " + to_string(kv.second));
                } catch (e) {
                    print("Compound assignment error: " + to_string(e));
                }
            }
            
            print("Final map value: " + to_string(m["key"]));
            m["key"]
        )");
        std::cout << "Result: " << result.as<int>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}