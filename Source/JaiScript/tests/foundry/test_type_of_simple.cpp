#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        std::cout << "Test 1: Basic type_of\n";
        eng->execute(R"(
            var x = 42;
            var t = type_of(x);
            print(t);
        )");
        
        std::cout << "\nTest 2: String concatenation\n";
        eng->execute(R"(
            var s1 = "Hello ";
            var s2 = "World";
            var s3 = s1 + s2;
            print(s3);
        )");
        
        std::cout << "\nTest 3: type_of with concatenation\n";
        eng->execute(R"(
            var x = 42;
            var msg = "type is: ";
            var t = type_of(x);
            var full = msg + t;
            print(full);
        )");
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}