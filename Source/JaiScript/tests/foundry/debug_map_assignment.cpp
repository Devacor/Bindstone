#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <memory>

using namespace jai;

// Simple tracked object to monitor copies
class TrackedObject {
public:
    static int copy_count;
    static bool verbose;
    
    int value;
    
    TrackedObject(int val) : value(val) {
        if (verbose) std::cout << "  TrackedObject(" << val << ")\n";
    }
    
    TrackedObject(const TrackedObject& other) : value(other.value) {
        copy_count++;
        if (verbose) std::cout << "  COPY: " << other.value << " -> " << value << " (count=" << copy_count << ")\n";
    }
    
    void modify(int new_val) { value = new_val; }
    int get() const { return value; }
    
    static void reset() { copy_count = 0; }
};

int TrackedObject::copy_count = 0;
bool TrackedObject::verbose = false;

int main() {
    std::cout << "Debugging map assignment...\n\n";
    
    auto eng = engine::make();
    
    class_builder<TrackedObject>(*eng, "TrackedObject")
        .constructor<int>()
        .method("modify", &TrackedObject::modify)
        .method("get", &TrackedObject::get)
        .property("value", &TrackedObject::value)
        .build();
    
    TrackedObject::reset();
    TrackedObject::verbose = true;
    
    try {
        std::cout << "=== Test: Variable Declaration ===\n";
        eng->execute("auto obj = TrackedObject(42);");
        std::cout << "Copy count after variable declaration: " << TrackedObject::copy_count << "\n\n";
        
        std::cout << "=== Test: Map Creation ===\n";
        eng->execute("auto map = {};");
        std::cout << "Copy count after map creation: " << TrackedObject::copy_count << "\n\n";
        
        std::cout << "=== Test: Map Assignment ===\n";
        eng->execute("map[\"key\"] = obj;");
        std::cout << "Copy count after map assignment: " << TrackedObject::copy_count << "\n\n";
        
        std::cout << "=== Test: Map Access ===\n";
        auto result = eng->execute("map[\"key\"].value");
        std::cout << "Copy count after map access: " << TrackedObject::copy_count << "\n";
        std::cout << "Retrieved value: " << result.as<int>() << "\n\n";
        
        std::cout << "=== Test: Direct Map Assignment ===\n";
        TrackedObject::reset();
        eng->execute("auto direct_map = {\"key2\": TrackedObject(99)};");
        std::cout << "Copy count after direct map assignment: " << TrackedObject::copy_count << "\n";
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    
    TrackedObject::verbose = false;
    
    return 0;
}