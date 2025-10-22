#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        std::cout << "Test 1: Simple variable assignment in switch" << std::endl;
        engine->execute(R"(
            auto x = 2;
            auto result = "initial";
            switch (x) {
                case 2:
                    result = "matched";
            }
        )");
        
        auto result = engine->execute("result");
        std::cout << "Result after switch: " << result.to_string() << std::endl;
        
        std::cout << "\nTest 2: Switch with multiple statements" << std::endl;
        auto result2 = engine->execute(R"(
            auto y = 1;
            switch (y) {
                case 1:
                    print("Case 1");
            }
            "after switch"
        )");
        std::cout << "Result2: " << result2.to_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}