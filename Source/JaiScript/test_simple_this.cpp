#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        
        std::cout << "=== Defining class with method that uses 'this' ===" << std::endl;
        eng->execute(R"(
            class TestClass {
                x = 10;
                
                method get_x() {
                    return this.x;
                }
            }
        )");
        
        std::cout << "Class defined successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
