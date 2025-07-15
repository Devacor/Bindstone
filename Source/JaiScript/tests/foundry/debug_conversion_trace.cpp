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
    }
    
    TrackedObject(TrackedObject&& other) : id(other.id), value(other.value) {
        std::cout << "MOVE TrackedObject id=" << id << std::endl;
        other.id = -1;  // Mark as moved
    }
    
    TrackedObject& operator=(const TrackedObject& other) {
        std::cout << "COPY ASSIGN from id=" << other.id << " to id=" << id << std::endl;
        if (this != &other) {
            value = other.value;
            // Note: we don't change id in assignment
        }
        return *this;
    }
    
    TrackedObject& operator=(TrackedObject&& other) {
        std::cout << "MOVE ASSIGN from id=" << other.id << " to id=" << id << std::endl;
        if (this != &other) {
            value = other.value;
            int old_id = id;
            id = other.id;
            other.id = old_id;
        }
        return *this;
    }
    
    ~TrackedObject() {
        if (id != -1) {  // Don't log moved-from objects
            std::cout << "DESTROY ~TrackedObject() id=" << id << std::endl;
        }
    }
    
    int get() const { return value; }
    
    static void reset() { copy_count = 0; next_id = 0; }
};

int TrackedObject::copy_count = 0;
int TrackedObject::next_id = 0;

// Custom converter that logs
namespace jai {
namespace conversions {
    template<>
    struct custom_converter<TrackedObject> {
        static TrackedObject from_script(const script_value& v) {
            std::cout << "\n--- custom_converter<TrackedObject>::from_script called ---" << std::endl;
            auto ptr = v.as<std::shared_ptr<TrackedObject>>();
            std::cout << "Got shared_ptr to TrackedObject id=" << ptr->id << std::endl;
            std::cout << "About to return by value (will trigger copy)" << std::endl;
            return *ptr;  // This will copy!
        }
        
        static script_value to_script(const TrackedObject& obj, std::weak_ptr<engine> eng) {
            std::cout << "\n--- custom_converter<TrackedObject>::to_script called ---" << std::endl;
            std::cout << "Converting TrackedObject id=" << obj.id << " to script_value" << std::endl;
            auto ptr = std::make_shared<TrackedObject>(obj);  // This will copy!
            std::cout << "Created shared_ptr with new TrackedObject id=" << ptr->id << std::endl;
            return script_value::make_class_instance("TrackedObject", ptr, eng);
        }
    };
}
}

int main() {
    auto eng = engine::make();
    eng->set_backend(backend_type::interpreter);
    
    class_builder<TrackedObject>(*eng, "TrackedObject")
        .constructor<int>()
        .method("get", &TrackedObject::get)
        .property("value", &TrackedObject::value)
        .build();
        
    // Register the custom converter
    eng->add_custom_conversion<TrackedObject>(
        [](const script_value& v) -> TrackedObject {
            return conversions::custom_converter<TrackedObject>::from_script(v);
        },
        [eng_ptr = eng.get()](const TrackedObject& obj) -> script_value {
            return conversions::custom_converter<TrackedObject>::to_script(obj, eng_ptr->weak_from_this());
        }
    );
    
    // Function that takes by value
    eng->add_function("take_by_value", [](TrackedObject obj) -> int {
        std::cout << "\nInside take_by_value: obj.id=" << obj.id << std::endl;
        return obj.get();
    });
    
    TrackedObject::reset();
    
    try {
        std::cout << "=== Starting test ===" << std::endl;
        
        // Create object
        eng->execute("auto obj = TrackedObject(42);");
        std::cout << "\nAfter creation: " << TrackedObject::copy_count << " copies\n" << std::endl;
        
        // Call function with by-value parameter
        std::cout << "=== About to call take_by_value ===" << std::endl;
        auto result = eng->execute("take_by_value(obj)");
        std::cout << "\nAfter function call: " << TrackedObject::copy_count << " copies" << std::endl;
        std::cout << "Result: " << result.as<int>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
    }
    
    return 0;
}