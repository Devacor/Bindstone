#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(*engine);
        
        std::cout << "Testing simple field access" << std::endl;
        
        // Test reading field with explicit this
        const char* code1 = R"(
            class Test {
                string name = "default";
                
                string getName() {
                    return this.name;
                }
            }
            
            auto t = Test();
            print("Explicit: " + t.getName());
        )";
        
        engine->execute(code1);
        
        // Test reading field with implicit this
        const char* code2 = R"(
            class Test2 {
                string name2 = "default2";
                
                string getName() {
                    return name2;  // Implicit this
                }
            }
            
            auto t2 = Test2();
            print("Implicit: " + t2.getName());
        )";
        
        engine->execute(code2);
        
        std::cout << "All tests passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}