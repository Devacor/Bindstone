#include <iostream>
#include <jaiscript/core/engine.hpp>

int main() {
    jai::engine eng;
    
    // Test 1: Simple variable assignment (should work)
    std::cout << "=== Test 1: x = 42 ===\n";
    eng.execute("auto x = 0;");
    auto r1 = eng.execute("x = 42;");
    std::cout << "Result type: " << static_cast<int>(r1.type()) << "\n";
    
    // Test 2: Map subscript assignment
    std::cout << "\n=== Test 2: map[\"key\"] = 42 ===\n";
    eng.execute("auto map = {};");
    auto r2 = eng.execute("map[\"key\"] = 42;");
    std::cout << "Result type: " << static_cast<int>(r2.type()) << "\n";
    
    // Test 3: Try to read it back
    std::cout << "\n=== Test 3: Read map[\"key\"] ===\n";
    auto r3 = eng.execute("map[\"key\"]");
    std::cout << "Read result type: " << static_cast<int>(r3.type()) << "\n";
    
    return 0;
}