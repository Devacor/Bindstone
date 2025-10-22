#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        try {
            auto result = engine->execute(R"(
                switch (2) {
                    case 2:
                        42;
                }
            )");
            std::cout << "Result: " << result.to_string() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Inner exception: " << e.what() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Outer exception: " << e.what() << std::endl;
    }
    return 0;
}
