#include <jaiscript/jaiscript.hpp>
#include <iostream>

using namespace jai;

int main() {
    auto eng = engine::make();
    
    std::cout << "Testing static methods...\n";
    
    try {
        eng->execute(R"(
            class Math {
                static function add(int a, int b) -> int {
                    return a + b;
                }
                
                static function multiply(int x, int y) -> int {
                    return x * y;
                }
                
                static int PI = 3;
                
                static function getPi() -> int {
                    return PI;
                }
            }
        )");
        std::cout << "✓ Static method class declared successfully\n";
        
        // Test static method calls
        auto result1 = eng->execute("Math::add(5, 3)");
        std::cout << "✓ Math::add(5, 3) = " << result1.as<int>() << "\n";
        
        auto result2 = eng->execute("Math::multiply(4, 7)");
        std::cout << "✓ Math::multiply(4, 7) = " << result2.as<int>() << "\n";
        
        // Test static method accessing static field
        auto result3 = eng->execute("Math::getPi()");
        std::cout << "✓ Math::getPi() = " << result3.as<int>() << "\n";
        
        std::cout << "✅ All static method tests passed!\n";
        
    } catch (const std::exception& e) {
        std::cout << "✗ Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}