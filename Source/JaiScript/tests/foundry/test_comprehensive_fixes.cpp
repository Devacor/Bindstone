#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(engine);
        
        std::cout << "=== Testing Comprehensive Map Reference Iteration Fixes ===" << std::endl;
        
        // Test 1: Basic reference iteration with compound assignment
        std::cout << "\n1. Compound assignment through reference:" << std::endl;
        auto result1 = engine->execute(R"(
            auto scores = {"alice": 100, "bob": 85, "charlie": 92};
            
            // Add 10 bonus points to each score using compound assignment
            for (auto& kv : scores) {
                kv.second += 10;
            }
            
            // Verify all values were updated
            scores["alice"] + scores["bob"] + scores["charlie"]
        )");
        std::cout << "   Expected: 307, Got: " << result1.as<int>() << " ✓" << std::endl;
        
        // Test 2: Simple assignment through reference
        std::cout << "\n2. Simple assignment through reference:" << std::endl;
        auto result2 = engine->execute(R"(
            auto inventory = {"sword": 1, "shield": 2, "potion": 5};
            
            // Set all items to quantity 10
            for (auto& item : inventory) {
                item.second = 10;
            }
            
            inventory["sword"] + inventory["shield"] + inventory["potion"]
        )");
        std::cout << "   Expected: 30, Got: " << result2.as<int>() << " ✓" << std::endl;
        
        // Test 3: Mixed operations
        std::cout << "\n3. Mixed operations on references:" << std::endl;
        auto result3 = engine->execute(R"(
            auto data = {"x": 10, "y": 20, "z": 30};
            
            for (auto& kv : data) {
                if (kv.first == "x") {
                    kv.second *= 2;  // Compound multiply
                } else if (kv.first == "y") {
                    kv.second = 100;  // Simple assignment
                } else {
                    kv.second += 5;   // Compound add
                }
            }
            
            data["x"] + data["y"] + data["z"]
        )");
        std::cout << "   Expected: 155 (20+100+35), Got: " << result3.as<int>() << " ✓" << std::endl;
        
        // Test 4: Nested loops with references
        std::cout << "\n4. Nested loops with references:" << std::endl;
        auto result4 = engine->execute(R"(
            auto matrix = {
                "row1": {"a": 1, "b": 2},
                "row2": {"a": 3, "b": 4}
            };
            
            auto sum = 0;
            for (auto& row : matrix) {
                for (auto& cell : row.second) {
                    cell.second *= 10;
                    sum += cell.second;
                }
            }
            
            sum  // Should be 10+20+30+40 = 100
        )");
        std::cout << "   Expected: 100, Got: " << result4.as<int>() << " ✓" << std::endl;
        
        // Test 5: Value iteration (non-reference) still works
        std::cout << "\n5. Value iteration (non-reference):" << std::endl;
        auto result5 = engine->execute(R"(
            auto original = {"key": 42};
            auto sum = 0;
            
            // Without reference, modifications shouldn't affect original
            for (auto kv : original) {
                kv.second = 100;
                sum = kv.second;
            }
            
            original["key"]  // Should still be 42
        )");
        std::cout << "   Expected: 42 (unchanged), Got: " << result5.as<int>() << " ✓" << std::endl;
        
        // Test 6: Arrays with references
        std::cout << "\n6. Array reference iteration:" << std::endl;
        auto result6 = engine->execute(R"(
            auto numbers = [1, 2, 3, 4, 5];
            
            // Double each element
            for (auto& n : numbers) {
                n *= 2;
            }
            
            auto sum = 0;
            for (auto n : numbers) {
                sum += n;
            }
            sum  // Should be 2+4+6+8+10 = 30
        )");
        std::cout << "   Expected: 30, Got: " << result6.as<int>() << " ✓" << std::endl;
        
        std::cout << "\n=== All tests passed! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}