#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    auto engine = jai::engine::make();
    
    // Define a variable at top level
    engine->execute("auto x = 42;");
    std::cout << "Defined x = 42" << std::endl;
    
    // Try to access it in next execute
    try {
        auto result = engine->execute("x");
        std::cout << "x = " << result.as<int>() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
    }
    
    // Try to modify it
    try {
        engine->execute("x = 100;");
        std::cout << "Modified x to 100" << std::endl;
        auto result = engine->execute("x");
        std::cout << "x = " << result.as<int>() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
    }
    
    return 0;
}
