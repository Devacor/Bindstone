#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;

class SimpleObj {
public:
    int value;
    SimpleObj(int v) : value(v) {}
    int getValue() const { return value; }
};

int main() {
    auto eng = engine::make();
    
    class_builder<SimpleObj>(*eng, "SimpleObj")
        .constructor<int>()
        .method("getValue", &SimpleObj::getValue)
        .property("value", &SimpleObj::value)
        .build();
    
    try {
        std::cout << "=== Creating objects ===\n";
        eng->execute("auto obj = SimpleObj(999);");
        eng->execute("auto map = {};");
        
        std::cout << "=== Testing map assignment ===\n";
        eng->execute("map[\"key\"] = obj;");
        
        std::cout << "=== Testing map contents ===\n";
        auto map_size = eng->execute("map.size()");
        std::cout << "Map size: " << map_size.as<int>() << "\n";
        
        auto has_key = eng->execute("map.contains(\"key\")");
        std::cout << "Contains 'key': " << has_key.as<bool>() << "\n";
        
        std::cout << "=== Testing direct access ===\n";
        try {
            auto direct = eng->execute("map[\"key\"]");
            std::cout << "Direct access type: " << static_cast<int>(direct.type()) << "\n";
            std::cout << "Is null: " << direct.is_null() << "\n";
            std::cout << "Is object: " << direct.is_object() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Direct access error: " << e.what() << "\n";
        }
        
        std::cout << "=== Testing method call ===\n";
        try {
            auto method_result = eng->execute("map[\"key\"].getValue()");
            std::cout << "Method result: " << method_result.as<int>() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Method call error: " << e.what() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    
    return 0;
}