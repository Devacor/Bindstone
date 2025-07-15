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
    
    // Explicitly set interpreter backend
    eng->set_backend(backend_type::interpreter);
    std::cout << "Forced interpreter backend\n";
    
    class_builder<TrackedObject>(*eng, "TrackedObject")
        .constructor<int>()
        .method("getValue", &TrackedObject::getValue)
        .property("value", &TrackedObject::value)
        .build();
        
    TrackedObject::reset();
    
    try {
        std::cout << "=== Creating object and map ===\n";
        eng->execute("auto obj = TrackedObject(42);");
        eng->execute("auto map = {};");
        std::cout << "Copy count after creation: " << TrackedObject::copy_count << "\n\n";
        
        std::cout << "=== Testing assignment (should see DEBUG output) ===\n";
        eng->execute("map[\"key\"] = obj;");
        std::cout << "Copy count after assignment: " << TrackedObject::copy_count << "\n\n";
        
        std::cout << "=== Testing retrieval ===\n";
        auto result = eng->execute("map[\"key\"]");
        std::cout << "Retrieved type: " << static_cast<int>(result.type()) << "\n";
        
        if (result.is_object()) {
            auto value = eng->execute("map[\"key\"].value");
            std::cout << "Retrieved value: " << value.as<int>() << "\n";
        } else {
            std::cout << "ERROR: Retrieved value is not an object!\n";
        }
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    
    return 0;
}