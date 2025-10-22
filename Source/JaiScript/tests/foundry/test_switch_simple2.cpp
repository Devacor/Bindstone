#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        std::cerr << "About to execute script with switch...\n";
        auto result = engine->execute(R"(
            switch (2) {
                case 2:
                    42;
            }
        )");
        
        std::cout << "Script executed, result: " << result.to_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
