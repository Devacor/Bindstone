#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/bound_array.hpp>
#include <iostream>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class bound_debug_tests : public suite {
public:
    bound_debug_tests() : suite("Bound Array Debug") {}
    
    void forge_tests() override {
        test("check_function_registration", [this]() {
            auto engine = engine::make();
            
            // Register standard conversions
            engine->add_standard_conversions();
            
            // Test function that takes bound_array<int>
            std::cout << "Registering sum_ints function...\n";
            engine->add_function("sum_ints", [](const bound_array<int>& nums) -> int {
                int sum = 0;
                for (int n : nums) sum += n;
                return sum;
            });
            
            // Check if function was registered
            bool has_func = engine->has_function("sum_ints");
            std::cout << "has_function('sum_ints'): " << (has_func ? "true" : "false") << "\n";
            check_eq(has_func, true);
            
            // Try to call it
            try {
                std::cout << "Attempting to execute sum_ints([1, 2, 3])...\n";
                script_value result = engine->execute("sum_ints([1, 2, 3])");
                std::cout << "Success! Result: " << result.as<int>() << "\n";
                check_eq(result.as<int>(), 6);
            } catch (const std::exception& e) {
                std::cout << "Failed with error: " << e.what() << "\n";
                check_eq(std::string(e.what()), std::string(""));  // Force failure
            }
        });
    }
};

}

FOUNDRY_REGISTER(jai::foundry::tests::bound_debug_tests)