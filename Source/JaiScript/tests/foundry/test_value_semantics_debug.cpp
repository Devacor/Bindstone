#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <memory>

using namespace jai;

// Test class that tracks all copy/move operations
class TrackedObject {
public:
    static int instance_count;
    static int copy_count;
    static int move_count;
    static bool verbose;
    
    int id;
    int value;
    
    TrackedObject(int val) : id(++instance_count), value(val) {
        if (verbose) std::cout << "  TrackedObject(" << val << ") -> id=" << id << "\n";
    }
    
    TrackedObject(const TrackedObject& other) : id(++instance_count), value(other.value) {
        copy_count++;
        if (verbose) std::cout << "  COPY from id=" << other.id << " to id=" << id << "\n";
    }
    
    TrackedObject(TrackedObject&& other) noexcept : id(++instance_count), value(other.value) {
        move_count++;
        if (verbose) std::cout << "  MOVE from id=" << other.id << " to id=" << id << "\n";
    }
    
    ~TrackedObject() {
        if (verbose) std::cout << "  ~TrackedObject() id=" << id << "\n";
    }
    
    void modify(int new_val) { value = new_val; }
    int get() const { return value; }
    
    static void reset() {
        instance_count = 0;
        copy_count = 0;
        move_count = 0;
    }
};

int TrackedObject::instance_count = 0;
int TrackedObject::copy_count = 0;
int TrackedObject::move_count = 0;
bool TrackedObject::verbose = false;

void register_tracked_object(engine& eng) {
    class_builder<TrackedObject>(eng, "TrackedObject")
        .constructor<int>()
        .method("modify", &TrackedObject::modify)
        .method("get", &TrackedObject::get)
        .property("value", &TrackedObject::value)
        .property("id", &TrackedObject::id)
        .build();
}

int main() {
    std::cout << "Testing value semantics for deep copy/reference issues...\n\n";
    
    bool passed = true;
    
    try {
        // Test function_by_value_copies
        auto eng = engine::make();
        register_tracked_object(*eng);
        
        // Register a function that takes by value
        eng->add_function("take_by_value", [](TrackedObject obj) -> int {
            return obj.get();
        });
        
        TrackedObject::reset();
        TrackedObject::verbose = true;
        std::cout << "function_by_value_copies test:\n";
        auto result = eng->execute("auto obj = TrackedObject(42); take_by_value(obj)");
        TrackedObject::verbose = false;
        
        std::cout << "  Copy count: " << TrackedObject::copy_count << " (expected: 2)\n";
        std::cout << "  Result: " << result.as<int>() << " (expected: 42)\n";
        
        if (TrackedObject::copy_count != 2 || result.as<int>() != 42) {
            std::cout << "  FAILED\n";
            passed = false;
        } else {
            std::cout << "  PASSED\n";
        }
    } catch (const std::exception& e) {
        std::cout << "function_by_value_copies test FAILED with exception: " << e.what() << "\n";
        passed = false;
    }
    
    try {
        // Test function_by_reference_no_copy
        auto eng = engine::make();
        register_tracked_object(*eng);
        
        // Register a function that takes by reference
        eng->add_function("take_by_ref", [](TrackedObject& obj) -> int {
            return obj.get();
        });
        
        TrackedObject::reset();
        TrackedObject::verbose = true;
        std::cout << "\nfunction_by_reference_no_copy test:\n";
        auto result = eng->execute("auto obj = TrackedObject(42); take_by_ref(obj)");
        TrackedObject::verbose = false;
        
        std::cout << "  Copy count: " << TrackedObject::copy_count << " (expected: 1)\n";
        std::cout << "  Result: " << result.as<int>() << " (expected: 42)\n";
        
        if (TrackedObject::copy_count != 1 || result.as<int>() != 42) {
            std::cout << "  FAILED\n";
            passed = false;
        } else {
            std::cout << "  PASSED\n";
        }
    } catch (const std::exception& e) {
        std::cout << "function_by_reference_no_copy test FAILED with exception: " << e.what() << "\n";
        passed = false;
    }
    
    try {
        // Test map_assignment_copies
        auto eng = engine::make();
        register_tracked_object(*eng);
        
        TrackedObject::reset();
        TrackedObject::verbose = true;
        std::cout << "\nmap_assignment_copies test:\n";
        eng->execute("auto obj = TrackedObject(42);");
        eng->execute("auto myMap = {};");
        eng->execute("myMap[\"key\"] = obj;");
        
        // Verify the value is in the map
        auto result = eng->execute("myMap[\"key\"].value");
        TrackedObject::verbose = false;
        
        std::cout << "  Copy count: " << TrackedObject::copy_count << " (expected: 2)\n";
        std::cout << "  Result: " << result.as<int>() << " (expected: 42)\n";
        
        if (TrackedObject::copy_count != 2 || result.as<int>() != 42) {
            std::cout << "  FAILED\n";
            passed = false;
        } else {
            std::cout << "  PASSED\n";
        }
    } catch (const std::exception& e) {
        std::cout << "map_assignment_copies test FAILED with exception: " << e.what() << "\n";
        passed = false;
    }
    
    std::cout << "\n" << (passed ? "All tests PASSED" : "Some tests FAILED") << "\n";
    return passed ? 0 : 1;
}