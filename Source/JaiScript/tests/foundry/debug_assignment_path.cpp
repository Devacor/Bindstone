#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <memory>

using namespace jai;

// Test object to debug assignment
class TrackedObject {
public:
    static int copy_count;
    static int default_count;
    
    int value;
    
    TrackedObject() : value(0) { 
        default_count++;
        std::cout << "  TrackedObject() DEFAULT constructor (count=" << default_count << ")\n";
    }
    
    TrackedObject(int val) : value(val) {
        std::cout << "  TrackedObject(" << val << ")\n";
    }
    
    TrackedObject(const TrackedObject& other) : value(other.value) {
        copy_count++;
        std::cout << "  COPY: " << other.value << " -> " << value << " (count=" << copy_count << ")\n";
    }
    
    TrackedObject& operator=(const TrackedObject& other) {
        std::cout << "  ASSIGNMENT: " << value << " <- " << other.value << "\n";
        value = other.value;
        return *this;
    }
    
    int get() const { return value; }
    
    static void reset() { 
        copy_count = 0; 
        default_count = 0;
    }
};

int TrackedObject::copy_count = 0;
int TrackedObject::default_count = 0;

int main() {
    std::cout << "Debugging assignment path...\n\n";
    
    auto eng = engine::make();
    
    class_builder<TrackedObject>(*eng, "TrackedObject")
        .constructor<>()  // Default constructor
        .constructor<int>()
        .method("get", &TrackedObject::get)
        .property("value", &TrackedObject::value)
        .build();
    
    TrackedObject::reset();
    
    try {
        std::cout << "=== Step 1: Create object ===\n";
        auto result1 = eng->execute("auto obj = TrackedObject(42);");
        std::cout << "Copies: " << TrackedObject::copy_count << ", Defaults: " << TrackedObject::default_count << "\n\n";
        
        std::cout << "=== Step 2: Create map ===\n";
        auto result2 = eng->execute("auto map = {};");
        std::cout << "Copies: " << TrackedObject::copy_count << ", Defaults: " << TrackedObject::default_count << "\n\n";
        
        std::cout << "=== Step 3: Test simple assignment (var to var) ===\n";
        auto result3 = eng->execute("auto obj2 = obj;");
        std::cout << "Copies: " << TrackedObject::copy_count << ", Defaults: " << TrackedObject::default_count << "\n\n";
        
        std::cout << "=== Step 4: Test map assignment ===\n";
        auto result4 = eng->execute("map[\"key\"] = obj;");
        std::cout << "Copies: " << TrackedObject::copy_count << ", Defaults: " << TrackedObject::default_count << "\n\n";
        
        std::cout << "=== Step 5: Try to access map value ===\n";
        try {
            auto result5 = eng->execute("map[\"key\"].value");
            std::cout << "Value retrieved: " << result5.as<int>() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Failed to retrieve: " << e.what() << "\n";
        }
        std::cout << "Final - Copies: " << TrackedObject::copy_count << ", Defaults: " << TrackedObject::default_count << "\n\n";
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    
    return 0;
}