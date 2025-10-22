#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        // Test accessing pair members
        std::cout << "Testing pair member access..." << std::endl;
        auto result = engine->execute(R"(
            auto m = {"key": 42};
            auto val = 0;
            for (auto& kv : m) {
                print("About to access kv.first");
                auto k = kv.first;
                print("Got key: " + k);
                print("About to access kv.second");
                val = kv.second;  // This line crashes
                print("Got value");
            }
            val
        )");
        std::cout << "Result: " << result.as<int>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}