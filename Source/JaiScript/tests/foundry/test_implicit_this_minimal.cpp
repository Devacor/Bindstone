#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        std::cout << "Testing method calling without 'this.'\n";
        
        std::cout << "Testing basic class creation...\n";
        
        eng->execute(R"(
            class TestClass {
                int x = 10;
                
                int getX() {
                    return this.x;  // Using explicit this first
                }
                
                int getXImplicit() {
                    return x;  // Using implicit this
                }
            };
            print("Class defined");
        )");
        
        std::cout << "Class definition works\n";
        
        std::cout << "Testing constructor call\n";
        
        eng->execute(R"(
            var obj = TestClass();
            print("Object created");
            print("x = " + to_string(obj.getX()));
            print("x implicit = " + to_string(obj.getXImplicit()));
        )");
        
        std::cout << "Object creation works\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}