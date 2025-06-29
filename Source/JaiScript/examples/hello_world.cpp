#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    std::cout << "JaiScript version: " << jai::version() << std::endl;
    
    auto engine = jai::createEngine();
    
    // Register standard library functions
    jai::stdlib::register_all(*engine);
    
    // Test basic functionality
    engine->add_global("test", jai::script_value(42));
    
    // Now print should work
    engine->eval("print('Hello from JaiScript!');");
    
    // Test print with multiple arguments
    engine->eval("print('The answer is', test);");
    
    // Test write (no newline)
    engine->eval("write('Hello '); write('World!'); print();");
    
    // Test type_of
    engine->eval("print('Type of test:', type_of(test));");
    
    return 0;
}