#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/types.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        try {
            engine->execute(R"(
                fallthrough;
            )");
            std::cout << "ERROR: Should have thrown parse_error" << std::endl;
        } catch (const jai::parse_error& e) {
            std::cout << "SUCCESS: Got parse_error: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cout << "ERROR: Got wrong exception type: " << typeid(e).name() << " - " << e.what() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Outer error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
