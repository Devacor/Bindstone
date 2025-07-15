#include <jaiscript/jaiscript.hpp>
#include <iostream>

using namespace jai;

int main() {
    try {
        auto engine = engine::make();
        
        // Test different registration orders
        std::cout << "Test 1: Only map conversion" << std::endl;
        {
            auto engine = engine::make();
            engine->add_bound_map_conversion<std::string, int>();
            
            engine->add_function("test_only_map", [](const bound_map<std::string, int>& scores) -> int {
                int total = 0;
                for (const auto& [key, value] : scores) {
                    std::cout << "Key: " << key << ", Value: " << value << std::endl;
                    total += value;
                }
                return total;
            });
            
            auto result = engine->execute(R"(test_only_map({"alice": 10, "bob": 20}))");
            std::cout << "Result: " << result.as<int>() << std::endl;
        }
        
        std::cout << "\nTest 2: Array then map conversion" << std::endl;
        {
            auto engine = engine::make();
            engine->add_bound_array_conversion<int>();
            engine->add_bound_map_conversion<std::string, int>();
            
            engine->add_function("test_array_then_map", [](const bound_map<std::string, int>& scores) -> int {
                int total = 0;
                for (const auto& [key, value] : scores) {
                    std::cout << "Key: " << key << ", Value: " << value << std::endl;
                    total += value;
                }
                return total;
            });
            
            auto result = engine->execute(R"(test_array_then_map({"alice": 10, "bob": 20}))");
            std::cout << "Result: " << result.as<int>() << std::endl;
        }
        
        std::cout << "\nTest 3: String array then map conversion" << std::endl;
        {
            auto engine = engine::make();
            engine->add_bound_array_conversion<std::string>();
            engine->add_bound_map_conversion<std::string, int>();
            
            engine->add_function("test_string_array_then_map", [](const bound_map<std::string, int>& scores) -> int {
                int total = 0;
                for (const auto& [key, value] : scores) {
                    std::cout << "Key: " << key << ", Value: " << value << std::endl;
                    total += value;
                }
                return total;
            });
            
            auto result = engine->execute(R"(test_string_array_then_map({"alice": 10, "bob": 20}))");
            std::cout << "Result: " << result.as<int>() << std::endl;
        }
        
        std::cout << "\nTest 4: All three conversions" << std::endl;
        {
            auto engine = engine::make();
            engine->add_bound_array_conversion<int>();
            engine->add_bound_array_conversion<std::string>();
            engine->add_bound_map_conversion<std::string, int>();
            
            engine->add_function("test_all_three", [](const bound_map<std::string, int>& scores) -> int {
                int total = 0;
                for (const auto& [key, value] : scores) {
                    std::cout << "Key: " << key << ", Value: " << value << std::endl;
                    total += value;
                }
                return total;
            });
            
            auto result = engine->execute(R"(test_all_three({"alice": 10, "bob": 20}))");
            std::cout << "Result: " << result.as<int>() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}