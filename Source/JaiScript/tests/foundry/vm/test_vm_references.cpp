#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/types.hpp>
#include <jaiscript/jvm/virtual_machine.hpp>
#include <iostream>

using namespace jai;
using namespace jai::foundry;

class test_vm_references : public suite {
public:
    test_vm_references() : suite("VM References") {}
    
    void forge_tests() override {
        test("map_subscript_assignment", [this]() {
            auto vm_engine = engine::make();
            vm_engine->set_backend(backend_type::jvm);
            
            // Test that map subscript creates references
            vm_engine->execute(R"(
                auto m = {"key": 42};
            )");
            
            // This should modify the original map
            vm_engine->execute(R"(
                m["key"] = 100;
            )");
            
            auto result = vm_engine->execute("m[\"key\"]");
            check_eq(result.as<int>(), 100, "Map subscript assignment should persist");
        });
        
        test("array_method_mutation", [this]() {
            auto vm_engine = engine::make();
            vm_engine->set_backend(backend_type::jvm);
            
            vm_engine->execute("auto arr = [1, 2, 3];");
            check_eq(vm_engine->execute("arr.size()").as<int>(), 3, "Initial array size");
            
            // This should modify the original array
            vm_engine->execute("arr.push(4);");
            
            check_eq(vm_engine->execute("arr.size()").as<int>(), 4, "Array push should persist");
            check_eq(vm_engine->execute("arr[3]").as<int>(), 4, "Pushed element should be accessible");
        });
        
        test("map_method_mutation", [this]() {
            auto vm_engine = engine::make();
            vm_engine->set_backend(backend_type::jvm);
            
            vm_engine->execute(R"(
                auto m = {"a": 1, "b": 2};
            )");
            check_eq(vm_engine->execute("m.size()").as<int>(), 2, "Initial map size");
            
            // This should modify the original map
            vm_engine->execute(R"(m.erase("a");)");
            
            check_eq(vm_engine->execute("m.size()").as<int>(), 1, "Map erase should persist");
            check_eq(vm_engine->execute(R"(m.contains("a"))").as<bool>(), false, "Erased key should not exist");
        });
        
        test("nested_container_references", [this]() {
            auto vm_engine = engine::make();
            vm_engine->set_backend(backend_type::jvm);
            
            // Create nested structure
            vm_engine->execute(R"(
                auto data = {
                    "users": [
                        {"name": "Alice", "age": 30},
                        {"name": "Bob", "age": 25}
                    ]
                };
            )");
            
            // Modify through nested references
            vm_engine->execute(R"(
                data["users"][0]["age"] = 31;
            )");
            
            auto age = vm_engine->execute(R"(data["users"][0]["age"])");
            check_eq(age.as<int>(), 31, "Nested reference modification should persist");
        });
        
        test("reference_comparison_with_interpreter", [this]() {
            // Test same code in both backends
            const char* code = R"(
                auto m = {"count": 0};
                m["count"] = m["count"] + 1;
                m["count"];
            )";
            
            auto interp_engine = engine::make();
            interp_engine->set_backend(backend_type::interpreter);
            auto interp_result = interp_engine->execute(code);
            
            auto vm_engine = engine::make();
            vm_engine->set_backend(backend_type::jvm);
            auto vm_result = vm_engine->execute(code);
            
            check_eq(vm_result.as<int>(), interp_result.as<int>(), 
                    "VM and interpreter should produce same result");
        });
    }
};

REGISTER_FOUNDRY_TESTS(test_vm_references)