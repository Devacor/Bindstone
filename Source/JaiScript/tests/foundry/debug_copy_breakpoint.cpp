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
        std::cout << "\n=== COPY #" << copy_count << " from id=" << other.id << " to id=" << id << " ===" << std::endl;
        // SET BREAKPOINT HERE IN GDB
        int breakpoint_here = 1;  // Line for breakpoint
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
    
    class_builder<TrackedObject>(*eng, "TrackedObject")
        .constructor<int>()
        .method("get", &TrackedObject::get)
        .property("value", &TrackedObject::value)
        .build();
        
    // Function that takes by value - should copy on call
    eng->add_function("take_by_value", [](TrackedObject obj) -> int {
        std::cout << "Inside take_by_value: obj.id=" << obj.id << std::endl;
        return obj.get();
    });
    
    TrackedObject::reset();
    
    try {
        std::cout << "=== Starting test ===" << std::endl;
        
        // Create object - should be 0 copies
        eng->execute("auto obj = TrackedObject(42);");
        std::cout << "\nAfter creation: " << TrackedObject::copy_count << " copies\n" << std::endl;
        
        // Call function with by-value parameter
        auto result = eng->execute("take_by_value(obj)");
        std::cout << "\nAfter function call: " << TrackedObject::copy_count << " copies" << std::endl;
        std::cout << "Result: " << result.as<int>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
    }
    
    return 0;
}