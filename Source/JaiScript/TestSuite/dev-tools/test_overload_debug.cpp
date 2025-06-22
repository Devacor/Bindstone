#include <iostream>
#include <jaiscript/jaiscript.hpp>

using namespace JaiScript;

int main() {
    try {
        Engine engine;
        
        std::cout << "=== Registering overloaded sum functions ===\n";
        
        std::cout << "Registering sum() with arity 0\n";
        engine.addFunction("sum", []() -> int { 
            std::cout << "sum() called\n";
            return 0; 
        });
        
        std::cout << "Registering sum(int) with arity 1\n";
        engine.addFunction("sum", [](int a) -> int { 
            std::cout << "sum(int) called with " << a << "\n";
            return a; 
        });
        
        std::cout << "Registering sum(int, int) with arity 2\n";
        engine.addFunction("sum", [](int a, int b) -> int { 
            std::cout << "sum(int, int) called with " << a << ", " << b << "\n";
            return a + b; 
        });
        
        std::cout << "Registration complete. Checking hasFunction: " << engine.hasFunction("sum") << "\n";
        
        std::cout << "\n=== Testing sum() ===\n";
        Value result1 = engine.execute("sum();");
        std::cout << "Result: " << result1.toString() << "\n";
        
        std::cout << "\n=== Testing sum(5) ===\n";
        Value result2 = engine.execute("sum(5);");
        std::cout << "Result: " << result2.toString() << "\n";
        
        std::cout << "\n=== Testing sum(5, 3) ===\n";
        Value result3 = engine.execute("sum(5, 3);");
        std::cout << "Result: " << result3.toString() << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}