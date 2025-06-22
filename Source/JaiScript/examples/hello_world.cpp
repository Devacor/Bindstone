#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    std::cout << "JaiScript version: " << JaiScript::version() << std::endl;
    
    auto engine = JaiScript::createEngine();
    
    // Test basic functionality
    engine->addGlobal("test", JaiScript::Value(42));
    
    // This will just print a stub message for now
    engine->eval("print('Hello from JaiScript!');");
    
    return 0;
}