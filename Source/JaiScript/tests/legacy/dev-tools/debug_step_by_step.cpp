#include <iostream>
#include <jaiscript/core/engine.hpp>

using namespace jai;

int main() {
    engine eng;
    
    std::cout << "Step 1: Simple literal" << std::endl;
    script_value result1 = eng.execute("42");
    std::cout << "Result: " << (result1.is_null() ? "null" : result1.to_string()) << std::endl;
    
    std::cout << "\nStep 2: Simple array literal" << std::endl;
    script_value result2 = eng.execute("[1, 2, 3]");
    std::cout << "Result: " << (result2.is_null() ? "null" : "array") << std::endl;
    
    std::cout << "\nStep 3: Simple map literal" << std::endl;
    script_value result3 = eng.execute("{{\"test\", 5}}");
    std::cout << "Result: " << (result3.is_null() ? "null" : "map") << std::endl;
    
    std::cout << "\nStep 4: Variable assignment + return" << std::endl;
    script_value result4 = eng.execute("var x = 10; x");
    std::cout << "Result: " << (result4.is_null() ? "null" : result4.to_string()) << std::endl;
    
    std::cout << "\nStep 5: Variable assignment + map return" << std::endl;
    script_value result5 = eng.execute("var map = {{\"test\", 5}}; map");
    std::cout << "Result: " << (result5.is_null() ? "null" : "map") << std::endl;
    
    return 0;
}