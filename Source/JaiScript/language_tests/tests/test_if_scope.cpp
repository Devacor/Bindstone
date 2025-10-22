#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        engine->execute(R"(
            auto result = "initial";
            print("Before if: " + result);
            
            if (true) {
                result = "changed";
                print("Inside if: " + result);
            }
            
            print("After if: " + result);
        )");
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
