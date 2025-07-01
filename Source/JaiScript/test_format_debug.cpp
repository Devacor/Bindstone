#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    jai::stdlib::register_all(engine);
    
    std::cout << "Debug format function:\n" << std::endl;
    
    // Test format directly
    std::cout << "1. Testing format('}}'):" << std::endl;
    auto result1 = engine.execute("format('}}')");
    std::cout << "   Is string: " << result1.is_string() << std::endl;
    std::cout << "   Value: '" << result1.as<std::string>() << "'" << std::endl;
    std::cout << "   Length: " << result1.as<std::string>().length() << std::endl;
    
    std::cout << "\n2. Testing format('{{}}'):" << std::endl;
    auto result2 = engine.execute("format('{{}}')");
    std::cout << "   Is string: " << result2.is_string() << std::endl;
    std::cout << "   Value: '" << result2.as<std::string>() << "'" << std::endl;
    std::cout << "   Length: " << result2.as<std::string>().length() << std::endl;
    
    std::cout << "\n3. Testing print vs format:" << std::endl;
    std::cout << "   print('}}') outputs: ";
    engine.execute("print('}}')");
    std::cout << "   format('}}') returns: '" << engine.execute("format('}}')").as<std::string>() << "'" << std::endl;
    
    return 0;
}