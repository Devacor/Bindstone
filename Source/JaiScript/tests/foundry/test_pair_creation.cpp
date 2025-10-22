#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        // Test pair creation
        std::cout << "Testing pair creation..." << std::endl;
        auto result = engine->execute(R"(
            auto m = {"key": 42};
            print("Map created");
            
            for (auto& kv : m) {
                print("In loop");
                print("kv type: " + type_of(kv));
                print("Has first: " + to_string(kv.first != null));
                print("Has second: " + to_string(kv.second != null));
                if (kv.first != null) {
                    print("First: " + kv.first);
                }
                if (kv.second != null) {
                    print("Second: " + to_string(kv.second));
                }
            }
            
            true
        )");
        std::cout << "Result: " << result.as<bool>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}