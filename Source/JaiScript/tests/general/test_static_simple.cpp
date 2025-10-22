#include <jaiscript/jaiscript.hpp>
#include <iostream>

using namespace jai;

int main() {
    auto eng = engine::make();
    
    std::cout << "Creating class with static field...\n";
    eng->execute(R"(
        class TestClass {
            static int count = 0;
            
            TestClass() {
                count = count + 1;
            }
            
            function getCount() -> int {
                return count;
            }
        }
    )");
    
    try {
        std::cout << "\nAccessing static field via class name...\n";
        auto count = eng->execute("TestClass::count");
        std::cout << "TestClass::count = " << count.as<int>() << "\n";
        
        std::cout << "\nCreating instance...\n";
        eng->execute("auto t = TestClass();");
        
        std::cout << "\nAccessing static field after construction...\n";
        count = eng->execute("TestClass::count");
        std::cout << "TestClass::count = " << count.as<int>() << "\n";
        
        std::cout << "\nCalling method that accesses static field...\n";
        count = eng->execute("t.getCount()");
        std::cout << "t.getCount() = " << count.as<int>() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
    
    return 0;
}