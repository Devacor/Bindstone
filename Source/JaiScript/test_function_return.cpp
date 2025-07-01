#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    jai::stdlib::register_all(engine);
    
    // Test regular function returning string
    engine.add_function("test_string", []() -> jai::script_value {
        return jai::script_value("hello");
    });
    
    // Test regular function returning int
    engine.add_function("test_int", []() -> jai::script_value {
        return jai::script_value(42);
    });
    
    // Test variadic function returning string
    engine.add_variadic_function("test_variadic", [](const std::vector<jai::script_value>&) -> jai::script_value {
        return jai::script_value("variadic");
    });
    
    try {
        std::cout << "Testing function returns:\n" << std::endl;
        
        std::cout << "1. test_string():" << std::endl;
        auto result = engine.execute("test_string()");
        std::cout << "   is_string: " << result.is_string() << std::endl;
        if (result.is_string()) {
            std::cout << "   value: '" << result.as<std::string>() << "'" << std::endl;
        }
        
        std::cout << "\n2. test_int():" << std::endl;
        result = engine.execute("test_int()");
        std::cout << "   is_int: " << result.is_int() << std::endl;
        if (result.is_int()) {
            std::cout << "   value: " << result.as<jai::script_int>() << std::endl;
        }
        
        std::cout << "\n3. test_variadic():" << std::endl;
        result = engine.execute("test_variadic()");
        std::cout << "   is_string: " << result.is_string() << std::endl;
        if (result.is_string()) {
            std::cout << "   value: '" << result.as<std::string>() << "'" << std::endl;
        }
        
        // Also test the actual format function
        std::cout << "\n4. format():" << std::endl;
        result = engine.execute("format()");
        std::cout << "   is_string: " << result.is_string() << std::endl;
        std::cout << "   is_null: " << result.is_null() << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}