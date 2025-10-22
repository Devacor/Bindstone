#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    auto js = jai::engine::make();
    
    int test_value = 42;
    js->add_global_ref("test_var", test_value);
    
    const char* script = R"(
        test_var
    )";
    
    try {
        auto result = js->execute(script);
        std::cout << "Result is_int: " << result.is_int() << std::endl;
        if (result.is_int()) {
            auto int64_val = result.as<int64_t>();
            std::cout << "Result as int64: " << int64_val << std::endl;
            
            auto int_val = result.as<int>();
            std::cout << "Result as int: " << int_val << std::endl;
            
            if (int_val == 42) {
                std::cout << "✓ SUCCESS: cpp_bound int works!" << std::endl;
                return 0;
            }
        }
        std::cout << "✗ FAIL: Unexpected result" << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cout << "✗ ERROR: " << e.what() << std::endl;
        return 1;
    }
}
