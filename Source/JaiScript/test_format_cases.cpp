#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    jai::stdlib::register_all(engine);
    
    try {
        std::cout << "Testing format cases:\n" << std::endl;
        
        // Test different argument counts
        std::cout << "1. format() - no args:" << std::endl;
        auto result = engine.execute("format()");
        std::cout << "   is_string: " << result.is_string() << ", value: '" 
                  << (result.is_string() ? result.as<std::string>() : "NOT STRING") << "'" << std::endl;
        
        std::cout << "\n2. format('hello') - single string arg:" << std::endl;
        result = engine.execute("format('hello')");
        std::cout << "   is_string: " << result.is_string() << ", value: '" 
                  << (result.is_string() ? result.as<std::string>() : "NOT STRING") << "'" << std::endl;
        
        std::cout << "\n3. format(42) - single int arg:" << std::endl;
        result = engine.execute("format(42)");
        std::cout << "   is_string: " << result.is_string() << ", value: '" 
                  << (result.is_string() ? result.as<std::string>() : "NOT STRING") << "'" << std::endl;
        
        std::cout << "\n4. format('a', 'b') - two args:" << std::endl;
        result = engine.execute("format('a', 'b')");
        std::cout << "   is_string: " << result.is_string() << ", value: '" 
                  << (result.is_string() ? result.as<std::string>() : "NOT STRING") << "'" << std::endl;
        
        std::cout << "\n5. format('{}', 'test') - format string:" << std::endl;
        result = engine.execute("format('{}', 'test')");
        std::cout << "   is_string: " << result.is_string() << ", value: '" 
                  << (result.is_string() ? result.as<std::string>() : "NOT STRING") << "'" << std::endl;
        
        // Now test escaping
        std::cout << "\n6. format('}}') - escape test:" << std::endl;
        result = engine.execute("format('}}')");
        std::cout << "   is_string: " << result.is_string() << ", value: '" 
                  << (result.is_string() ? result.as<std::string>() : "NOT STRING") << "'" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}