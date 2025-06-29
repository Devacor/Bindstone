#include <iostream>
#include <jaiscript/core/engine.hpp>

using namespace jai;

int main() {
    engine eng;
    
    std::cout << "=== Simple Map Subscript Debug ===\n";
    
    // Just the map subscript that's failing
    script_value result = eng.execute("var map = {{\"test\", 5}}; map[\"test\"]");
    
    std::cout << "Result is_null: " << result.is_null() << "\n";
    if (!result.is_null()) {
        std::cout << "Result value: " << result.as<int>() << "\n";
    }
    
    return 0;
}