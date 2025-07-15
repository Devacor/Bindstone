#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;

class SimpleObj {
public:
    int value;
    SimpleObj(int v) : value(v) {}
    int getValue() const { return value; }
};

// Helper to access private members for debugging
namespace jai {
    void debug_script_value_object(const script_value& val) {
        if (val.type() == script_value_type::jai_object_type) {
            auto obj_holder = std::get<std::shared_ptr<script_value::object_holder>>(val.storage_);
            std::cout << "Object details:\n";
            std::cout << "  type_name: " << obj_holder->type_name << "\n";
            std::cout << "  is_cpp_class_instance: " << obj_holder->is_cpp_class_instance << "\n";
            std::cout << "  data ptr: " << obj_holder->data.get() << "\n";
            std::cout << "  data use_count: " << obj_holder->data.use_count() << "\n";
        } else {
            std::cout << "Not an object type: " << static_cast<int>(val.type()) << "\n";
        }
    }
}

int main() {
    auto eng = engine::make();
    
    class_builder<SimpleObj>(*eng, "SimpleObj")
        .constructor<int>()
        .method("getValue", &SimpleObj::getValue)
        .property("value", &SimpleObj::value)
        .build();
    
    try {
        std::cout << "=== Creating object ===\n";
        eng->execute("auto obj = SimpleObj(777);");
        
        std::cout << "\n=== Checking object details ===\n";
        auto obj_value = eng->get_variable("obj");
        debug_script_value_object(obj_value);
        
        std::cout << "\n=== Testing clone ===\n";
        auto cloned = obj_value.clone();
        std::cout << "Original:\n";
        debug_script_value_object(obj_value);
        std::cout << "Cloned:\n";
        debug_script_value_object(cloned);
        
        std::cout << "\n=== Comparing data pointers ===\n";
        if (obj_value.type() == script_value_type::jai_object_type && cloned.type() == script_value_type::jai_object_type) {
            auto orig_holder = std::get<std::shared_ptr<script_value::object_holder>>(obj_value.storage_);
            auto clone_holder = std::get<std::shared_ptr<script_value::object_holder>>(cloned.storage_);
            
            std::cout << "Same object_holder: " << (orig_holder.get() == clone_holder.get()) << "\n";
            std::cout << "Same data pointer: " << (orig_holder->data.get() == clone_holder->data.get()) << "\n";
        }
        
        std::cout << "\n=== Testing map assignment ===\n";
        eng->execute("auto map = {};");
        auto map_before = eng->get_variable("map");
        std::cout << "Map before assignment - type: " << static_cast<int>(map_before.type()) << "\n";
        
        eng->execute("map[\"key\"] = obj;");
        auto map_after = eng->get_variable("map");
        std::cout << "Map after assignment - type: " << static_cast<int>(map_after.type()) << "\n";
        
        if (map_after.is_map()) {
            std::cout << "Map is still valid after assignment\n";
            try {
                auto size = eng->execute("map.size()");
                std::cout << "Map size: " << size.as<int>() << "\n";
            } catch (const std::exception& e) {
                std::cout << "Map size error: " << e.what() << "\n";
            }
        } else {
            std::cout << "Map is corrupted after assignment!\n";
        }
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    
    return 0;
}