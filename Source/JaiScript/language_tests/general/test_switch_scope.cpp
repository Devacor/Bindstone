#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        // First, verify if statements work correctly
        engine->execute(R"(
            function test_if() {
                auto result = "initial";
                if (true) {
                    result = "changed";
                }
                return result;
            }
        )");
        
        auto if_result = engine->execute("test_if()");
        std::cout << "If result: " << if_result.to_string() << std::endl;
        
        // Now test switch
        engine->execute(R"(
            function test_switch() {
                auto result = "initial";
                switch (2) {
                    case 2:
                        result = "two";
                }
                return result;
            }
        )");
        
        auto switch_result = engine->execute("test_switch()");
        std::cout << "Switch result: " << switch_result.to_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
