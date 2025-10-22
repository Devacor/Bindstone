#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        // Test what happens when we try to set a reference field
        std::cout << "Testing reference field assignment..." << std::endl;
        auto result = engine->execute(R"(
            auto m = {"key": 42};
            
            for (auto& kv : m) {
                print("kv.second type: " + type_of(kv.second));
                print("kv.second value: " + to_string(kv.second));
                
                // Try direct assignment first
                try {
                    kv.second = 100;
                    print("Direct assignment succeeded");
                } catch (e) {
                    print("Direct assignment failed: " + to_string(e));
                }
                
                print("After assignment: " + to_string(kv.second));
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