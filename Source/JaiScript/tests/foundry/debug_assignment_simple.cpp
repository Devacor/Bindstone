#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;

class SimpleObj {
public:
    static int copy_count;
    int value;
    
    SimpleObj(int v) : value(v) {
        std::cout << "SimpleObj(" << v << ")" << std::endl;
    }
    
    SimpleObj(const SimpleObj& other) : value(other.value) {
        copy_count++;
        std::cout << "COPY SimpleObj: " << other.value << " (count=" << copy_count << ")" << std::endl;
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
        std::cout << "=== Creating object ===\n";
        eng->execute("auto obj = SimpleObj(456);");
        std::cout << "Copy count after creation: " << SimpleObj::copy_count << "\n\n";
        
        std::cout << "=== Testing assignment to variable ===\n";
        eng->execute("auto obj2 = obj;");
        std::cout << "Copy count after variable assignment: " << SimpleObj::copy_count << "\n\n";
        
        std::cout << "=== Testing map assignment ===\n";
        eng->execute("auto map = {};");
        
        try {
            std::cout << "About to execute map assignment...\n";
            eng->execute("map[\"key\"] = obj;");
            std::cout << "Map assignment completed\n";
        } catch (const std::exception& e) {
            std::cout << "Map assignment error: " << e.what() << "\n";
        }
        std::cout << "Copy count after map assignment: " << SimpleObj::copy_count << "\n\n";
        
        std::cout << "=== Testing map retrieval ===\n";
        try {
            auto retrieved = eng->execute("map[\"key\"]");
            std::cout << "Retrieved successfully, type: " << static_cast<int>(retrieved.type()) << "\n";
            
            auto value = eng->execute("map[\"key\"].value");
            std::cout << "Retrieved value: " << value.as<int>() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Retrieval error: " << e.what() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    
    return 0;
}