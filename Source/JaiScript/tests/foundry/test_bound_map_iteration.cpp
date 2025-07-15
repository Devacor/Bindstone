#include <jaiscript/jaiscript.hpp>
#include <iostream>

using namespace jai;

int main() {
    try {
        auto engine = engine::make();
        
        // Register conversions - exactly like the failing test
        engine->add_bound_array_conversion<int>();
        engine->add_bound_array_conversion<std::string>();
        engine->add_bound_map_conversion<std::string, int>();
        
        // Add a test function with all three types - exactly like the failing test
        engine->add_function("process_data", [](
            const bound_array<int>& numbers,
            const bound_array<std::string>& words,
            const bound_map<std::string, int>& scores
        ) -> int {
            int total = 0;
            
            std::cout << "Processing numbers..." << std::endl;
            // Sum numbers
            for (int n : numbers) {
                std::cout << "Number: " << n << std::endl;
                total += n;
            }
            
            std::cout << "Processing words..." << std::endl;
            // Add word count
            total += static_cast<int>(words.size()) * 10;
            for (const auto& word : words) {
                std::cout << "Word: " << word << std::endl;
            }
            
            std::cout << "Processing scores..." << std::endl;
            // Add score values
            for (const auto& [key, value] : scores) {
                std::cout << "Key: " << key << ", Value: " << value << std::endl;
                total += value;
            }
            
            return total;
        });
        
        // Test with exact same script as the failing test
        std::cout << "Executing script..." << std::endl;
        auto result = engine->execute(R"(
            process_data(
                [1, 2, 3],
                ["hello", "world"],
                {"alice": 10, "bob": 20}
            )
        )");
        
        std::cout << "Result: " << result.as<int>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}