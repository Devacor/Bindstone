#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    
    // Test returning strings with special characters
    engine.add_function("return_plain", []() -> jai::script_value {
        return jai::script_value("plain");
    });
    
    engine.add_function("return_brace", []() -> jai::script_value {
        return jai::script_value("}");
    });
    
    engine.add_function("return_double_brace", []() -> jai::script_value {
        return jai::script_value("}}");
    });
    
    // Test with script_string explicitly
    engine.add_function("return_script_string", []() -> jai::script_value {
        jai::script_string str = "test";
        return jai::script_value(str);
    });
    
    // Test with to_string on script_value
    engine.add_function("test_to_string", []() -> jai::script_value {
        jai::script_value val("input");
        std::string str = val.to_string();
        return jai::script_value(str);
    });
    
    try {
        std::cout << "Testing string returns:\n" << std::endl;
        
        auto tests = {
            "return_plain()",
            "return_brace()",
            "return_double_brace()",
            "return_script_string()",
            "test_to_string()"
        };
        
        for (const char* test : tests) {
            std::cout << test << ":" << std::endl;
            auto result = engine.execute(test);
            std::cout << "  is_string: " << result.is_string() 
                      << ", value: '" << (result.is_string() ? result.as<std::string>() : "NOT STRING") 
                      << "'" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}