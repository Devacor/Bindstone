#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        std::cout << "Step 1: Define class with method\n";
        eng->execute(R"(
            class Test {
                int getValue() { 
                    return 42; 
                }
            };
            print("Class defined");
        )");
        
        std::cout << "\nStep 2: Create object\n";
        eng->execute(R"(
            var obj = Test();
            print("Object created");
        )");
        
        std::cout << "\nStep 3: Access method\n";
        eng->execute(R"(
            var method = obj.getValue;
            print("Method accessed");
            print("Method type: " + type_of(method));
        )");
        
        std::cout << "\nStep 4: Call method\n";
        eng->execute(R"(
            var result = obj.getValue();
            print("Method called, result: " + to_string(result));
        )");
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}