#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;

int main() {
    try {
        auto engine = engine::make();
        jai::stdlib::register_all(engine);
        
        std::cout << "Testing reference semantics in range-based for loops:\n\n";
        
        // Test 1: Array references
        std::cout << "1. Array reference modification:\n";
        try {
            auto result = engine->execute(R"(
                auto numbers = [1, 2, 3, 4, 5];
                print("Before: [" + to_string(numbers[0]) + ", " + to_string(numbers[1]) + ", " + to_string(numbers[2]) + ", " + to_string(numbers[3]) + ", " + to_string(numbers[4]) + "]");
                
                for (auto& item : numbers) {
                    item *= 2;  // Double each element
                }
                
                print("After: [" + to_string(numbers[0]) + ", " + to_string(numbers[1]) + ", " + to_string(numbers[2]) + ", " + to_string(numbers[3]) + ", " + to_string(numbers[4]) + "]");
                numbers[0] + numbers[1] + numbers[2] + numbers[3] + numbers[4]
            )");
            std::cout << "Sum after doubling: " << result.template as<int>() << " (expected: 30)\n\n";
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n\n";
        }
        
        // Test 2: Map value references (should work)
        std::cout << "2. Map value reference modification:\n";
        try {
            auto result = engine->execute(R"(
                auto scores = {"alice": 100, "bob": 85, "charlie": 92};
                print("Before - Alice: " + to_string(scores["alice"]) + ", Bob: " + to_string(scores["bob"]) + ", Charlie: " + to_string(scores["charlie"]));
                
                for (auto& kv : scores) {
                    kv.second += 10;  // Add bonus points to each score
                }
                
                print("After - Alice: " + to_string(scores["alice"]) + ", Bob: " + to_string(scores["bob"]) + ", Charlie: " + to_string(scores["charlie"]));
                scores["alice"] + scores["bob"] + scores["charlie"]
            )");
            std::cout << "Total after bonus: " << result.template as<int>() << " (expected: 307)\n\n";
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n\n";
        }
        
        // Test 3: Verify keys are not modifiable (should be const)
        std::cout << "3. Map key modification (should fail or be ignored):\n";
        try {
            auto result = engine->execute(R"(
                auto test_map = {"key1": 100};
                for (auto& kv : test_map) {
                    print("Original key: " + kv.first);
                    // This should either fail or be ignored since keys should be const
                    // kv.first = "modified_key";  // Uncomment to test
                }
                "test_completed"
            )");
            std::cout << "Key modification test: " << result.as<std::string>() << "\n\n";
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}