#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        
        // Test 1: Define class
        std::cout << "=== Defining class ===" << std::endl;
        eng->execute(R"(
            class TestClass {
                x = 10;
                
                method get_x() {
                    print("In get_x method");
                    return this.x;
                }
            }
        )");
        
        // Test 2: Create instance
        std::cout << "=== Creating instance ===" << std::endl;
        auto obj = eng->execute("TestClass()");
        std::cout << "Instance created, type: " << obj.type() << std::endl;
        
        // Test 3: Call method
        std::cout << "=== Calling method ===" << std::endl;
        auto result = eng->execute(R"(
            var obj = TestClass();
            obj.get_x()
        )");
        
        std::cout << "Result: " << result.to_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
