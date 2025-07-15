#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <iostream>

using namespace jai;

class TrackedObject {
public:
    static int copy_count;
    static int next_id;
    int id;
    int value;
    
    TrackedObject(int v) : id(++next_id), value(v) {
        std::cout << "CONSTRUCT TrackedObject(" << v << ") -> id=" << id << std::endl;
    }
    
    TrackedObject(const TrackedObject& other) : id(++next_id), value(other.value) {
        copy_count++;
        std::cout << "COPY #" << copy_count << " from id=" << other.id << " to id=" << id << " (value=" << value << ")" << std::endl;
    }
    
    ~TrackedObject() {
        std::cout << "DESTROY ~TrackedObject() id=" << id << std::endl;
    }
    
    int get() const { return value; }
    
    static void reset() { copy_count = 0; next_id = 0; }
};

int TrackedObject::copy_count = 0;
int TrackedObject::next_id = 0;

int main() {
    auto eng = engine::make();
    eng->set_backend(backend_type::interpreter);
    
    // Register TrackedObject class
    class_builder<TrackedObject>(*eng, "TrackedObject")
        .constructor<int>()
        .method("get", &TrackedObject::get)
        .property("value", &TrackedObject::value)
        .build();
    
    // Test 1: Simple function that returns the value    
    eng->add_function("take_by_value", [](TrackedObject obj) -> int {
        std::cout << "\n=== INSIDE take_by_value: obj.id=" << obj.id << " ===" << std::endl;
        return obj.get();
    });
    
    // Test 2: Function that takes reference (for comparison)
    eng->add_function("take_by_ref", [](const TrackedObject& obj) -> int {
        std::cout << "\n=== INSIDE take_by_ref: obj.id=" << obj.id << " ===" << std::endl;
        return obj.get();
    });
    
    TrackedObject::reset();
    
    try {
        std::cout << "=== Creating object ===" << std::endl;
        eng->execute("auto obj = TrackedObject(42);");
        std::cout << "\nCopies after creation: " << TrackedObject::copy_count << "\n" << std::endl;
        
        std::cout << "=== Test 1: Call by value ===" << std::endl;
        TrackedObject::copy_count = 0;  // Reset for this test
        auto result1 = eng->execute("take_by_value(obj)");
        std::cout << "\nCopies in by-value call: " << TrackedObject::copy_count << std::endl;
        std::cout << "Result: " << result1.as<int>() << "\n" << std::endl;
        
        std::cout << "=== Test 2: Call by reference ===" << std::endl;
        TrackedObject::copy_count = 0;  // Reset for this test
        auto result2 = eng->execute("take_by_ref(obj)");
        std::cout << "\nCopies in by-ref call: " << TrackedObject::copy_count << std::endl;
        std::cout << "Result: " << result2.as<int>() << "\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
    }
    
    return 0;
}