#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        std::cout << "=== Testing method to method calls ===\n";
        
        // Test 1: Verify basic method works
        std::cout << "\nTest 1: Basic method call\n";
        eng->execute(R"(
            class Test1 {
                int getValue() { 
                    print("getValue called");
                    return 42; 
                }
            };
            
            var obj1 = Test1();
            print("Calling getValue directly");
            var result1 = obj1.getValue();
            print("Result: " + to_string(result1));
        )");
        
        // Test 2: Method using explicit this
        std::cout << "\nTest 2: Method calling another with explicit this\n";
        eng->execute(R"(
            class Test2 {
                int getValue() { 
                    print("getValue called");
                    return 42; 
                }
                
                int callWithThis() { 
                    print("callWithThis called");
                    return this.getValue();  // Explicit this
                }
            };
            
            var obj2 = Test2();
            var result2 = obj2.callWithThis();
            print("Result: " + to_string(result2));
        )");
        
        // Test 3: Debug what happens with implicit call
        std::cout << "\nTest 3: Debug implicit method call\n";
        eng->execute(R"(
            class Test3 {
                int getValue() { 
                    print("getValue called");
                    return 42; 
                }
                
                int debugCall() { 
                    print("debugCall called");
                    print("Type of this: " + type_of(this));
                    
                    // Try to see if getValue is accessible
                    try {
                        print("About to call getValue");
                        var result = getValue();  // Implicit this
                        print("getValue returned: " + to_string(result));
                        return result;
                    } catch (e) {
                        print("Error calling getValue: " + e);
                        
                        // Try to access it as a field/method on this
                        print("Trying this.getValue");
                        return this.getValue();
                    }
                }
            };
            
            var obj3 = Test3();
            var result3 = obj3.debugCall();
            print("Final result: " + to_string(result3));
        )");
        
        std::cout << "\nAll tests completed!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}