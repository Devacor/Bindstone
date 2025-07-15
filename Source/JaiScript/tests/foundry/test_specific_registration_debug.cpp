#include <jaiscript/jaiscript.hpp>
#include <iostream>

using namespace jai;

int main() {
    try {
        // Test the exact same pattern as the failing test but in isolation
        std::cout << "Creating first engine for working test..." << std::endl;
        {
            auto engine = engine::make();
            engine->add_bound_map_conversion<std::string, int>();
            
            engine->add_function("simple_test", [](const bound_map<std::string, int>& scores) -> int {
                int total = 0;
                for (const auto& [key, value] : scores) {
                    std::cout << "Simple test - Key: " << key << ", Value: " << value << std::endl;
                    total += value;
                }
                return total;
            });
            
            auto result = engine->execute(R"(simple_test({"alice": 10, "bob": 20}))");
            std::cout << "Simple test result: " << result.as<int>() << std::endl;
        }
        
        std::cout << "\nCreating second engine for full test..." << std::endl;
        {
            auto engine = engine::make();
            
            // Register only specific conversions we need
            engine->add_bound_array_conversion<int>();
            engine->add_bound_array_conversion<std::string>();
            engine->add_bound_map_conversion<std::string, int>();
            
            // Test function that takes the registered types
            engine->add_function("process_data", [](
                const bound_array<int>& numbers,
                const bound_array<std::string>& words,
                const bound_map<std::string, int>& scores
            ) -> int {
                int total = 0;
                
                std::cout << "Processing arrays and map..." << std::endl;
                
                // Sum numbers
                for (int n : numbers) total += n;
                std::cout << "Numbers processed, total so far: " << total << std::endl;
                
                // Add word count
                total += static_cast<int>(words.size()) * 10;
                std::cout << "Words processed, total so far: " << total << std::endl;
                
                // Add score values
                std::cout << "About to iterate map..." << std::endl;
                for (const auto& [key, value] : scores) {
                    std::cout << "Key: " << key << ", Value: " << value << std::endl;
                    total += value;
                }
                std::cout << "Map iteration complete, final total: " << total << std::endl;
                
                return total;
            });
            
            std::cout << "Executing script..." << std::endl;
            auto result = engine->execute(R"(
                process_data(
                    [1, 2, 3],
                    ["hello", "world"],
                    {"alice": 10, "bob": 20}
                )
            )");
            
            // Expected: 6 (sum) + 20 (2 words * 10) + 30 (scores) = 56
            std::cout << "Result: " << result.as<int>() << std::endl;
            if (result.as<int>() == 56) {
                std::cout << "✓ Specific type registration works!" << std::endl;
            } else {
                std::cout << "✗ Wrong result!" << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}