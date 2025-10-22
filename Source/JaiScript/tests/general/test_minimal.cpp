#include <jaiscript/jaiscript.hpp>
#include <iostream>

using namespace jai;

int main() {
    std::cout << "=== TESTING HOT RELOAD SETTER ISSUE ===\n";
    auto e = engine::make();
    
    try {
        std::cout << "\n1. Creating initial class Cat with age field...\n";
        e->execute(R"(
            class Cat {
                auto age = 5;
            }
            
            auto cat = Cat();
        )");
        
        std::cout << "2. Reading initial age value...\n";
        auto age = e->execute("cat.age");
        std::cout << "   cat.age = " << age.as<int>() << "\n";
        
        std::cout << "\n3. Hot reloading class Cat (redefining)...\n";
        e->execute(R"(
            class Cat {
                auto age = 10;
                auto name = "default";
            }
        )");
        
        std::cout << "\n4. Trying to set age field after hot reload...\n";
        e->execute("cat.age = 42;");
        
        std::cout << "5. Reading age after assignment...\n";
        age = e->execute("cat.age");
        std::cout << "   cat.age = " << age.as<int>() << "\n";
        
        std::cout << "\nSUCCESS: Hot reload setter works!\n";
        
    } catch (const std::exception& ex) {
        std::cout << "\nERROR: " << ex.what() << "\n";
        return 1;
    }
    
    return 0;
}