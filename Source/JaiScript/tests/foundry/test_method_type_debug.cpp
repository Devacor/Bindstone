#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        std::cout << "Define class with method\n";
        eng->execute(R"(
            class Test {
                int getValue() { 
                    return 42; 
                }
            };
            print("Class defined");
        )");
        
        std::cout << "\nCreate object and check method type\n";
        eng->execute(R"(
            var obj = Test();
            print("Object created");
            
            // Access the method without calling it
            var method = obj.getValue;
            print("Method accessed");
            print("Method type: " + type_of(method));
            
            // Check if it's a function
            print("Is function: " + to_string(type_of(method) == "function"));
        )");
        
        std::cout << "\nNow try to call the method\n";
        eng->execute(R"(
            try {
                var result = method();
                print("Method called successfully, result: " + to_string(result));
            } catch (e) {
                print("Error calling method: " + e);
            }
        )");
        
        std::cout << "\nDone!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}