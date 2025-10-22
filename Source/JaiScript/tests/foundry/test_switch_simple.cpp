#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        std::cout << "Testing basic integer switch..." << std::endl;
        auto result = engine->execute(R"(
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
            result
        )");
        
        
        std::cout << "Result as string: " << result.to_string() << std::endl;
        
        if (result.is_string()) {
            std::string str_result = result.as<std::string>();
            std::cout << "String value: " << str_result << std::endl;
        } else {
            std::cout << "Result is not a string!" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
