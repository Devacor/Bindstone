#include <jaiscript/core/engine.hpp>
#include <iostream>

int main() {
    try {
        auto engine = jai::engine::make();
        
        // First definition
        engine->execute(R"(
            class Cat {
                auto age = 0;
                auto name = "default";
                
                void meow() {
                    print("Meow from", name);
                }
            }
            
            auto cat = Cat();
            cat.age = 5;
            cat.name = "Fluffy";
        )");
        
        std::cout << "\n=== REDEFINING CLASS ===\n" << std::endl;
        
        // Redefine class - this should trigger hot reload
        engine->execute(R"(
            class Cat {
                auto name = "unnamed";
                auto lives = 9;
                
                void dance() {
                    print(name, "is dancing!");
                }
            }
        )");
        
        std::cout << "\n=== ACCESSING FIELDS AFTER HOT RELOAD ===\n" << std::endl;
        
        // Try to access the name field
        auto result = engine->execute("cat.name");
        std::cout << "cat.name = " << result.to_string() << std::endl;
        
        // Try to set a field (this should use the setter)
        engine->execute("cat.name = \"Mittens\";");
        
        result = engine->execute("cat.name");
        std::cout << "After assignment, cat.name = " << result.to_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}