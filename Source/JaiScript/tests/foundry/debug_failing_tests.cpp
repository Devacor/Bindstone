#include <jaiscript/core/engine.hpp>
#include <iostream>
#include <string>

using namespace jai;

void debug_complex_control_flow() {
    std::cout << "\n=== DEBUG: complex_control_flow ===" << std::endl;
    
    auto engine = engine::make();
    script_value result = engine->execute(R"(
        var result = 0;
        for (int i = 1; i <= 10; ++i) {
            if (i % 2 == 0) {
                continue;
            }
            var j = 0;
            while (j < i) {
                result += 1;
                j++;
                if (j > 3) {
                    break;
                }
            }
        }
        result;
    )");
    
    std::cout << "Expected: 15" << std::endl;
    std::cout << "Actual: " << result.as<int>() << std::endl;
    std::cout << "Is Int: " << result.is_int() << std::endl;
}

void debug_cpp_runtime_error_interop() {
    std::cout << "\n=== DEBUG: cpp_runtime_error_interop ===" << std::endl;
    
    auto engine = engine::make();
    
    // Add a C++ function that throws std::runtime_error
    engine->add_function("risky_function", []() {
        throw std::runtime_error("C++ function error");
    });
    
    std::string script = R"(
        try {
            risky_function();
        } catch (e) {
            return e;
        }
    )";
    
    script_value result = engine->execute(script);
    
    std::cout << "Expected: \"C++ function error\"" << std::endl;
    std::cout << "Actual: \"" << result.as_string() << "\"" << std::endl;
    std::cout << "Is String: " << result.is_string() << std::endl;
    std::cout << "Is Int: " << result.is_int() << std::endl;
}

void debug_cpp_generic_exception_interop() {
    std::cout << "\n=== DEBUG: cpp_generic_exception_interop ===" << std::endl;
    
    auto engine = engine::make();
    
    // Add a C++ function that throws generic std::exception
    engine->add_function("generic_exception", []() {
        throw std::logic_error("Generic C++ error");
    });
    
    std::string script = R"(
        try {
            generic_exception();
        } catch (e) {
            return e;
        }
    )";
    
    script_value result = engine->execute(script);
    
    std::cout << "Expected: \"Unbound exception type caught in JaiScript.\"" << std::endl;
    std::cout << "Actual: \"" << result.as_string() << "\"" << std::endl;
    std::cout << "Is String: " << result.is_string() << std::endl;
    std::cout << "Is Int: " << result.is_int() << std::endl;
}

void debug_exception_with_numeric_values() {
    std::cout << "\n=== DEBUG: exception_with_numeric_values ===" << std::endl;
    
    auto engine = engine::make();
    
    std::string script = R"(
        try {
            throw 42;
        } catch (e) {
            return e;
        }
    )";
    
    script_value result = engine->execute(script);
    
    std::cout << "Expected: \"42\"" << std::endl;
    std::cout << "Actual: \"" << result.as_string() << "\"" << std::endl;
    std::cout << "Is String: " << result.is_string() << std::endl;
    std::cout << "Is Int: " << result.is_int() << std::endl;
}

int main() {
    try {
        debug_complex_control_flow();
        debug_cpp_runtime_error_interop();
        debug_cpp_generic_exception_interop();
        debug_exception_with_numeric_values();
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}