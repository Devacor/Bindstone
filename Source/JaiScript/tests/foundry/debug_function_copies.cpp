#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;

class TrackedObject {
public:
    static int copy_count;
    static int next_id;
    int id;
    int value;
    
    TrackedObject(int v) : id(++next_id), value(v) {
        std::cout << "TrackedObject(" << v << ") -> id=" << id << std::endl;
    }
    
    TrackedObject(const TrackedObject& other) : id(++next_id), value(other.value) {
        copy_count++;
        std::cout << "COPY from id=" << other.id << " to id=" << id << std::endl;
    }
    
    ~TrackedObject() {
        std::cout << "~TrackedObject() id=" << id << std::endl;
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
    
    // Register a function that takes by value
    eng->add_function("take_by_value", [](TrackedObject obj) -> int {
        std::cout << "=== INSIDE take_by_value function ===\n";
        std::cout << "Function parameter obj.id = " << obj.id << "\n";
        return obj.get();
    });
        
    TrackedObject::reset();
    
    try {
        std::cout << "=== Step 1: Object creation ===\n";
        eng->execute("auto obj = TrackedObject(42);");
        std::cout << "Copy count after creation: " << TrackedObject::copy_count << "\n\n";
        
        std::cout << "=== Step 2: Function call ===\n";
        auto result = eng->execute("take_by_value(obj)");
        std::cout << "Copy count after function call: " << TrackedObject::copy_count << "\n";
        std::cout << "Result: " << result.as<int>() << "\n\n";
        
        std::cout << "=== Step 3: Just accessing obj to see if it's still valid ===\n";
        auto objValue = eng->execute("obj.value");
        std::cout << "obj.value = " << objValue.as<int>() << "\n";
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    
    return 0;
}