#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

using namespace jai;

int main() {
    try {
        auto e = engine::make();
        jai::stdlib::register_all(e);
        
        std::cout << "Testing constructor with field assignment\n";
        e->execute(R"(
            class Test {
                int value = 0;
                Test(int v) {
                    value = v;
                }
            }
            auto t = Test(42);
            print("t.value = " + to_string(t.value));
        )");
        
        std::cout << "\nTest completed successfully\n";
        
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    
    return 0;
}