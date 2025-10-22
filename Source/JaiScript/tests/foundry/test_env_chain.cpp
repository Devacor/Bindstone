#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        std::cout << "Testing environment chain\n";
        
        // Create a simple function that uses a variable
        eng->execute(R"(
            var global_var = 100;
            
            function test_lookup() {
                print("Looking up global_var");
                return global_var;
            }
            
            var result = test_lookup();
            print("Result: " + to_string(result));
        )");
        
        std::cout << "\nNow test in method context:\n";
        
        eng->execute(R"(
            class TestClass {
                int method_var = 200;
                
                int test_method() {
                    print("In test_method");
                    print("Looking up method_var");
                    return method_var;
                }
            };
            
            var obj = TestClass();
            var result2 = obj.test_method();
            print("Method result: " + to_string(result2));
        )");
        
        std::cout << "\nAll tests completed!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}