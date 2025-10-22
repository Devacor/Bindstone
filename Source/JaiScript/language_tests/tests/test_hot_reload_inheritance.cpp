#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    auto engine = jai::engine::make();
    
    std::cout << "=== Step 1: Define base and derived classes ===" << std::endl;
    engine->execute(R"(
        class Animal {
            auto sound = "generic";
        }
        
        class Cat : Animal {
            auto age = 5;
        }
    )");
    
    std::cout << "\n=== Step 2: Create instance ===" << std::endl;
    engine->execute("auto cat = Cat();");
    
    std::cout << "\n=== Step 3: Set fields (should work) ===" << std::endl;
    try {
        engine->execute("cat.age = 10;");
        std::cout << "SUCCESS: age set to 10" << std::endl;
        engine->execute("cat.sound = \"meow\";");
        std::cout << "SUCCESS: sound set to meow" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
    }
    
    std::cout << "\n=== Step 4: Redefine derived class (hot reload) ===" << std::endl;
    engine->execute(R"(
        class Cat : Animal {
            auto age = 20;
            auto lives = 9;
        }
    )");
    
    std::cout << "\n=== Step 5: Set fields after hot reload ===" << std::endl;
    try {
        engine->execute("cat.age = 30;");
        std::cout << "SUCCESS: age set to 30 after hot reload" << std::endl;
        engine->execute("cat.sound = \"purr\";");
        std::cout << "SUCCESS: sound set to purr after hot reload" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
    }
    
    return 0;
}