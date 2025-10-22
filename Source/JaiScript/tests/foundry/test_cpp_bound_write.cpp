#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    auto js = jai::engine::make();

    int test_value = 42;
    js->add_global_ref("test_var", test_value);

    std::cout << "Initial C++ value: " << test_value << std::endl;

    const char* script = R"(
        test_var = 100;
        test_var
    )";

    try {
        auto result = js->execute(script);
        std::cout << "Script result: " << result.as<int>() << std::endl;
        std::cout << "C++ value after script: " << test_value << std::endl;

        if (test_value == 100 && result.as<int>() == 100) {
            std::cout << "✓ SUCCESS: cpp_bound write works!" << std::endl;
            return 0;
        } else {
            std::cout << "✗ FAIL: Values don't match" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "✗ ERROR: " << e.what() << std::endl;
        return 1;
    }
}
