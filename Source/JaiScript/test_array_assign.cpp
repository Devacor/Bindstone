#include <iostream>
#include "include/jaiscript/core/engine.hpp"

int main() {
    try {
        JaiScript::Engine engine;
        
        // Test 1: Simple array assignment
        std::cout << "Test 1: Simple array assignment\n";
        auto result = engine.execute(R"(
            var arr = [1, 2, 3];
            arr[1] = 99;
            arr[1];
        )");
        std::cout << "arr[1] after assignment: " << result.as<int>() << " (expected: 99)\n\n";
        
        // Test 2: Array reversal
        std::cout << "Test 2: Array reversal\n";
        result = engine.execute(R"(
            var arr = [1, 2, 3, 4, 5];
            var n = 5;
            for (var i = 0; i < n / 2; i = i + 1) {
                var temp = arr[i];
                arr[i] = arr[n - 1 - i];
                arr[n - 1 - i] = temp;
            }
            arr[0];
        )");
        std::cout << "arr[0] after reversal: " << result.as<int>() << " (expected: 5)\n\n";
        
        // Test 3: Check full array after reversal
        std::cout << "Test 3: Full array check\n";
        for (int i = 0; i < 5; i++) {
            result = engine.execute("arr[" + std::to_string(i) + "];");
            std::cout << "arr[" << i << "] = " << result.as<int>() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}