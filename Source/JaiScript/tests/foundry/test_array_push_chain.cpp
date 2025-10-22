#include <jaiscript/core/engine.hpp>
#include <iostream>

int main() {
    auto engine = jai::engine::make();

    try {
        std::cout << "Test 1: Single push\n";
        jai::script_value result1 = engine->execute(R"(
            var arr = [];
            arr.push(1);
            arr.size();
        )");
        std::cout << "Array size after push: " << result1.as<int>() << "\n\n";

        std::cout << "Test 2: Push return value\n";
        jai::script_value result2 = engine->execute(R"(
            var arr = [];
            var ret = arr.push(1);
            ret;
        )");
        std::cout << "Return value type: " << static_cast<int>(result2.get_type()) << "\n";
        if (result2.is_array()) {
            std::cout << "Return is array, size: " << result2.as_array().size() << "\n";
        } else {
            std::cout << "Return is NOT an array!\n";
        }

        std::cout << "\nTest 3: Chained push\n";
        jai::script_value result3 = engine->execute(R"(
            var arr = [];
            arr.push(1).push(2);
            arr.size();
        )");
        std::cout << "SUCCESS: Array size: " << result3.as<int>() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
