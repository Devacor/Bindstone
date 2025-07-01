#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    jai::stdlib::register_all(engine);
    
    std::cout << "Format function - final test:\n" << std::endl;
    
    // Test escaping works correctly
    std::cout << "=== Escape sequences ===" << std::endl;
    engine.execute(R"(
        print("{{ -> ", format("{{"));
        print("}} -> ", format("}}"));
        print("{{}} -> ", format("{{}}"));
        print("{{0}} -> ", format("{{0}}"));
    )");
    
    std::cout << "\n=== Format vs Concatenation ===" << std::endl;
    engine.execute(R"(
        // Format mode (has unescaped {})
        print("Format: ", format("Hello, {}!", "World"));
        print("Format: ", format("{1} {0}", "World", "Hello"));
        
        // Concatenation mode (no unescaped {})
        print("Concat: ", format("Hello, ", "World", "!"));
        print("Concat: ", format("{{", "escaped", "}}"));
    )");
    
    std::cout << "\n=== JSON Building (Recommended) ===" << std::endl;
    engine.execute(R"(
        auto key = "name";
        auto value = "John Doe";
        
        // Method 1: Use string concatenation
        auto json1 = "{\"" + key + "\": \"" + value + "\"}";
        print("String concat: ", json1);
        
        // Method 2: Force concat mode with empty first arg
        auto json2 = format("", "{\"", key, "\": \"", value, "\"}");
        print("Format concat: ", json2);
        
        // Method 3: Use escaped braces in single string
        auto json3 = format("{{\"name\": \"John Doe\"}}");
        print("Escaped single: ", json3);
    )");
    
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "- {{ escapes to {" << std::endl;
    std::cout << "- }} escapes to }" << std::endl;
    std::cout << "- Format mode activates when first arg contains unescaped {" << std::endl;
    std::cout << "- Use string concatenation or empty first arg for JSON building" << std::endl;
    
    return 0;
}