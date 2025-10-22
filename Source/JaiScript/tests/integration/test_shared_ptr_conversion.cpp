#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <memory>
#include <iostream>

using namespace jai;

class TestObject {
public:
    int value;
    std::string name;
    
    TestObject() : value(0), name("default") {}
    TestObject(int v, const std::string& n) : value(v), name(n) {}
    
    int get_value() const { return value; }
    void set_value(int v) { value = v; }
    const std::string& get_name() const { return name; }
    void set_name(const std::string& n) { name = n; }
};

int main() {
    try {
        auto engine = engine::make();
        
        // Register TestObject class
        class_builder<TestObject>(*engine, "TestObject")
            .constructor<>()
            .constructor<int, const std::string&>()
            .method("getValue", &TestObject::get_value)
            .method("setValue", &TestObject::set_value)
            .method("getName", &TestObject::get_name)
            .method("setName", &TestObject::set_name)
            .property("value", &TestObject::value)
            .property("name", &TestObject::name)
            .build();
        
        // Test creating and converting bound object
        script_value obj_val = engine->execute("TestObject(42, \"test\")");
        
        // Debug: Check what type we have
        std::cout << "Object type: " << static_cast<int>(obj_val.type()) << std::endl;
        std::cout << "Is object: " << obj_val.is_object() << std::endl;
        
        try {
            auto obj_ptr = obj_val.as<std::shared_ptr<TestObject>>();
            std::cout << "Value: " << obj_ptr->get_value() << std::endl;
            std::cout << "Name: " << obj_ptr->get_name() << std::endl;
            std::cout << "SUCCESS: shared_ptr conversion works!" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "FAILED: " << e.what() << std::endl;
            return 1;
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}