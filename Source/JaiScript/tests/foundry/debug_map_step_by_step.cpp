#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;

class SimpleObj {
public:
    static int copy_count;
    int value;
    
    SimpleObj(int v) : value(v) {
        std::cout << "SimpleObj(" << v << ")\n";
    }
    
    SimpleObj(const SimpleObj& other) : value(other.value) {
        copy_count++;
        std::cout << "COPY SimpleObj: " << other.value << " (count=" << copy_count << ")\n";
    }
    
    int getValue() const { return value; }
    
    static void reset() { copy_count = 0; }
};

int SimpleObj::copy_count = 0;

int main() {
    auto eng = engine::make();
    
    class_builder<SimpleObj>(*eng, "SimpleObj")
        .constructor<int>()
        .method("getValue", &SimpleObj::getValue)
        .property("value", &SimpleObj::value)
        .build();
    
    SimpleObj::copy_count = 0;
    
    try {
        std::cout << "=== Step 1: Create object and map ===\n";
        eng->execute("auto obj = SimpleObj(123);");
        std::cout << "After object creation - copies: " << SimpleObj::copy_count << "\n";
        
        eng->execute("auto map = {};");
        std::cout << "After map creation - copies: " << SimpleObj::copy_count << "\n\n";
        
        std::cout << "=== Step 2: Test map subscript (creates reference) ===\n";
        try {
            auto ref_result = eng->execute("map[\"test\"]");
            std::cout << "map[\"test\"] type: " << static_cast<int>(ref_result.type()) << "\n";
            std::cout << "map[\"test\"] is_reference: " << ref_result.is_reference() << "\n";
            std::cout << "map[\"test\"] is_null: " << ref_result.is_null() << "\n";
        } catch (const std::exception& e) {
            std::cout << "map[\"test\"] error: " << e.what() << "\n";
        }
        std::cout << "After map access - copies: " << SimpleObj::copy_count << "\n\n";
        
        std::cout << "=== Step 3: Test assignment ===\n";
        try {
            eng->execute("map[\"key\"] = obj;");
            std::cout << "Assignment completed successfully\n";
        } catch (const std::exception& e) {
            std::cout << "Assignment error: " << e.what() << "\n";
        }
        std::cout << "After assignment - copies: " << SimpleObj::copy_count << "\n\n";
        
        std::cout << "=== Step 4: Test map state ===\n";
        try {
            auto size = eng->execute("map.size()");
            std::cout << "Map size: " << size.as<int>() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Map size error: " << e.what() << "\n";
        }
        
        try {
            auto contains = eng->execute("map.contains(\"key\")");
            std::cout << "Map contains 'key': " << contains.as<bool>() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Map contains error: " << e.what() << "\n";
        }
        
        std::cout << "=== Step 5: Test retrieval ===\n";
        try {
            auto retrieved = eng->execute("map[\"key\"]");
            std::cout << "Retrieved type: " << static_cast<int>(retrieved.type()) << "\n";
            std::cout << "Retrieved is_object: " << retrieved.is_object() << "\n";
            std::cout << "Retrieved is_null: " << retrieved.is_null() << "\n";
            
            if (retrieved.is_object()) {
                auto value = eng->execute("map[\"key\"].value");
                std::cout << "Retrieved value: " << value.as<int>() << "\n";
            }
        } catch (const std::exception& e) {
            std::cout << "Retrieval error: " << e.what() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cout << "MAIN ERROR: " << e.what() << "\n";
    }
    
    return 0;
}