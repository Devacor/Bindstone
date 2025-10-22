#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        // First, verify print and type_of work properly
        std::cout << "Testing basic print and type_of:\n";
        eng->execute(R"(
            var x = 42;
            print("x = " + to_string(x));
            print("type of x: " + type_of(x));
            
            var f = []() -> int { return 100; };
            print("type of f: " + type_of(f));
        )");
        
        std::cout << "\nNow testing class method access:\n";
        eng->execute(R"(
            class Test {
                int getValue() { 
                    return 42; 
                }
            };
            
            var obj = Test();
            print("Object created, type: " + type_of(obj));
            
            // Try to access the method
            print("About to access obj.getValue");
            var method = obj.getValue;
            print("Method accessed");
            print("Checking method...");
            if (method == null) {
                print("Method is null!");
            } else {
                print("Method is not null");
                // Try to print its type
                var method_type = type_of(method);
                print("Method type: " + method_type);
            }
        )");
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}