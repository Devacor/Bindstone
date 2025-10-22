#include <jaiscript/jaiscript.hpp>
#include <iostream>

using namespace jai;

int main() {
    auto eng = engine::make();
    
    std::cout << "Testing static field with array...\n";
    try {
        eng->execute(R"(
            class Config {
                static array<string> options = ["a", "b", "c"];
            }
        )");
        std::cout << "✓ Array static field declared successfully\n";
        
        auto result = eng->execute("Config::options");
        std::cout << "✓ Static field accessed, type: " << static_cast<int>(result.type()) << "\n";
        std::cout << "✓ Is array: " << result.is_array() << "\n";
        
        if (result.is_array()) {
            std::cout << "✓ Array size: " << result.as_array().size() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cout << "✗ Error: " << e.what() << "\n";
    }
    
    std::cout << "\nTesting implicit access to static array in method...\n";
    try {
        eng->execute(R"(
            class Config {
                static array<string> options = ["a", "b", "c"];
                
                function testArrayAccess() -> string {
                    // Test subscript access first
                    return options[0];
                }
                
                function getOptionCount() -> int {
                    return options.size();
                }
            }
        )");
        std::cout << "✓ Class with method declared successfully\n";
        
        // Test subscript access first
        auto result1 = eng->execute("auto c = Config(); c.testArrayAccess()");
        std::cout << "✓ Array subscript access successful, result: " << result1.as<std::string>() << "\n";
        
        // Then test method call
        auto result2 = eng->execute("c.getOptionCount()");
        std::cout << "✓ Array method call successful, result: " << result2.as<int>() << "\n";
        
    } catch (const std::exception& e) {
        std::cout << "✗ Error: " << e.what() << "\n";
    }
    
    std::cout << "\nTesting static field with map...\n";
    try {
        eng->execute(R"(
            class Settings {
                static map<string, int> defaults = {"width": 800, "height": 600};
                
                function getWidth() -> int {
                    return defaults["width"];
                }
            }
        )");
        std::cout << "✓ Map static field declared successfully\n";
        
        auto result = eng->execute("auto s = Settings(); s.getWidth()");
        std::cout << "✓ Map access in method successful, result: " << result.as<int>() << "\n";
        
    } catch (const std::exception& e) {
        std::cout << "✗ Error: " << e.what() << "\n";
    }
    
    return 0;
}