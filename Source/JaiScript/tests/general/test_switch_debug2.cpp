#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        std::cout << "Testing switch case execution..." << std::endl;
        
        engine->execute(R"(
            auto x = 2;
            auto result = "initial";
            print("Before switch: " + result);
            
            switch (x) {
                case 1:
                    print("In case 1");
                    result = "one";
                case 2:
                    print("In case 2");
                    result = "two";
                    print("After assignment in case 2: " + result);
                case 3:
                    print("In case 3");
                    result = "three";
                default:
                    print("In default");
                    result = "other";
            }
            
            print("After switch: " + result);
        )");
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
