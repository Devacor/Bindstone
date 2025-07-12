#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/bound_array.hpp>
#include <iostream>

using namespace jai;

int main() {
    try {
        auto engine = engine::make();
        
        // Register bound_array conversion
        engine->add_bound_array_conversion<int>();
        
        // Add a simple function that takes bound_array
        engine->add_function("sum_array", [](const bound_array<int>& arr) -> int {
            std::cout << "In sum_array, array size: " << arr.size() << std::endl;
            int sum = 0;
            for (int n : arr) {
                std::cout << "Value: " << n << std::endl;
                sum += n;
            }
            return sum;
        });
        
        // Test it
        auto result = engine->execute("sum_array([1, 2, 3])");
        std::cout << "Result: " << result.as<int>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
