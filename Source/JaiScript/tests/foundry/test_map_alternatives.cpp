#include <iostream>
#include <jaiscript/core/engine.hpp>

int main() {
    jai::engine eng;
    
    std::cout << "=== Test 1: myMap[\"key\"] = 42 ===\n";
    try {
        eng.execute("auto myMap1 = {};");
        eng.execute("myMap1[\"key\"] = 42;");
        auto r1 = eng.execute("myMap1[\"key\"]");
        std::cout << "Result: " << (r1.is_null() ? "null" : "has value") << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "\n=== Test 2: Using variable for key ===\n";
    try {
        eng.execute("auto myMap2 = {};");
        eng.execute("auto key = \"mykey\";");
        eng.execute("myMap2[key] = 42;");
        auto r2 = eng.execute("myMap2[key]");
        std::cout << "Result: " << (r2.is_null() ? "null" : "has value") << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "\n=== Test 3: Using integer key ===\n";
    try {
        eng.execute("auto myMap3 = {};");
        eng.execute("myMap3[123] = 42;");
        auto r3 = eng.execute("myMap3[123]");
        std::cout << "Result: " << (r3.is_null() ? "null" : "has value") << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    return 0;
}