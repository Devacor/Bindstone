#pragma once

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <memory>

namespace jai::foundry::tests {

// Test class that tracks all copy/move operations
class TrackedObject {
public:
    static int instance_count;
    static int copy_count;
    static int move_count;
    static bool verbose;
    
    int id;
    int value;
    
    TrackedObject(int val) : id(++instance_count), value(val) {
        if (verbose) std::cout << "  TrackedObject(" << val << ") -> id=" << id << "\n";
    }
    
    TrackedObject(const TrackedObject& other) : id(++instance_count), value(other.value) {
        copy_count++;
        if (verbose) std::cout << "  COPY from id=" << other.id << " to id=" << id << "\n";
    }
    
    TrackedObject(TrackedObject&& other) noexcept : id(++instance_count), value(other.value) {
        move_count++;
        if (verbose) std::cout << "  MOVE from id=" << other.id << " to id=" << id << "\n";
    }
    
    ~TrackedObject() {
        if (verbose) std::cout << "  ~TrackedObject() id=" << id << "\n";
    }
    
    void modify(int new_val) { value = new_val; }
    int get() const { return value; }
    
    static void reset() {
        instance_count = 0;
        copy_count = 0;
        move_count = 0;
    }
};

int TrackedObject::instance_count = 0;
int TrackedObject::copy_count = 0;
int TrackedObject::move_count = 0;
bool TrackedObject::verbose = false;

class value_semantics_tests : public suite {
public:
    value_semantics_tests() : suite("Value Semantics") {}
    
    void pre_test() override {
        // Reset TrackedObject counters before each test
        TrackedObject::reset();
        TrackedObject::verbose = false;  // Ensure verbose is off by default
    }
    
    void forge_tests() override {
        test("variable_declaration_copies", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            eng->execute("auto obj = TrackedObject(42);");
            
            // Should have 1 copy: from constructor result to variable
            check_eq(TrackedObject::copy_count, 1);
        });
        
        test("variable_access_no_copy", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            eng->execute("auto obj = TrackedObject(42); obj;");
            
            // Still only 1 copy, accessing variable shouldn't copy
            check_eq(TrackedObject::copy_count, 1);
        });
        
        test("property_access_no_copy", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            auto result = eng->execute("auto obj = TrackedObject(42); obj.value");
            
            check_eq(TrackedObject::copy_count, 1);
            check_eq(result.as<int>(), 42);
        });
        
        test("method_call_no_copy", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            auto result = eng->execute("auto obj = TrackedObject(42); obj.get()");
            
            check_eq(TrackedObject::copy_count, 1);
            check_eq(result.as<int>(), 42);
        });
        
        test("assignment_copies", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            eng->execute("auto obj1 = TrackedObject(42); auto obj2 = obj1;");
            
            // Should have 2 copies: initial + assignment
            check_eq(TrackedObject::copy_count, 2);
        });
        
        test("deep_copy_verification", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            eng->execute("auto obj1 = TrackedObject(42); auto obj2 = obj1;");
            eng->execute("obj1.modify(99);");
            
            auto result = eng->execute("obj2.get()");
            
            // obj2 should still have 42, not 99
            check_eq(result.as<int>(), 42);
            check_eq(TrackedObject::copy_count, 2);
        });
        
        test("function_by_value_copies", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            // Register a function that takes by value
            eng->add_function("take_by_value", [](TrackedObject obj) -> int {
                return obj.get();
            });
            
            auto result = eng->execute("auto obj = TrackedObject(42); take_by_value(obj)");
            
            // Should have 2 copies: initial + parameter
            check_eq(TrackedObject::copy_count, 2);
            check_eq(result.as<int>(), 42);
        });
        
        test("function_by_reference_no_copy", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            // Register a function that takes by reference
            eng->add_function("take_by_ref", [](TrackedObject& obj) -> int {
                return obj.get();
            });
            
            auto result = eng->execute("auto obj = TrackedObject(42); take_by_ref(obj)");
            
            // Should have only 1 copy: initial
            check_eq(TrackedObject::copy_count, 1);
            check_eq(result.as<int>(), 42);
        });
        
        test("array_push_copies", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            eng->execute("auto obj = TrackedObject(42); auto arr = []; arr.push(obj);");
            
            // Should have 2 copies: initial + array push
            check_eq(TrackedObject::copy_count, 2);
        });
        
        test("array_assignment_copies", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            eng->execute("auto obj = TrackedObject(42); auto arr = [null]; arr[0] = obj;");
            
            // Should have 2 copies: initial + array assignment
            check_eq(TrackedObject::copy_count, 2);
        });
        
        test("map_assignment_copies", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            TrackedObject::verbose = true;
            eng->execute("auto obj = TrackedObject(42);");
            
            eng->execute("auto map = {};");
            
            eng->execute("map[\"key\"] = obj;");
            
            // Verify the value is in the map
            auto result = eng->execute("map[\"key\"].value");
            
            TrackedObject::verbose = false;
            
            // Should have 2 copies: initial + map assignment
            check_eq(TrackedObject::copy_count, 2);
        });
        
        test("lambda_capture_by_value_copies", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            auto result = eng->execute(R"(
                auto obj = TrackedObject(42);
                auto f = [obj]() { return obj.get(); };
                f()
            )");
            
            // Should have 2 copies: initial + capture
            check_eq(TrackedObject::copy_count, 2);
            check_eq(result.as<int>(), 42);
        });
        
        test("lambda_capture_by_reference_no_copy", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            auto result = eng->execute(R"(
                auto obj = TrackedObject(42);
                auto f = [&obj]() { return obj.get(); };
                f()
            )");
            
            // Should have only 1 copy: initial
            check_eq(TrackedObject::copy_count, 1);
            check_eq(result.as<int>(), 42);
        });
        
        test("lambda_default_capture_by_value", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            auto result = eng->execute(R"(
                auto obj = TrackedObject(42);
                auto f = [=]() { return obj.get(); };
                f()
            )");
            
            // Should have 2 copies: initial + capture
            check_eq(TrackedObject::copy_count, 2);
            check_eq(result.as<int>(), 42);
        });
        
        test("lambda_default_capture_by_reference", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            auto result = eng->execute(R"(
                auto obj = TrackedObject(42);
                auto f = [&]() { return obj.get(); };
                f()
            )");
            
            // Should have only 1 copy: initial
            check_eq(TrackedObject::copy_count, 1);
            check_eq(result.as<int>(), 42);
        });
        
        test("multiple_assignments", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            eng->execute("auto obj1 = TrackedObject(42); auto obj2 = obj1; auto obj3 = obj2;");
            
            // Should have 3 copies: initial + 2 assignments
            check_eq(TrackedObject::copy_count, 3);
        });
        
        test("compound_assignment_on_property", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            eng->execute("auto obj = TrackedObject(42); obj.value += 10;");
            
            // Should have only 1 copy: initial
            check_eq(TrackedObject::copy_count, 1);
            
            auto result = eng->execute("obj.value");
            check_eq(result.as<int>(), 52);
        });
        
        test("temporary_object_no_copy", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            auto result = eng->execute("TrackedObject(42).value");
            
            // Should have 0 copies: temporary object
            check_eq(TrackedObject::copy_count, 0);
            check_eq(result.as<int>(), 42);
        });
        
        test("method_chaining_no_copy", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            eng->execute("auto obj = TrackedObject(42); obj.modify(10); obj.modify(20);");
            auto result = eng->execute("obj.get()");
            
            // Should have only 1 copy: initial
            check_eq(TrackedObject::copy_count, 1);
            check_eq(result.as<int>(), 20);
        });
        
        test("array_literal_copies", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            eng->execute("auto obj = TrackedObject(42); auto arr = [obj, obj, obj];");
            
            // Should have 4 copies: initial + 3 in array
            check_eq(TrackedObject::copy_count, 4);
        });
        
        test("reference_parameter_preserves_modifications", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            // Function that modifies by reference
            eng->add_function("modify_by_ref", [](TrackedObject& obj, int val) {
                obj.modify(val);
            });
            
            eng->execute("auto obj = TrackedObject(42); modify_by_ref(obj, 99);");
            auto result = eng->execute("obj.get()");
            
            // Should have only 1 copy and value should be modified
            check_eq(TrackedObject::copy_count, 1);
            check_eq(result.as<int>(), 99);
        });
        
        test("method_call_on_cpp_object_property", [this]() {
            auto eng = engine::make();
            register_tracked_object(*eng);
            
            // Create a C++ class that contains a TrackedObject
            class ObjectContainer {
            public:
                std::shared_ptr<TrackedObject> obj;
                
                ObjectContainer() : obj(std::make_shared<TrackedObject>(100)) {
                    if (TrackedObject::verbose) {
                        std::cout << "ObjectContainer created, copy_count=" << TrackedObject::copy_count << std::endl;
                    }
                }
                
                std::shared_ptr<TrackedObject> get_object() { return obj; }
            };
            
            dynamic_binder<ObjectContainer>(*eng, "ObjectContainer")
                .constructor<>()
                .method("get_object", &ObjectContainer::get_object)
                .build();
            
            eng->execute("auto container = ObjectContainer();");
            auto result = eng->execute("container.get_object().value");
            
            // The shared_ptr object is created in C++, no script copies
            if (TrackedObject::copy_count != 0) {
                std::cout << "\nmethod_call_on_cpp_object_property FAILURE:" << std::endl;
                std::cout << "  Expected copy_count: 0" << std::endl;
                std::cout << "  Actual copy_count: " << TrackedObject::copy_count << std::endl;
                std::cout << "  instance_count: " << TrackedObject::instance_count << std::endl;
                std::cout << "  move_count: " << TrackedObject::move_count << std::endl;
            }
            check_eq(TrackedObject::copy_count, 0);
            check_eq(result.as<int>(), 100);
        });
    }
    
private:
    void register_tracked_object(engine& eng) {
        dynamic_binder<TrackedObject>(eng, "TrackedObject")
            .constructor<int>()
            .method("modify", &TrackedObject::modify)
            .method("get", &TrackedObject::get)
            .property("value", &TrackedObject::value)
            .property("id", &TrackedObject::id)
            .build();
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::value_semantics_tests)