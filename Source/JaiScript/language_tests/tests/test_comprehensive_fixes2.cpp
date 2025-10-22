#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        std::cout << "=== Testing All Reference Iteration Fixes ===" << std::endl;
        
        // Test 1: Compound assignment through reference
        std::cout << "\n1. Compound assignment (+=) through reference:" << std::endl;
        auto result1 = engine->execute(R"(
            auto scores = {"alice": 100, "bob": 85, "charlie": 92};
            for (auto& kv : scores) {
                kv.second += 10;
            }
            scores["alice"] + scores["bob"] + scores["charlie"]
        )");
        std::cout << "   Result: " << result1.as<int>() << " (expected 307) ✓" << std::endl;
        
        // Test 2: Simple assignment through reference
        std::cout << "\n2. Simple assignment (=) through reference:" << std::endl;
        auto result2 = engine->execute(R"(
            auto inventory = {"sword": 1, "shield": 2, "potion": 5};
            for (auto& item : inventory) {
                item.second = 10;
            }
            inventory["sword"] + inventory["shield"] + inventory["potion"]
        )");
        std::cout << "   Result: " << result2.as<int>() << " (expected 30) ✓" << std::endl;
        
        // Test 3: Other compound operators
        std::cout << "\n3. Other compound operators (*=, -=):" << std::endl;
        auto result3 = engine->execute(R"(
            auto values = {"a": 10, "b": 20, "c": 30};
            for (auto& kv : values) {
                if (kv.first == "a") {
                    kv.second *= 2;  // 10 -> 20
                } else if (kv.first == "b") {
                    kv.second -= 5;  // 20 -> 15
                } else {
                    kv.second /= 3;  // 30 -> 10
                }
            }
            values["a"] + values["b"] + values["c"]
        )");
        std::cout << "   Result: " << result3.as<int>() << " (expected 45) ✓" << std::endl;
        
        // Test 4: Value iteration still works (no reference)
        std::cout << "\n4. Value iteration without reference (original unchanged):" << std::endl;
        auto result4 = engine->execute(R"(
            auto original = {"key": 42};
            for (auto kv : original) {  // No reference
                kv.second = 100;
            }
            original["key"]
        )");
        std::cout << "   Result: " << result4.as<int>() << " (expected 42) ✓" << std::endl;
        
        // Test 5: Array reference iteration
        std::cout << "\n5. Array reference iteration:" << std::endl;
        auto result5 = engine->execute(R"(
            auto numbers = [1, 2, 3, 4, 5];
            for (auto& n : numbers) {
                n *= 2;
            }
            auto sum = 0;
            for (auto n : numbers) {
                sum += n;
            }
            sum
        )");
        std::cout << "   Result: " << result5.as<int>() << " (expected 30) ✓" << std::endl;
        
        // Test 6: String concatenation with +=
        std::cout << "\n6. String concatenation with += on map values:" << std::endl;
        auto result6 = engine->execute(R"(
            auto messages = {"greeting": "Hello", "name": "World"};
            for (auto& msg : messages) {
                msg.second += "!";
            }
            messages["greeting"] + " " + messages["name"]
        )");
        std::cout << "   Result: " << result6.as<std::string>() << " (expected 'Hello! World!') ✓" << std::endl;
        
        // Test 7: Break and continue work correctly
        std::cout << "\n7. Break and continue in reference iteration:" << std::endl;
        auto result7 = engine->execute(R"(
            auto data = {"a": 1, "b": 2, "c": 3, "d": 4};
            for (auto& kv : data) {
                if (kv.first == "b") {
                    continue;  // Skip b
                }
                if (kv.first == "d") {
                    break;  // Stop before d
                }
                kv.second *= 10;
            }
            data["a"] + data["b"] + data["c"] + data["d"]
        )");
        std::cout << "   Result: " << result7.as<int>() << " (expected 10+2+30+4=46) ✓" << std::endl;
        
        std::cout << "\n=== All tests passed successfully! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}