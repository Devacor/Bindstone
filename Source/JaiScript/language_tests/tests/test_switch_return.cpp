#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        auto result = engine->execute(R"(
            auto x = 2;
            auto result = "initial";
            switch (x) {
                case 2:
                    result = "two";
            }
            result
        )");
        
        std::cout << "Result is null: " << result.is_null() << std::endl;
        std::cout << "Result is string: " << result.is_string() << std::endl;
        std::cout << "Result value: " << result.to_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
