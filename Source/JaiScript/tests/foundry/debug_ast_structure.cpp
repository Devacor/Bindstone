#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;

int main() {
    auto eng = engine::make();
    
    // Test to see what happens 
    std::cout << "=== Simple variable assignment ===\n";
    try {
        eng->execute("auto x = 5; x = 10;");
        std::cout << "Variable assignment succeeded\n";
    } catch (const std::exception& e) {
        std::cout << "Variable assignment failed: " << e.what() << "\n";
    }
    
    std::cout << "\n=== Map creation ===\n";
    try {
        eng->execute("auto map = {};");
        std::cout << "Map creation succeeded\n";
    } catch (const std::exception& e) {
        std::cout << "Map creation failed: " << e.what() << "\n";
    }
    
    std::cout << "\n=== Simple map assignment ===\n";
    try {
        eng->execute("auto map = {}; map[\"key\"] = 42;");
        std::cout << "Map assignment succeeded\n";
    } catch (const std::exception& e) {
        std::cout << "Map assignment failed: " << e.what() << "\n";
    }
    
    std::cout << "\n=== Map retrieval ===\n";
    try {
        auto result = eng->execute("auto map = {}; map[\"key\"] = 42; map[\"key\"]");
        std::cout << "Map retrieval succeeded, got type: " << static_cast<int>(result.type()) << ", value: " << result.to_string() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Map retrieval failed: " << e.what() << "\n";
    }
    
    return 0;
}