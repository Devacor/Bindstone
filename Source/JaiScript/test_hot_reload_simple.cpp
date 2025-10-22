#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    auto engine = jai::engine::make();
    
    std::cout << "=== Step 1: Define class ===" << std::endl;
    engine->execute("class Cat { auto age = 5; }");
    
    std::cout << "\n=== Step 2: Create instance ===" << std::endl;
    engine->execute("auto cat = Cat();");
    
    std::cout << "\n=== Step 3: Set field (should work) ===" << std::endl;
    try {
        engine->execute("cat.age = 10;");
        std::cout << "SUCCESS: Field set to 10" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
    }
    
    std::cout << "\n=== Step 4: Redefine class (hot reload) ===" << std::endl;
    engine->execute("class Cat { auto age = 20; }");
    
    std::cout << "\n=== Step 5: Set field after hot reload ===" << std::endl;
    try {
        engine->execute("cat.age = 30;");
        std::cout << "SUCCESS: Field set to 30 after hot reload" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
    }
    
    return 0;
}