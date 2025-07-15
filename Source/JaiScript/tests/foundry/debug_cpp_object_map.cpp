#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;

class SimpleObj {
public:
    static int copies;
    int value;
    
    SimpleObj(int v) : value(v) {
        std::cout << "SimpleObj(" << v << ")\n";
    }
    
    SimpleObj(const SimpleObj& other) : value(other.value) {
        copies++;
        std::cout << "COPY SimpleObj: " << other.value << " (copies=" << copies << ")\n";
    }
    
    int getValue() const { return value; }
    
    static void reset() { copies = 0; }
};

int SimpleObj::copies = 0;

int main() {
    auto eng = engine::make();
    
    class_builder<SimpleObj>(*eng, "SimpleObj")
        .constructor<int>()
        .method("getValue", &SimpleObj::getValue)
        .property("value", &SimpleObj::value)
        .build();
    
    SimpleObj::reset();
    
    try {
        std::cout << "=== Test 1: Object creation ===\n";
        eng->execute("auto obj = SimpleObj(100);");
        std::cout << "Copies: " << SimpleObj::copies << "\n\n";
        
        std::cout << "=== Test 2: Map creation ===\n";
        eng->execute("auto map = {};");
        std::cout << "Copies: " << SimpleObj::copies << "\n\n";
        
        std::cout << "=== Test 3: Direct map access (creates default) ===\n";
        try {
            auto result = eng->execute("map[\"test\"]");
            std::cout << "Map access result type: " << static_cast<int>(result.type()) << "\n";
            std::cout << "Is null: " << result.is_null() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Map access error: " << e.what() << "\n";
        }
        std::cout << "Copies: " << SimpleObj::copies << "\n\n";
        
        std::cout << "=== Test 4: Map assignment ===\n";
        try {
            eng->execute("map[\"key\"] = obj;");
            std::cout << "Map assignment completed\n";
        } catch (const std::exception& e) {
            std::cout << "Map assignment error: " << e.what() << "\n";
        }
        std::cout << "Copies after assignment: " << SimpleObj::copies << "\n\n";
        
        std::cout << "=== Test 5: Map retrieval ===\n";
        try {
            auto result = eng->execute("map[\"key\"].value");
            std::cout << "Retrieved value: " << result.as<int>() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Retrieval error: " << e.what() << "\n";
        }
        std::cout << "Final copies: " << SimpleObj::copies << "\n\n";
        
    } catch (const std::exception& e) {
        std::cout << "MAIN ERROR: " << e.what() << "\n";
    }
    
    return 0;
}