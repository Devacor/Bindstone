#include <iostream>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>

// Test class that tracks all copy/move operations
class TrackedObject {
public:
    static int copy_count;
    
    int id;
    int value;
    
    TrackedObject(int val) : id(1), value(val) {
        std::cout << "  TrackedObject(" << val << ") -> id=" << id << "\n";
    }
    
    TrackedObject(const TrackedObject& other) : id(2), value(other.value) {
        copy_count++;
        std::cout << "  COPY from id=" << other.id << " to id=" << id << "\n";
    }
    
    ~TrackedObject() {
        std::cout << "  ~TrackedObject() id=" << id << "\n";
    }
    
    void modify(int new_val) { value = new_val; }
    int get() const { return value; }
    
    static void reset() {
        copy_count = 0;
    }
};

int TrackedObject::copy_count = 0;

int main() {
    jai::engine eng;
    
    jai::make_class_builder<TrackedObject>(eng, "TrackedObject")
        .constructor<int>()
        .method("modify", &TrackedObject::modify)
        .method("get", &TrackedObject::get)
        .property("value", &TrackedObject::value)
        .property("id", &TrackedObject::id)
        .build();
    
    TrackedObject::reset();
    
    std::cout << "\n=== Creating object ===\n";
    eng.execute("auto obj = TrackedObject(42);");
    std::cout << "After creating obj, copy_count=" << TrackedObject::copy_count << "\n";
    
    std::cout << "\n=== Creating map ===\n";
    eng.execute("auto map = {};");
    std::cout << "After creating map, copy_count=" << TrackedObject::copy_count << "\n";
    
    std::cout << "\n=== Map assignment ===\n";
    try {
        eng.execute("map[\"key\"] = obj;");
        std::cout << "After map assignment, copy_count=" << TrackedObject::copy_count << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error during assignment: " << e.what() << "\n";
    }
    
    std::cout << "\n=== Reading from map ===\n";
    try {
        auto result = eng.execute("map[\"key\"]");
        std::cout << "map[\"key\"] type: " << static_cast<int>(result.type()) << "\n";
        if (!result.is_null()) {
            auto val_result = eng.execute("map[\"key\"].value");
            std::cout << "map[\"key\"].value = " << val_result.as<int>() << "\n";
        } else {
            std::cout << "map[\"key\"] is null!\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading from map: " << e.what() << "\n";
    }
    
    std::cout << "\n=== Final copy count: " << TrackedObject::copy_count << " ===\n";
    
    return 0;
}