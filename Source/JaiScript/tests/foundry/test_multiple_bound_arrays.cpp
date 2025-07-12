#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/bound_array.hpp>
#include <jaiscript/core/bound_map.hpp>
#include <iostream>

using namespace jai;

int main() {
    try {
        auto engine = engine::make();
        
        // Register conversions
        engine->add_bound_array_conversion<int>();
        engine->add_bound_array_conversion<std::string>();
        engine->add_bound_map_conversion<std::string, int>();
        
        // Add the same function as in the test
        engine->add_function("process_data", [](
            const bound_array<int>& numbers,
            const bound_array<std::string>& words,
            const bound_map<std::string, int>& scores
        ) -> int {
            int total = 0;
            
            std::cout << "Numbers size: " << numbers.size() << std::endl;
            // Sum numbers
            for (int n : numbers) {
                std::cout << "Number: " << n << std::endl;
                total += n;
            }
            
            std::cout << "Words size: " << words.size() << std::endl;
            // Add word count
            total += static_cast<int>(words.size()) * 10;
            
            std::cout << "Scores size: " << scores.size() << std::endl;
            // Add score values
            for (const auto& [key, value] : scores) {
                std::cout << "Score " << key << ": " << value << std::endl;
                total += value;
            }
            
            return total;
        });
        
        // Test it
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
