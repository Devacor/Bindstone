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
        std::cout << "CONSTRUCT TrackedObject(" << v << ") -> id=" << id << std::endl;
    }
    
    TrackedObject(const TrackedObject& other) : id(++next_id), value(other.value) {
        copy_count++;
        std::cout << "COPY from id=" << other.id << " to id=" << id << " (count=" << copy_count << ")" << std::endl;
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
        
    TrackedObject::reset();
    
    try {
        std::cout << "=== Test 1: Just creating object ===\n";
        eng->execute("auto obj = TrackedObject(42);");
        std::cout << "After object creation: " << TrackedObject::copy_count << " copies\n\n";
        
        std::cout << "=== Test 2: Accessing object property ===\n";
        auto prop = eng->execute("obj.value");
        std::cout << "After property access: " << TrackedObject::copy_count << " copies\n";
        std::cout << "Property value: " << prop.as<int>() << "\n\n";
        
        std::cout << "=== Test 3: Calling method ===\n";
        auto method_result = eng->execute("obj.get()");
        std::cout << "After method call: " << TrackedObject::copy_count << " copies\n";
        std::cout << "Method result: " << method_result.as<int>() << "\n\n";
        
        std::cout << "=== Test 4: Manual function that takes by value ===\n";
        eng->add_function("manual_test", [](TrackedObject obj) -> int {
            std::cout << "Inside manual_test: obj.id=" << obj.id << "\n";
            return obj.get();
        });
        
        auto manual_result = eng->execute("manual_test(obj)");
        std::cout << "After manual function: " << TrackedObject::copy_count << " copies\n";
        std::cout << "Manual result: " << manual_result.as<int>() << "\n";
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    
    return 0;
}