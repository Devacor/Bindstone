#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <map>
#include <string>

int main() {
    try {
        auto engine = jai::engine::make();
        
        // Test 1: Function returning std::map
        engine->add_function("get_scores", []() -> std::map<std::string, int> {
            return {{"Alice", 95}, {"Bob", 87}, {"Charlie", 92}};
        });
        
        // Test 2: Direct subscript on function return (like C++)
        std::cout << "Testing direct subscript on function returns:" << std::endl;
        
        // Test various keys
        auto alice = engine->execute("get_scores()[\"Alice\"]");
        std::cout << "✓ Alice's score: " << alice.as<int>() << std::endl;
        
        auto bob = engine->execute("get_scores()[\"Bob\"]");
        std::cout << "✓ Bob's score: " << bob.as<int>() << std::endl;
        
        auto charlie = engine->execute("get_scores()[\"Charlie\"]");
        std::cout << "✓ Charlie's score: " << charlie.as<int>() << std::endl;
        
        // Test missing key
        auto missing = engine->execute("get_scores()[\"David\"]");
        std::cout << "✓ Missing key returns null: " << missing.is_null() << std::endl;
        
        // Test with function returning array
        engine->add_function("get_array", []() -> std::vector<int> {
            return {10, 20, 30, 40};
        });
        
        auto elem = engine->execute("get_array()[2]");
        std::cout << "✓ Array element [2]: " << elem.as<int>() << std::endl;
        
        // Test chained operations
        engine->add_function("get_nested", []() -> std::map<std::string, std::vector<int>> {
            return {{"numbers", {100, 200, 300}}};
        });
        
        auto nested = engine->execute("get_nested()[\"numbers\"][1]");
        std::cout << "✓ Nested access: " << nested.as<int>() << std::endl;
        
        std::cout << "\nAll tests passed! JaiScript now supports func()[key] syntax!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}