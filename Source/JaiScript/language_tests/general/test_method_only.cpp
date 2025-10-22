#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        std::cout << "Defining class...\n";
        auto result = eng->execute(R"(
            class Test {
                int getValue() { 
                    return 42; 
                }
                int callGetValue() {
                    return getValue();  // Implicit method call
                }
            };
            print("Class defined");
        )");
        
        std::cout << "Class definition result is null: " << result.is_null() << "\n";
        
        std::cout << "\nCreating object and calling method:\n";
        eng->execute(R"(
            var obj = Test();
            print("Object created");
            var result = obj.callGetValue();
            print("Result: " + to_string(result));
        )");
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}