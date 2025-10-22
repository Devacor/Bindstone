#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        std::cout << "Testing explicit vs implicit method calls\n";
        
        // First confirm explicit this. works
        eng->execute(R"(
            class TestExplicit {
                int getValue() { 
                    print("getValue called");
                    return 42; 
                }
                
                int callWithExplicit() { 
                    print("callWithExplicit called");
                    return this.getValue();  // Explicit this
                }
            };
            
            var obj1 = TestExplicit();
            print("Calling with explicit this:");
            var result1 = obj1.callWithExplicit();
            print("Result: " + to_string(result1));
        )");
        
        std::cout << "\nNow testing implicit call:\n";
        
        // Now test implicit
        eng->execute(R"(
            class TestImplicit {
                int getValue() { 
                    print("getValue called");
                    return 42; 
                }
                
                int callWithImplicit() { 
                    print("callWithImplicit called");
                    // Let's trace what happens
                    print("About to look up getValue");
                    return getValue();  // Implicit this
                }
            };
            
            var obj2 = TestImplicit();
            print("Calling with implicit:");
            var result2 = obj2.callWithImplicit();
            print("Result: " + to_string(result2));
        )");
        
        std::cout << "\nAll tests completed!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}