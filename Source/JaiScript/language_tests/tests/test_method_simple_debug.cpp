#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        std::cout << "Testing simple method call\n";
        
        eng->execute(R"(
            class TestClass {
                int getValue() {
                    print("getValue() called");
                    return 42;
                }
            };
            
            var obj = TestClass();
            print("Calling getValue directly:");
            var result = obj.getValue();
            print("Result: " + to_string(result));
        )");
        
        std::cout << "\nNow test method calling method:\n";
        
        eng->execute(R"(
            class TestClass2 {
                int getValue() {
                    print("getValue() called");
                    return 42;
                }
                
                int callGetValue() {
                    print("callGetValue() called");
                    print("About to call getValue");
                    var result = getValue();
                    print("getValue returned: " + to_string(result));
                    return result;
                }
            };
            
            var obj2 = TestClass2();
            print("Calling callGetValue:");
            var result2 = obj2.callGetValue();
            print("Final result: " + to_string(result2));
        )");
        
        std::cout << "\nAll tests completed!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}