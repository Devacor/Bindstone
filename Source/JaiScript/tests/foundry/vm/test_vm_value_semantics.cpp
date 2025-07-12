#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/core/types.hpp>

using namespace jai;
using namespace jai::foundry;

// Test class to track object identity and modifications
class TestObject {
public:
    static int instance_counter;
    int id;
    int value;
    
    TestObject(int val = 0) : id(++instance_counter), value(val) {}
    TestObject(const TestObject& other) : id(++instance_counter), value(other.value) {}
    
    void setValue(int v) { value = v; }
    int getValue() const { return value; }
    int getId() const { return id; }
};

int TestObject::instance_counter = 0;

class vm_value_semantics_tests : public suite {
public:
    vm_value_semantics_tests() : suite("VM Value Semantics Tests") {}
    
    void forge_tests() override {
        test("map_deep_copy_primitive_values", [this]() {
            auto vm_eng = engine::make();
            vm_eng->set_backend(backend_type::jvm);
            
            auto result = vm_eng->execute(R"(
                var m1 = {"key": 42};
                var m2 = m1;  // Copy the map
                m2["key"] = 99;  // Modify the copy
                m1["key"]  // Should be 42 if deep copy
            )");
            
            check_eq(result.as<script_int>(), script_int(42), "Map should be deep copied");
        });
        
        test("map_deep_copy_nested_maps", [this]() {
            auto vm_eng = engine::make();
            vm_eng->set_backend(backend_type::jvm);
            
            auto result = vm_eng->execute(R"(
                var m1 = {"outer": {"inner": 42}};
                var m2 = m1;  // Copy the map
                m2["outer"]["inner"] = 99;  // Modify nested value
                m1["outer"]["inner"]  // Should be 42 if deep copy
            )");
            
            check_eq(result.as<script_int>(), script_int(42), "Nested maps should be deep copied");
        });
        
        test("map_deep_copy_with_objects", [this]() {
            auto vm_eng = engine::make();
            vm_eng->set_backend(backend_type::jvm);
            
            // Register TestObject
            class_builder<TestObject>(vm_eng, "TestObject")
                .constructor<int>()
                .method("setValue", &TestObject::setValue)
                .method("getValue", &TestObject::getValue)
                .method("getId", &TestObject::getId)
                .build();
            
            TestObject::instance_counter = 0;
            
            auto result = vm_eng->execute(R"(
                var m1 = {"obj": TestObject(100)};
                var original_id = m1["obj"].getId();
                
                var m2 = m1;  // Copy the map
                var copy_id = m2["obj"].getId();
                
                m2["obj"].setValue(200);  // Modify object in the copy
                
                // Return: [original_value, modified_value, ids_different]
                [m1["obj"].getValue(), m2["obj"].getValue(), original_id != copy_id]
            )");
            
            auto arr = result.as_array();
            check_eq(arr[0].as<script_int>(), script_int(100), "Original object should not be modified");
            check_eq(arr[1].as<script_int>(), script_int(200), "Copy object should be modified");
            check(arr[2].as<script_bool>(), "Object IDs should be different (deep copy)");
        });
        
        test("map_with_array_of_objects", [this]() {
            auto vm_eng = engine::make();
            vm_eng->set_backend(backend_type::jvm);
            
            // Register TestObject
            class_builder<TestObject>(vm_eng, "TestObject")
                .constructor<int>()
                .method("setValue", &TestObject::setValue)
                .method("getValue", &TestObject::getValue)
                .build();
            
            TestObject::instance_counter = 0;
            
            auto result = vm_eng->execute(R"(
                var m1 = {"arr": [TestObject(300), TestObject(400)]};
                var m2 = m1;  // Copy the map
                
                m2["arr"][0].setValue(500);  // Modify first object in array in copy
                
                // Return original values
                [m1["arr"][0].getValue(), m1["arr"][1].getValue()]
            )");
            
            auto arr = result.as_array();
            check_eq(arr[0].as<script_int>(), script_int(300), "Original array objects should not be modified");
            check_eq(arr[1].as<script_int>(), script_int(400), "Original array objects should not be modified");
        });
        
        test("compare_vm_and_interpreter_map_semantics", [this]() {
            // Test interpreter
            auto interp_eng = engine::make();
            interp_eng->set_backend(backend_type::interpreter);
            
            auto interp_result = interp_eng->execute(R"(
                var m1 = {"a": 1, "b": {"c": 2}};
                var m2 = m1;
                m2["a"] = 10;
                m2["b"]["c"] = 20;
                [m1["a"], m1["b"]["c"]]
            )");
            
            // Test VM
            auto vm_eng = engine::make();
            vm_eng->set_backend(backend_type::jvm);
            
            auto vm_result = vm_eng->execute(R"(
                var m1 = {"a": 1, "b": {"c": 2}};
                var m2 = m1;
                m2["a"] = 10;
                m2["b"]["c"] = 20;
                [m1["a"], m1["b"]["c"]]
            )");
            
            auto interp_arr = interp_result.as_array();
            auto vm_arr = vm_result.as_array();
            
            check_eq(vm_arr[0].as<script_int>(), interp_arr[0].as<script_int>(), 
                     "VM should match interpreter for map copy semantics");
            check_eq(vm_arr[1].as<script_int>(), interp_arr[1].as<script_int>(), 
                     "VM should match interpreter for nested map copy semantics");
        });
        
        test("array_deep_copy_with_objects", [this]() {
            auto vm_eng = engine::make();
            vm_eng->set_backend(backend_type::jvm);
            
            // Register TestObject
            class_builder<TestObject>(vm_eng, "TestObject")
                .constructor<int>()
                .method("setValue", &TestObject::setValue)
                .method("getValue", &TestObject::getValue)
                .build();
            
            TestObject::instance_counter = 0;
            
            auto result = vm_eng->execute(R"(
                var arr1 = [TestObject(100), TestObject(200)];
                var arr2 = arr1;  // Copy the array
                
                arr2[0].setValue(150);  // Modify object in copy
                
                // Return original values
                [arr1[0].getValue(), arr1[1].getValue()]
            )");
            
            auto arr = result.as_array();
            check_eq(arr[0].as<script_int>(), script_int(100), "Original array objects should not be modified");
            check_eq(arr[1].as<script_int>(), script_int(200), "Original array objects should not be modified");
        });
    }
};

// Enable individual test execution
CONDITIONAL_ISOLATED_TEST(vm_value_semantics_tests)