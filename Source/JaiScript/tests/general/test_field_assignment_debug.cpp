#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    std::cout << "Testing field assignment...\n";
    
    auto engine = jai::engine::make();
    
    // Add print for debugging
    engine->add_variadic_function("print", [engine](const std::vector<jai::script_value>& args) {
        for (const auto& arg : args) {
            std::cout << arg.to_string() << " ";
        }
        std::cout << std::endl;
        return jai::script_value(std::monostate{}, engine->weak_from_this());
    });
    
    try {
        // Simple class with field
        engine->execute(R"(
            class TestClass {
                auto value = 10;
            }
            
            auto obj = TestClass();
            print("Initial value:", obj.value);
            
            // Try to assign - this is where it fails
            obj.value = 20;
            print("After assignment:", obj.value);
        )");
        
        std::cout << "Test passed!\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}