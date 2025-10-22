#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        
        // Register print function
        eng->add_variadic_function("print", [](const std::vector<jai::script_value>& args) {
            for (const auto& arg : args) {
                std::cout << arg.to_string();
            }
            std::cout << std::endl;
            return jai::script_value::make_null(args[0].get_engine_ref());
        });
        
        // Test without 'this' first
        std::cout << "=== Test 1: Method without 'this' ===" << std::endl;
        eng->execute(R"(
            class TestClass {
                x = 10;
                
                method simple() {
                    print("In simple method");
                    return 42;
                }
            }
            
            var obj = TestClass();
            print("Calling obj.simple()...");
            var result = obj.simple();
            print("Result: " + to_string(result));
        )");
        
        std::cout << "Test 1 passed!" << std::endl;
        
        // Now test with 'this'
        std::cout << "\n=== Test 2: Method with 'this' ===" << std::endl;
        eng->execute(R"(
            class TestClass2 {
                y = 20;
                
                method get_y() {
                    print("In get_y method, about to access this.y");
                    return this.y;
                }
            }
            
            var obj2 = TestClass2();
            print("Calling obj2.get_y()...");
            var result2 = obj2.get_y();
            print("Result: " + to_string(result2));
        )");
        
        std::cout << "All tests passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
