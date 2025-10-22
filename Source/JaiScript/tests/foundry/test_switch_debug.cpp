#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        std::cout << "Testing switch with separate result query..." << std::endl;
        
        // First execute the switch
        engine->execute(R"(
            auto x = 2;
            auto result = "";
            switch (x) {
                case 1:
                    result = "one";
                case 2:
                    result = "two";
                case 3:
                    result = "three";
                default:
                    result = "other";
            }
        )");
        
        // Then query the result variable
        auto result = engine->execute("result");
        std::cout << "Result after switch: " << result.to_string() << std::endl;
        
        if (result.is_string()) {
            std::cout << "Success! Got: " << result.as<std::string>() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
