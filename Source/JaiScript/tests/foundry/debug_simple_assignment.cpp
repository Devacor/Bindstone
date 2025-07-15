#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <memory>

using namespace jai;

int main() {
    std::cout << "Debugging simple map assignment...\n\n";
    
    auto eng = engine::make();
    
    try {
        // Test basic map functionality
        std::cout << "=== Test 1: Basic map ===\n";
        auto result1 = eng->execute("auto m = {}; m[\"test\"] = 42; m[\"test\"]");
        std::cout << "Basic map test result: " << result1.as<int>() << "\n\n";
        
        // Test with string
        std::cout << "=== Test 2: String assignment ===\n";
        auto result2 = eng->execute("auto m2 = {}; m2[\"key\"] = \"hello\"; m2[\"key\"]");
        std::cout << "String test result: " << result2.as<std::string>() << "\n\n";
        
        // Test separate steps
        std::cout << "=== Test 3: Separate steps ===\n";
        eng->execute("auto m3 = {};");
        eng->execute("auto val = 99;");
        auto result3a = eng->execute("m3[\"sep\"] = val; m3[\"sep\"]");
        std::cout << "Separate steps result: " << result3a.as<int>() << "\n\n";
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    
    return 0;
}