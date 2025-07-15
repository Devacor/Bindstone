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
    
    ~TrackedObject() {
        std::cout << "DESTROY ~TrackedObject() id=" << id << std::endl;
    }
    
    int get() const { return value; }
    
    static void reset() { copy_count = 0; next_id = 0; }
};

int TrackedObject::copy_count = 0;
int TrackedObject::next_id = 0;

// Custom converter to see what's happening
namespace jai {
namespace detail {
    // Specialization for TrackedObject to trace conversions
    template<>
    struct value_converter<TrackedObject> {
        static TrackedObject from(const script_value& v, engine* eng) {
            std::cout << "\n--- value_converter<TrackedObject>::from called ---" << std::endl;
            std::cout << "Converting script_value to TrackedObject" << std::endl;
            
            // Check if it's a class instance
            if (v.is_class_instance()) {
                std::cout << "script_value is a class_instance" << std::endl;
                auto ptr = v.as<std::shared_ptr<TrackedObject>>();
                std::cout << "Got shared_ptr to TrackedObject id=" << ptr->id << std::endl;
                std::cout << "About to return *ptr (will copy)" << std::endl;
                return *ptr;  // COPY HERE
            } else {
                throw runtime_error("Cannot convert non-class-instance to TrackedObject");
            }
        }
        
        static script_value to(const TrackedObject& t, engine* eng) {
            std::cout << "\n--- value_converter<TrackedObject>::to called ---" << std::endl;
            std::cout << "Converting TrackedObject id=" << t.id << " to script_value" << std::endl;
            
            if (!eng) {
                throw runtime_error("Engine reference required for custom type conversion");
            }
            
            // Use conversion registry
            auto registry = eng->get_conversion_registry();
            if (!registry) {
                throw runtime_error("No conversion registry available");
            }
            
            // Try to convert using the registry
            script_value result = registry->convert_to_script<TrackedObject>(t, eng->weak_from_this());
            std::cout << "Conversion complete" << std::endl;
            return result;
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
        
    // Function that takes by value
    eng->add_function("take_by_value", [](TrackedObject obj) -> int {
        std::cout << "\nInside take_by_value: obj.id=" << obj.id << std::endl;
        return obj.get();
    });
    
    TrackedObject::reset();
    
    try {
        std::cout << "=== Creating object in script ===" << std::endl;
        eng->execute("auto obj = TrackedObject(42);");
        std::cout << "\nAfter creation: " << TrackedObject::copy_count << " copies\n" << std::endl;
        
        std::cout << "=== Calling function with by-value parameter ===" << std::endl;
        auto result = eng->execute("take_by_value(obj)");
        std::cout << "\nAfter function call: " << TrackedObject::copy_count << " copies" << std::endl;
        std::cout << "Result: " << result.as<int>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
    }
    
    return 0;
}