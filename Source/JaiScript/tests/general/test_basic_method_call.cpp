#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        std::cout << "Test 1: Simple method that returns a constant\n";
        eng->execute(R"(
            class Test {
                int getValue() { 
                    return 42; 
                }
            };
            var obj = Test();
            print("Object created");
            var result = obj.getValue();
            print("Method returned: " + to_string(result));
        )");
        
        std::cout << "\nTest 2: Method that uses 'this' explicitly\n";
        eng->execute(R"(
            class Test2 {
                int x = 10;
                int getX() { 
                    return this.x; 
                }
            };
            var obj2 = Test2();
            print("Object created");
            var result2 = obj2.getX();
            print("Method returned: " + to_string(result2));
        )");
        
        std::cout << "\nTest 3: Method that uses field implicitly\n";
        eng->execute(R"(
            class Test3 {
                int y = 20;
                int getY() { 
                    return y;  // Implicit this
                }
            };
            var obj3 = Test3();
            print("Object created");
            var result3 = obj3.getY();
            print("Method returned: " + to_string(result3));
        )");
        
        std::cout << "\nAll tests completed!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}