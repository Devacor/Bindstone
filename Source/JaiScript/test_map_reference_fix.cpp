#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;

int main() {
    try {
        auto engine = engine::make();
        jai::stdlib::register_all(engine);
        
        std::cout << "Testing map reference semantics fix:\n\n";
        
        // Test 1: Non-reference map iteration (should work as before)
        std::cout << "1. Non-reference map iteration:\n";
        try {
            auto result = engine->execute(R"(
                auto scores = {"alice": 100, "bob": 85, "charlie": 92};
                auto total = 0;
                for (auto kv : scores) {
                    total += kv.second;
                }
                print("Total (non-ref): " + to_string(total));
                total
            )");
            std::cout << "Non-ref sum: " << result.template as<int>() << " (expected: 277)\n\n";
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n\n";
        }
        
        // Test 2: Reference map iteration with modification
        std::cout << "2. Reference map iteration with value modification:\n";
        try {
            auto result = engine->execute(R"(
                auto scores = {"alice": 100, "bob": 85, "charlie": 92};
                print("Before: alice=" + to_string(scores["alice"]) + 
                      ", bob=" + to_string(scores["bob"]) + 
                      ", charlie=" + to_string(scores["charlie"]));
                
                // Add 10 bonus points to each score using reference iteration
                for (auto& kv : scores) {
                    kv.second += 10;
                }
                
                print("After: alice=" + to_string(scores["alice"]) + 
                      ", bob=" + to_string(scores["bob"]) + 
                      ", charlie=" + to_string(scores["charlie"]));
                
                scores["alice"] + scores["bob"] + scores["charlie"]
            )");
            std::cout << "Sum after bonus: " << result.template as<int>() << " (expected: 307)\n\n";
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n\n";
        }
        
        // Test 3: Verify key is still const (modification should not affect map)
        std::cout << "3. Key access (should be const/copy):\n";
        try {
            auto result = engine->execute(R"(
                auto test_map = {"key1": 100, "key2": 200};
                auto keys_before = "";
                for (auto kv : test_map) {
                    keys_before += kv.first + " ";
                }
                print("Keys before: " + keys_before);
                
                // Try to modify key (should not affect the map)
                for (auto& kv : test_map) {
                    kv.first = "modified";  // This modifies the pair's copy of the key
                }
                
                auto keys_after = "";
                for (auto kv : test_map) {
                    keys_after += kv.first + " ";
                }
                print("Keys after: " + keys_after);
                
                keys_before == keys_after ? "Keys unchanged (correct)" : "Keys changed (incorrect)"
            )");
            std::cout << "Result: " << result.template as<std::string>() << "\n\n";
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n\n";
        }
        
        // Test 4: Nested maps with reference iteration
        std::cout << "4. Nested map with reference iteration:\n";
        try {
            auto result = engine->execute(R"(
                auto data = {
                    "team1": {"alice": 100, "bob": 85},
                    "team2": {"charlie": 92, "david": 88}
                };
                
                // Add 5 points to everyone
                for (auto& team : data) {
                    for (auto& player : team.second) {
                        player.second += 5;
                    }
                }
                
                // Calculate total
                auto total = 0;
                for (auto team : data) {
                    for (auto player : team.second) {
                        total += player.second;
                    }
                }
                
                print("Total after bonus: " + to_string(total));
                total
            )");
            std::cout << "Nested total: " << result.template as<int>() << " (expected: 385)\n";
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}