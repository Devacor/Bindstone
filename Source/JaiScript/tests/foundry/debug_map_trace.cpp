#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/type_info.hpp>

using namespace jai;

int main() {
    auto eng = engine::make();
    
    try {
        std::cout << "=== Creating map directly ===\n";
        
        // Create map using factory method
        auto map_value = script_value::make_map(
            type_info::make_any(),  // Key type
            type_info::make_any(),  // Value type
            std::weak_ptr<engine>(eng)
        );
        eng->add_global("map", map_value);
        
        // Create value
        auto value = script_value(script_int(42), std::weak_ptr<engine>(eng));
        eng->add_global("value", value);
        
        std::cout << "Before assignment:\n";
        auto map_before = eng->get_variable("map");
        std::cout << "  Map type: " << static_cast<int>(map_before.type()) << "\n";
        std::cout << "  Map is_map: " << map_before.is_map() << "\n";
        
        // Try the assignment
        std::cout << "\nExecuting: map[\"key\"] = value;\n";
        auto result = eng->execute("map[\"key\"] = value;");
        std::cout << "Assignment result type: " << static_cast<int>(result.type()) << "\n";
        
        // Check the result
        std::cout << "\nAfter assignment:\n";
        auto retrieved = eng->execute("map[\"key\"]");
        std::cout << "  Retrieved type: " << static_cast<int>(retrieved.type()) << "\n";
        std::cout << "  Retrieved is_int: " << retrieved.is_int() << "\n";
        if (retrieved.is_int()) {
            std::cout << "  Retrieved value: " << retrieved.as<int>() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    
    return 0;
}