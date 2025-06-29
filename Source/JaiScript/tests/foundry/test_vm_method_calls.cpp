#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <iostream>

using namespace jai;
using namespace jai::foundry;

class TestClass {
public:
    TestClass(int v) : value(v) {}
    int getValue() const { return value; }
    void setValue(int v) { value = v; }
    int add(int x) { return value + x; }
    
    int value;
};

class vm_method_call_tests : public suite {
public:
    vm_method_call_tests() : suite("VM Method Call Tests") {}
    
    void forge_tests() override {
        test("array_builtin_methods", [this]() {
            engine eng;
            eng.set_backend(backend_type::jvm);
            
            // Test array methods
            auto result = eng.execute(R"(
                auto arr = [1, 2, 3];
                arr.size()
            )");
            check_eq(result.as<int>(), 3);
            
            result = eng.execute(R"(
                auto arr = [1, 2, 3];
                arr.push(4);
                arr.size()
            )");
            check_eq(result.as<int>(), 4);
            
            result = eng.execute(R"(
                auto arr = [1, 2, 3];
                arr.empty()
            )");
            check_eq(result.as<bool>(), false);
        });
        
        test("map_builtin_methods", [this]() {
            engine eng;
            eng.set_backend(backend_type::jvm);
            
            // Test map methods
            auto result = eng.execute(R"(
                auto m = {"a": 1, "b": 2};
                m.size()
            )");
            check_eq(result.as<int>(), 2);
            
            result = eng.execute(R"(
                auto m = {"a": 1, "b": 2};
                m.empty()
            )");
            check_eq(result.as<bool>(), false);
            
            result = eng.execute(R"(
                auto m = {"a": 1};
                m.contains("a")
            )");
            check_eq(result.as<bool>(), true);
        });
        
        test("cpp_class_methods", [this]() {
            engine eng;
            eng.set_backend(backend_type::jvm);
            
            // Register TestClass
            make_class_builder<TestClass>(eng, "TestClass")
                .constructor<int>()
                .method("getValue", &TestClass::getValue)
                .method("setValue", &TestClass::setValue)
                .method("add", &TestClass::add)
                .property("value", &TestClass::value)
                .build();
            
            // Test method calls
            auto result = eng.execute(R"(
                auto obj = TestClass(42);
                obj.getValue()
            )");
            check_eq(result.as<int>(), 42);
            
            result = eng.execute(R"(
                auto obj = TestClass(10);
                obj.add(5)
            )");
            check_eq(result.as<int>(), 15);
            
            // Test property access through getter
            result = eng.execute(R"(
                auto obj = TestClass(100);
                obj.value
            )");
            check_eq(result.as<int>(), 100);
        });
        
        test("method_call_with_arguments", [this]() {
            engine eng;
            eng.set_backend(backend_type::jvm);
            
            auto result = eng.execute(R"(
                auto arr = [1, 2, 3];
                arr.push(4);
                arr.push(5);
                arr.size()
            )");
            check_eq(result.as<int>(), 5);
        });
        
        test("chained_method_calls", [this]() {
            engine eng;
            eng.set_backend(backend_type::jvm);
            
            // Test we can chain method calls eventually
            // For now, test sequential calls work
            auto result = eng.execute(R"(
                auto m = {"a": 1};
                m.keys().size()
            )");
            check_eq(result.as<int>(), 1);
        });
        
        test("compare_vm_interpreter_methods", [this]() {
            // Test that VM and interpreter produce same results
            const char* scripts[] = {
                // Array methods
                R"(
                    auto arr = [1, 2, 3];
                    arr.size()
                )",
                R"(
                    auto arr = [];
                    arr.empty()
                )",
                R"(
                    auto arr = [10, 20];
                    arr.front()
                )",
                // Map methods
                R"(
                    auto m = {"x": 1, "y": 2};
                    m.size()
                )",
                R"(
                    auto m = {"test": true};
                    m.contains("test")
                )",
            };
            
            for (const char* script : scripts) {
                engine eng_interp;
                engine eng_vm;
                eng_vm.set_backend(backend_type::jvm);
                
                auto result_interp = eng_interp.execute(script);
                auto result_vm = eng_vm.execute(script);
                
                check_eq(result_vm.type(), result_interp.type());
                if (result_vm.is_int()) {
                    check_eq(result_vm.as<int>(), result_interp.as<int>());
                } else if (result_vm.is_bool()) {
                    check_eq(result_vm.as<bool>(), result_interp.as<bool>());
                }
            }
        });
    }
};

REGISTER_FOUNDRY_TESTS(vm_method_call_tests)