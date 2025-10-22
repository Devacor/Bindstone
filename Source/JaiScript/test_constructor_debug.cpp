#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

using namespace jai;

int main() {
    try {
        auto e = engine::make();
        jai::stdlib::register_all(e);
        
        std::cout << "Test 1: Simple class without constructor\n";
        e->execute(R"(
            class Simple {
                int value = 42;
            }
            auto s = Simple();
            print("Simple value: " + to_string(s.value));
        )");
        
        std::cout << "\nTest 2: Class with empty constructor\n";
        e->execute(R"(
            class Empty {
                int value = 10;
                Empty() {
                    print("Empty constructor called");
                }
            }
            auto e = Empty();
            print("Empty value: " + to_string(e.value));
        )");
        
        std::cout << "\nTest 3: Class with constructor that modifies field\n";
        e->execute(R"(
            class Modify {
                int value = 0;
                Modify() {
                    print("Before assignment, value = " + to_string(value));
                    value = 42;
                    print("After assignment, value = " + to_string(value));
                }
            }
            auto m = Modify();
            print("Outside constructor, m.value = " + to_string(m.value));
        )");
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}