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
    
    int getValue() const { return value; }
    
    static void reset() { copy_count = 0; next_id = 0; }
};

int TrackedObject::copy_count = 0;
int TrackedObject::next_id = 0;

int main() {
    auto eng = engine::make();
    eng->set_backend(backend_type::interpreter);
    
    class_builder<TrackedObject>(*eng, "TrackedObject")
        .constructor<int>()
        .method("getValue", &TrackedObject::getValue)
        .property("value", &TrackedObject::value)
        .build();
        
    TrackedObject::reset();
    
    try {
        std::cout << "=== Step 1: Create object ===\n";
        eng->execute("auto obj = TrackedObject(42);");
        std::cout << "Copy count after object creation: " << TrackedObject::copy_count << "\n\n";
        
        std::cout << "=== Step 2: Create map ===\n";
        eng->execute("auto myMap = {};");
        std::cout << "Copy count after map creation: " << TrackedObject::copy_count << "\n\n";
        
        std::cout << "=== Step 3: Assignment (should call clone) ===\n";
        eng->execute("myMap[\"key\"] = obj;");
        std::cout << "Copy count after assignment: " << TrackedObject::copy_count << "\n\n";
        
        std::cout << "=== Step 4: Test direct access ===\n";
        auto retrieved = eng->execute("myMap[\"key\"]");
        std::cout << "Retrieved type: " << static_cast<int>(retrieved.type()) << "\n";
        
        if (retrieved.is_object()) {
            std::cout << "SUCCESS: Retrieved object!\n";
            try {
                auto value = eng->execute("myMap[\"key\"].value");
                std::cout << "Value: " << value.as<int>() << "\n";
            } catch (const std::exception& e) {
                std::cout << "ERROR accessing .value: " << e.what() << "\n";
            }
        } else {
            std::cout << "ERROR: Retrieved value is not an object (type=" << static_cast<int>(retrieved.type()) << ")\n";
        }
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    
    return 0;
}