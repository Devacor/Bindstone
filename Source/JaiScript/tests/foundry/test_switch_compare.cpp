#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        auto val1 = engine->execute("2");
        auto val2 = engine->execute("2");
        
        std::cout << "val1: " << val1.to_string() << std::endl;
        std::cout << "val2: " << val2.to_string() << std::endl;
        
        try {
            bool equal = (val1 == val2);
            std::cout << "Equal: " << equal << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Comparison error: " << e.what() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
