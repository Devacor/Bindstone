#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        std::cout << "Testing method calling another method without 'this.'\n";
        
        // Simple test - one method calls another
        auto result = eng->execute(R"(
            class Test {
                int getValue() { 
                    print("getValue called");
                    return 42; 
                }
                
                int callGetValue() { 
                    print("callGetValue called");
                    return getValue();  // This should work with implicit this
                }
            };
            
            var obj = Test();
            print("Object created");
            var result = obj.callGetValue();
            print("Result: " + to_string(result));
            result
        )");
        
        std::cout << "Final result: " << result.as<int>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}