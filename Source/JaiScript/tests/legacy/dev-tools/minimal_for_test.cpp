#include <iostream>
#include <jaiscript/core/engine.hpp>

int main() {
    jai::engine engine;
    
    // Exact script from the test
    const char* script = R"(
        var arr = [1, 2, 3, 4, 5];
        var n = 5;
        for (var i = 0; i < n / 2; i = i + 1) {
            var temp = arr[i];
            arr[i] = arr[n - 1 - i];
            arr[n - 1 - i] = temp;
        }
        arr[0];  // Should be 5
    )";
    
    std::cout << "Executing script:\n" << script << "\n\n";
    
    try {
        jai::script_value result = engine.execute(script);
        std::cout << "Result type: " << (int)result.type() << "\n";
        std::cout << "Result is null: " << result.is_null() << "\n";
        if (\!result.is_null()) {
            std::cout << "Result value: " << result.as<int>() << "\n";
        }
    } catch (const std::exception& e) {
        std::cout << "exception: " << e.what() << "\n";
    }
    
    return 0;
}
