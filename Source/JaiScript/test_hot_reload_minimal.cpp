#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/io.hpp>
#include <iostream>

int main() {
    try {
        auto engine = jai::engine::make();
        
        // Register print function
        jai::stdlib::register_io(engine);
        
        std::cout << "=== DEFINING INITIAL CLASS ===" << std::endl;
        
        // First definition
        engine->execute(R"(
            class Cat {
                auto age = 5;
            }
            
            auto cat = Cat();
        )");
        
        std::cout << "\n=== REDEFINING CLASS (HOT RELOAD) ===" << std::endl;
        
        // Redefine class - this should trigger hot reload
        engine->execute(R"(
            class Cat {
                auto age = 10;
            }
        )");
        
        std::cout << "\n=== TRYING TO SET FIELD ===" << std::endl;
        
        // Try to set the field
        engine->execute("cat.age = 42;");
        
        std::cout << "SUCCESS: Field was set!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}