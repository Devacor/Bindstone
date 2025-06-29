#include <iostream>
#include <jaiscript/core/engine.hpp>

void test_assignment(jai::engine& eng, const std::string& code, const std::string& desc) {
    std::cout << "\n=== " << desc << " ===\n";
    std::cout << "Code: " << code << "\n";
    try {
        auto result = eng.execute(code);
        std::cout << "Success! Result type: " << static_cast<int>(result.type()) << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
}

int main() {
    jai::engine eng;
    
    // Setup
    eng.execute("auto x = 0;");
    eng.execute("auto arr = [1, 2, 3];");
    eng.execute("auto myMap = {};");
    
    // Test different assignment types
    test_assignment(eng, "x = 42", "Simple variable assignment");
    test_assignment(eng, "arr[0] = 99", "Array subscript assignment");
    test_assignment(eng, "myMap[123] = 456", "Map assignment with int key");
    test_assignment(eng, "myMap[\"key\"] = 789", "Map assignment with string key");
    
    // Verify what actually happened
    std::cout << "\n=== Verification ===\n";
    std::cout << "x = " << eng.execute("x").as<int>() << "\n";
    std::cout << "arr[0] = " << eng.execute("arr[0]").as<int>() << "\n";
    
    try {
        std::cout << "myMap[123] = " << eng.execute("myMap[123]").as<int>() << "\n";
    } catch (...) {
        std::cout << "myMap[123] failed to read\n";
    }
    
    try {
        std::cout << "myMap[\"key\"] = " << eng.execute("myMap[\"key\"]").as<int>() << "\n";
    } catch (...) {
        std::cout << "myMap[\"key\"] failed to read\n";
    }
    
    return 0;
}