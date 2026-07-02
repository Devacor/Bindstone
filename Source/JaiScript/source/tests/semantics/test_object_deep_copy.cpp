#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <sstream>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {
namespace object_deep_copy_ns {

class test_object {
public:
    int value;
    std::string name;
    
    test_object() : value(0), name("default") {}
    test_object(int v, const std::string& n) : value(v), name(n) {}
    
    // Make sure it's copyable
    test_object(const test_object& other) = default;
    test_object& operator=(const test_object& other) = default;
    
    int get_value() const { return value; }
    void set_value(int v) { value = v; }
    std::string get_name() const { return name; }
    void set_name(const std::string& n) { name = n; }
};

class derived_object : public test_object {
public:
    double extra;
    
    derived_object() : test_object(), extra(0.0) {}
    derived_object(int v, const std::string& n, double e) : test_object(v, n), extra(e) {}
    
    double get_extra() const { return extra; }
    void set_extra(double e) { extra = e; }
};

class object_deep_copy_tests : public suite {
public:
    object_deep_copy_tests() : suite("object deep copy tests") {}
    
    void forge_tests() override {
        test("basic object deep copy", [this]() {
            auto eng = make_engine();
            
            dynamic_binder<test_object>(*eng, "test_object")
                .constructor<int, script_string>()
                .method("get_value", &test_object::get_value)
                .method("set_value", &test_object::set_value)
                .method("get_name", &test_object::get_name)
                .method("set_name", &test_object::set_name)
                .property("value", &test_object::value)
                .property("name", &test_object::name)
                .build();

            eng->execute(R"(
                var obj1 = test_object(42, "original");
                var obj2 = obj1;  // This should deep copy
                
                // Modify obj2
                obj2.set_value(100);
                obj2.set_name("modified");
            )");
            
            // Verify deep copy - obj1 should be unchanged
            auto obj1_value = eng->execute("obj1.get_value()").as<int>();
            auto obj1_name = eng->execute("obj1.get_name()").as<std::string>();
            auto obj2_value = eng->execute("obj2.get_value()").as<int>();
            auto obj2_name = eng->execute("obj2.get_name()").as<std::string>();
            
            check_eq(obj1_value, 42);
            check_eq(obj1_name, "original");
            check_eq(obj2_value, 100);
            check_eq(obj2_name, "modified");
        });
        
        test("object deep copy with inheritance", [this]() {
            auto eng = make_engine();
            
            dynamic_binder<test_object>(*eng, "test_object")
                .constructor<int, script_string>()
                .method("get_value", &test_object::get_value)
                .method("set_value", &test_object::set_value)
                .method("get_name", &test_object::get_name)
                .method("set_name", &test_object::set_name)
                .build();
            
            dynamic_binder<derived_object>(*eng, "derived_object")
                .constructor<int, script_string, script_float>()
                .base_class<test_object>()
                .method("get_extra", &derived_object::get_extra)
                .method("set_extra", &derived_object::set_extra)
                .build();
            
            eng->execute(R"(
                var obj1 = derived_object(42, "original", 3.14);
                var obj2 = obj1;  // This should deep copy
                
                // Modify obj2
                obj2.set_value(100);
                obj2.set_name("modified");
                obj2.set_extra(2.71);
            )");
            
            // Verify deep copy - obj1 should be unchanged
            auto obj1_value = eng->execute("obj1.get_value()").as<int>();
            auto obj1_name = eng->execute("obj1.get_name()").as<std::string>();
            auto obj1_extra = eng->execute("obj1.get_extra()").as<double>();
            
            auto obj2_value = eng->execute("obj2.get_value()").as<int>();
            auto obj2_name = eng->execute("obj2.get_name()").as<std::string>();
            auto obj2_extra = eng->execute("obj2.get_extra()").as<double>();
            
            check_eq(obj1_value, 42);
            check_eq(obj1_name, "original");
            check_eq(obj1_extra, 3.14);
            
            check_eq(obj2_value, 100);
            check_eq(obj2_name, "modified");
            check_eq(obj2_extra, 2.71);
        });
        
        test("object deep copy in containers", [this]() {
            auto eng = make_engine();
            
            dynamic_binder<test_object>(*eng, "test_object")
                .constructor<int, script_string>()
                .method("get_value", &test_object::get_value)
                .method("set_value", &test_object::set_value)
                .method("get_name", &test_object::get_name)
                .method("set_name", &test_object::set_name)
                .build();

            eng->execute(R"(
                var obj = test_object(42, "original");
                
                // Put object in array
                var arr1 = [obj];
                var arr2 = arr1;  // Deep copy array and its contents
                
                // Modify object in arr2
                arr2[0].set_value(100);
                
                // Put object in map
                var map1 = {"key": obj};
                var map2 = map1;  // Deep copy map and its contents
                
                // Modify object in map2
                map2["key"].set_name("modified");
            )");
            
            // Verify deep copy - original objects should be unchanged
            auto obj_value = eng->execute("obj.get_value()").as<int>();
            auto obj_name = eng->execute("obj.get_name()").as<std::string>();
            
            auto arr1_obj_value = eng->execute("arr1[0].get_value()").as<int>();
            auto arr2_obj_value = eng->execute("arr2[0].get_value()").as<int>();
            
            auto map1_obj_name = eng->execute("map1[\"key\"].get_name()").as<std::string>();
            auto map2_obj_name = eng->execute("map2[\"key\"].get_name()").as<std::string>();
            
            check_eq(obj_value, 42);
            check_eq(obj_name, "original");
            check_eq(arr1_obj_value, 42);
            check_eq(arr2_obj_value, 100);
            check_eq(map1_obj_name, "original");
            check_eq(map2_obj_name, "modified");
        });
    }
};

} // namespace object_deep_copy_ns
} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::object_deep_copy_ns::object_deep_copy_tests)