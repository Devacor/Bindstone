#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    jai::stdlib::register_all(engine);
    
    std::cout << "Testing edge case escaping:\n" << std::endl;
    
    // Test escaped braces with multiple arguments
    std::cout << "1. format(\"{{0}}\", \"ignored\"):" << std::endl;
    auto result = engine.execute("format(\"{{0}}\", \"ignored\")");
    std::cout << "   Result: '" << result.as<std::string>() << "'" << std::endl;
    std::cout << "   Expected: '{0}'" << std::endl;
    
    std::cout << "\n2. format(\"{{0}}\") - single arg:" << std::endl;
    result = engine.execute("format(\"{{0}}\")");
    std::cout << "   Result: '" << result.as<std::string>() << "'" << std::endl;
    std::cout << "   Expected: '{0}'" << std::endl;
    
    std::cout << "\n3. format(\"{001}\", \"A\", \"B\"):" << std::endl;
    result = engine.execute("format(\"{001}\", \"A\", \"B\")");
    std::cout << "   Result: '" << result.as<std::string>() << "'" << std::endl;
    std::cout << "   Expected: 'B' (index 1)" << std::endl;
    
    std::cout << "\n4. format(\"{{0}} and {0}\", \"TEST\"):" << std::endl;
    result = engine.execute("format(\"{{0}} and {0}\", \"TEST\")");
    std::cout << "   Result: '" << result.as<std::string>() << "'" << std::endl;
    std::cout << "   Expected: '{0} and TEST'" << std::endl;
    
    return 0;
}