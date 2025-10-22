#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        std::cout << "Testing method calling without 'this.'\n";
        
        try {
            auto result = eng->execute(R"(
                class Test {
                    int getValue() { 
                        print("getValue called");
                        return 42; 
                    }
                    int callMethod() { 
                        print("callMethod called");
                        print("About to call getValue");
                        int result = getValue();
                        print("getValue returned: " + to_string(result)); 
                        return result;
                    }
                };
            )");
            std::cout << "Class defined successfully\n";
            std::cout << "Result is null: " << result.is_null() << "\n";
            
            // Now test creating an object and calling the method
            eng->execute(R"(
                var obj = Test();
                print("Object created");
                print("Calling method: " + to_string(obj.callMethod()));
            )");
            
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}