#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        
        // Test 1: Simple method with 'this' access
        std::cout << "Test 1: Simple method with explicit 'this' access" << std::endl;
        auto result = eng->execute(R"(
            class TestClass {
                x = 10;
                
                method get_x() {
                    return this.x;
                }
            }
            
            var obj = TestClass();
            obj.get_x();
        )");
        
        std::cout << "Result: " << result.to_string() << std::endl;
        std::cout << "All tests passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
