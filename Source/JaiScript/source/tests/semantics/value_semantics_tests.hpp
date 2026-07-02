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

// Copy-count pins for the engine's designed value semantics (VM parity bar):
// - Declarations clone only lvalue initializers; temporaries (ctor results) bind with no copy.
// - Assignment into variables, array elements, map entries, and push() deep-clones (1 C++ copy
//   via the class copy_function), except shared_ptr-typed values which share.
// - Lambda captures share C++-backed objects (clone_for_capture) regardless of capture mode.
// - Calls into C++ never deep-clone args script-side; a by-value C++ param costs exactly the
//   one copy function_binder makes, a T& param costs zero.
class value_semantics_tests : public suite {
public:
    value_semantics_tests() : suite("Value Semantics") {}

    void pre_test() override {
        TrackedObject::reset();
        TrackedObject::verbose = false;
    }

    void forge_tests() override {
        test("variable_declaration_copies", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->execute("auto obj = TrackedObject(42);");

            // Constructor result is a temporary: bound without a clone
            check_eq(0, TrackedObject::copy_count);
        });

        test("variable_access_no_copy", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->execute("auto obj = TrackedObject(42); obj;");

            // Reading a variable is a shallow script_value copy - no C++ copy
            check_eq(0, TrackedObject::copy_count);
        });

        test("property_access_no_copy", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            auto result = eng->execute("auto obj = TrackedObject(42); obj.value");

            check_eq(0, TrackedObject::copy_count);
            check_eq(42, result.as<int>());
        });

        test("method_call_no_copy", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            auto result = eng->execute("auto obj = TrackedObject(42); obj.get()");

            check_eq(0, TrackedObject::copy_count);
            check_eq(42, result.as<int>());
        });

        test("assignment_copies", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->execute("auto obj1 = TrackedObject(42); auto obj2 = obj1;");

            // Initializing from an lvalue deep-clones: 1 copy
            check_eq(1, TrackedObject::copy_count);
        });

        test("deep_copy_verification", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->execute("auto obj1 = TrackedObject(42); auto obj2 = obj1;");
            eng->execute("obj1.modify(99);");

            auto result = eng->execute("obj2.get()");

            // obj2 is an independent deep copy: still 42, not 99
            check_eq(42, result.as<int>());
            check_eq(1, TrackedObject::copy_count);
        });

        test("function_by_value_copies", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->add_function("take_by_value", [](TrackedObject obj) -> int {
                return obj.get();
            });

            auto result = eng->execute("auto obj = TrackedObject(42); take_by_value(obj)");

            // Script passes the object shallow; function_binder copies once into the by-value param
            check_eq(1, TrackedObject::copy_count);
            check_eq(42, result.as<int>());
        });

        test("function_by_reference_no_copy", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->add_function("take_by_ref", [](TrackedObject& obj) -> int {
                return obj.get();
            });

            auto result = eng->execute("auto obj = TrackedObject(42); take_by_ref(obj)");

            check_eq(0, TrackedObject::copy_count);
            check_eq(42, result.as<int>());
        });

        test("array_push_copies", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->execute("auto obj = TrackedObject(42); auto arr = []; arr.push(obj);");

            // push() deep-clones the element: 1 copy
            check_eq(1, TrackedObject::copy_count);
        });

        test("array_assignment_copies", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->execute("auto obj = TrackedObject(42); auto arr = [null]; arr[0] = obj;");

            // Element assignment deep-clones: 1 copy
            check_eq(1, TrackedObject::copy_count);
        });

        test("map_assignment_copies", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->execute("auto obj = TrackedObject(42);");
            eng->execute("auto m = {};");
            eng->execute("m[\"key\"] = obj;");

            auto result = eng->execute("m[\"key\"].value");

            // Map entry assignment deep-clones: 1 copy
            check_eq(1, TrackedObject::copy_count);
            check_eq(42, result.as<int>());
        });

        test("lambda_capture_by_value_copies", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            auto result = eng->execute(R"(
                auto obj = TrackedObject(42);
                auto f = [obj]() { return obj.get(); };
                f()
            )");

            // C++-backed objects are reference types for capture: shared, not cloned
            check_eq(0, TrackedObject::copy_count);
            check_eq(42, result.as<int>());
        });

        test("lambda_capture_by_reference_no_copy", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            auto result = eng->execute(R"(
                auto obj = TrackedObject(42);
                auto f = [&obj]() { return obj.get(); };
                f()
            )");

            check_eq(0, TrackedObject::copy_count);
            check_eq(42, result.as<int>());
        });

        test("lambda_default_capture_by_value", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            auto result = eng->execute(R"(
                auto obj = TrackedObject(42);
                auto f = [=]() { return obj.get(); };
                f()
            )");

            // Same sharing rule as explicit capture: C++-backed objects are not cloned
            check_eq(0, TrackedObject::copy_count);
            check_eq(42, result.as<int>());
        });

        test("lambda_default_capture_by_reference", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            auto result = eng->execute(R"(
                auto obj = TrackedObject(42);
                auto f = [&]() { return obj.get(); };
                f()
            )");

            check_eq(0, TrackedObject::copy_count);
            check_eq(42, result.as<int>());
        });

        test("multiple_assignments", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->execute("auto obj1 = TrackedObject(42); auto obj2 = obj1; auto obj3 = obj2;");

            // Two lvalue initializations, one deep clone each
            check_eq(2, TrackedObject::copy_count);
        });

        test("compound_assignment_on_property", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->execute("auto obj = TrackedObject(42); obj.value += 10;");

            // In-place mutation through the property setter - no object copy
            check_eq(0, TrackedObject::copy_count);

            auto result = eng->execute("obj.value");
            check_eq(52, result.as<int>());
        });

        test("temporary_object_no_copy", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            auto result = eng->execute("TrackedObject(42).value");

            check_eq(0, TrackedObject::copy_count);
            check_eq(42, result.as<int>());
        });

        test("method_chaining_no_copy", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->execute("auto obj = TrackedObject(42); obj.modify(10); obj.modify(20);");
            auto result = eng->execute("obj.get()");

            check_eq(0, TrackedObject::copy_count);
            check_eq(20, result.as<int>());
        });

        test("array_literal_copies", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->execute("auto obj = TrackedObject(42); auto arr = [obj, obj, obj];");

            // Array literal elements are shallow handles and the literal itself is a
            // temporary, so no element is cloned (unlike push()/element assignment)
            check_eq(0, TrackedObject::copy_count);
        });

        test("reference_parameter_preserves_modifications", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            eng->add_function("modify_by_ref", [](TrackedObject& obj, int val) {
                obj.modify(val);
            });

            eng->execute("auto obj = TrackedObject(42); modify_by_ref(obj, 99);");
            auto result = eng->execute("obj.get()");

            check_eq(0, TrackedObject::copy_count);
            check_eq(99, result.as<int>());
        });

        test("method_call_on_cpp_object_property", [this]() {
            auto eng = make_engine();
            register_tracked_object(*eng);

            class ObjectContainer {
            public:
                std::shared_ptr<TrackedObject> obj;

                ObjectContainer() : obj(std::make_shared<TrackedObject>(100)) {}

                std::shared_ptr<TrackedObject> get_object() { return obj; }
            };

            dynamic_binder<ObjectContainer>(*eng, "ObjectContainer")
                .constructor<>()
                .method("get_object", &ObjectContainer::get_object)
                .build();

            eng->execute("auto container = ObjectContainer();");
            auto result = eng->execute("container.get_object().value");

            // shared_ptr returns share - no script copies
            check_eq(0, TrackedObject::copy_count);
            check_eq(100, result.as<int>());
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

FOUNDRY_REGISTER(jai::foundry::tests::value_semantics_tests)
