#include <iostream>
#include <jaiscript/core/engine.hpp>

using namespace jai;

int main() {
    engine eng;
    
    std::cout << "Test 1: Just create a map variable and return it\n";
    script_value result1 = eng.execute(R"(
        var m = {{"key", 42}};
        m
    )");
    std::cout << "Result: " << (result1.is_null() ? "null" : "has value") << "\n";
    std::cout << "Is map: " << result1.is_map() << "\n";
    
    std::cout << "\nTest 2: Check if we can access the variable directly\n";
    if (eng.has_variable("m")) {
        script_value var_m = eng.get_variable("m");
        std::cout << "Variable m exists: " << (!var_m.is_null()) << "\n";
        std::cout << "Variable m is map: " << var_m.is_map() << "\n";
    } else {
        std::cout << "Variable m does not exist\n";
    }
    
    return 0;
}