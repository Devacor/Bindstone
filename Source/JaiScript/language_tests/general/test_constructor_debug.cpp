#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(*engine);
        
        std::cout << "Testing simple constructor with parameter" << std::endl;
        
        const char* code = R"(
            class Test {
                string name = "default";
                
                Test(string n) {
                    print("Constructor called with: " + n);
                    name = n;
                }
                
                string getName() {
                    return name;
                }
            }
            
            auto t = Test("hello");
            print("Object name: " + t.getName());
        )";
        
        engine->execute(code);
        std::cout << "Test passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}