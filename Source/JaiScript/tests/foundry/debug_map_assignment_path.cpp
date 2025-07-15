#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;

int main() {
    auto eng = engine::make();
    
    try {
        std::cout << "=== Testing simple map assignment ===\n";
        
        // Create a simple map and value
        auto result1 = eng->execute("auto map = {};");
        auto result2 = eng->execute("auto value = 42;");
        
        std::cout << "Before assignment:\n";
        try {
            auto map_before = eng->get_variable("map");
            std::cout << "  Map type: " << static_cast<int>(map_before.type()) << "\n";
            std::cout << "  Map is_map: " << map_before.is_map() << "\n";
        } catch (const std::exception& e) {
            std::cout << "  Error getting map: " << e.what() << "\n";
        }
        
        // Try the assignment
        std::cout << "\nExecuting: map[\"key\"] = value;\n";
        eng->execute("map[\"key\"] = value;");
        std::cout << "Assignment completed without exception\n";
        
        // Check the result
        std::cout << "\nAfter assignment:\n";
        auto result = eng->execute("map[\"key\"]");
        std::cout << "  Retrieved type: " << static_cast<int>(result.type()) << "\n";
        std::cout << "  Retrieved is_int: " << result.is_int() << "\n";
        if (result.is_int()) {
            std::cout << "  Retrieved value: " << result.as<int>() << "\n";
        }
        
        // Test with a string key too
        std::cout << "\n=== Testing with different key ===\n";
        eng->execute("map[\"other\"] = 99;");
        auto other = eng->execute("map[\"other\"]");
        std::cout << "  Retrieved other: " << other.as<int>() << "\n";
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    
    return 0;
}