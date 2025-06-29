#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    try {
        jai::engine engine;
        std::cout << "Engine created\n";
        
        auto result = engine.execute("42;");
        std::cout << "Result: " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}