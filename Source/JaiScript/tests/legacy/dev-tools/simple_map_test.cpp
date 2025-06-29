#include <iostream>
#include <jaiscript/core/engine.hpp>

using namespace jai;

int main() {
    engine eng;
    
    // Test 1: Simple expression should return value
    std::cout << "Test 1: Single expression\n";
    script_value r1 = eng.execute("42");
    std::cout << "  Result: " << (r1.is_null() ? "null" : r1.to_string()) << "\n\n";
    
    // Test 2: Map creation
    std::cout << "Test 2: Map creation\n";
    script_value r2 = eng.execute("{{\"key\", 123}}");
    std::cout << "  Result: " << (r2.is_null() ? "null" : "map") << "\n\n";
    
    // Test 3: Variable declaration followed by expression
    std::cout << "Test 3: Variable + expression\n";
    script_value r3 = eng.execute("var x = 10; x");
    std::cout << "  Result: " << (r3.is_null() ? "null" : r3.to_string()) << "\n\n";
    
    // Test 4: Map creation + access
    std::cout << "Test 4: Map creation + access\n";
    script_value r4 = eng.execute(R"(
        var map = {{"one", 1}, {"two", 2}};
        map["one"]
    )");
    std::cout << "  Result: " << (r4.is_null() ? "null" : r4.to_string()) << "\n\n";
    
    // Test 5: Just map access as single expression
    std::cout << "Test 5: Map access only\n";
    eng.execute("var map = {{\"one\", 1}}");
    script_value r5 = eng.execute("map[\"one\"]");
    std::cout << "  Result: " << (r5.is_null() ? "null" : r5.to_string()) << "\n";
    
    // Check if map was created correctly
    std::cout << "  Map size: " << eng.get_variable("map").as_map().size() << "\n";
    
    // Try accessing a known key
    auto& map_val = eng.get_variable("map").as_map();
    for (const auto& [k, v] : map_val) {
        std::cout << "  Map contains: " << k.to_string() << " => " << v.to_string() << "\n";
    }
    std::cout << "\n";
    
    // Test 6: Array access for comparison
    std::cout << "Test 6: Array access\n";
    script_value r6 = eng.execute(R"(
        var arr = [10, 20, 30];
        arr[1]
    )");
    std::cout << "  Result: " << (r6.is_null() ? "null" : r6.to_string()) << "\n\n";
    
    return 0;
}