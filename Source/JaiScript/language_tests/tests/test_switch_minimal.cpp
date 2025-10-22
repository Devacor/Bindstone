#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        engine->execute(R"(
            auto x = 2;
            auto result = "initial";
            
            switch (x) {
                case 2:
                    result = "two";
            }
        )");
        
        auto result = engine->execute("result");
        std::cout << "Result after switch: " << result.to_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
