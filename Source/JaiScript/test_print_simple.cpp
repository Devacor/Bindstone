#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    jai::stdlib::register_all(engine);
    
    // Test 1: Basic print
    std::cout << "Test 1: ";
    engine.execute(R"(print("Hello"))");
    
    // Test 2: Multiple args without format
    std::cout << "Test 2: ";
    engine.execute(R"(print("A", "B", "C"))");
    
    // Test 3: Format string
    std::cout << "Test 3: ";  
    engine.execute(R"(print("{} + {} = {}", 1, 2, 3))");
    
    // Test 4: Check if skip_newline exists
    std::cout << "Test 4: skip_newline type = ";
    engine.execute(R"(print(type_of(skip_newline)))");
    
    return 0;
}