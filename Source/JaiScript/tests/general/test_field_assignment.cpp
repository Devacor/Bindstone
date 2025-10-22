#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(*engine);
        
        std::cout << "Testing field assignment in constructor" << std::endl;
        
        // First test explicit this
        const char* code1 = R"(
            class Test {
                string name = "default";
                
                Test(string n) {
                    print("Setting with explicit this");
                    this.name = n;
                }
            }
            
            auto t = Test("hello");
            print("Result: " + t.name);
        )";
        
        engine->execute(code1);
        
        // Now test implicit
        const char* code2 = R"(
            class Test2 {
                string name2 = "default2";
                
                Test2(string n) {
                    print("Setting with implicit this");
                    name2 = n;
                }
            }
            
            auto t2 = Test2("world");
            print("Result: " + t2.name2);
        )";
        
        engine->execute(code2);
        
        std::cout << "All tests passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}