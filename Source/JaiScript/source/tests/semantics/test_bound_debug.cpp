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
            auto engine = make_engine();
            
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

        // Move/move-assign must carry engine_ref_; otherwise the moved-to array builds its next
        // element with a wild (uninitialized) engine pointer. Observable in reference mode by
        // the engine the pushed element ends up carrying.
        test("move_preserves_engine_ref", [this]() {
            auto eng = make_engine();
            script_value holder = script_value::make_array(nullptr, eng.get());
            auto& backing = const_cast<std::vector<script_value>&>(holder.as_array());

            bound_array<script_int> ref(backing, eng.get());   // reference mode, engine_ref_ = eng
            bound_array<script_int> moved = std::move(ref);     // move ctor must carry engine_ref_
            moved.push_back((script_int)7);                     // element is built via engine_ref_
            check_eq((size_t)1, backing.size());
            check(backing[0].get_engine() == eng.get(), "moved bound_array lost its engine_ref_");

            bound_array<script_int> assigned(backing, eng.get());
            assigned = std::move(moved);                        // move assignment too
            assigned.push_back((script_int)9);
            check(backing.back().get_engine() == eng.get(), "move-assigned bound_array lost its engine_ref_");
        });
    }
};

}

FOUNDRY_REGISTER(jai::foundry::tests::bound_debug_tests)