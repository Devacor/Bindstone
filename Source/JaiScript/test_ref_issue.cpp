#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;

int main() {
    try {
        auto engine = engine::make();
        jai::stdlib::register_all(engine);
        
        std::cout << "Testing if pair constructor works with references:\n";
        
        // Test 1: Can we create references manually?
        std::cout << "1. Creating reference manually:\n";
        try {
            auto result = engine->execute(R"(
                auto val = 42;
                auto& ref_val = val;
                ref_val += 1;
                val  // Should be 43
            )");
            std::cout << "Reference works: " << result.template as<int>() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Reference failed: " << e.what() << "\n";
        }
        
        // Test 2: Test pair constructor with regular values (should work)
        std::cout << "\n2. Pair with regular values:\n";
        try {
            auto result = engine->execute(R"(
                auto p = pair(10, 20);
                p.first + p.second
            )");
            std::cout << "Regular pair works: " << result.template as<int>() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Regular pair failed: " << e.what() << "\n";
        }
        
        // Test 3: Non-reference array iteration (should work)
        std::cout << "\n3. Non-reference array iteration:\n";
        try {
            auto result = engine->execute(R"(
                auto numbers = [1, 2, 3];
                auto sum = 0;
                for (auto item : numbers) {
                    sum += item;
                }
                sum
            )");
            std::cout << "Non-ref array works: " << result.template as<int>() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Non-ref array failed: " << e.what() << "\n";
        }
        
        // Test 4: Reference array iteration (the problem)
        std::cout << "\n4. Reference array iteration:\n";
        try {
            auto result = engine->execute(R"(
                auto numbers = [1, 2, 3];
                for (auto& item : numbers) {
                    item *= 2;
                }
                numbers[0] + numbers[1] + numbers[2]
            )");
            std::cout << "Ref array works: " << result.template as<int>() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Ref array failed: " << e.what() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}