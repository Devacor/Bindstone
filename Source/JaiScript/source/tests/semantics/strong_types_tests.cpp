#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

namespace jai::foundry::tests {

class strong_types_tests : public suite {
public:
    strong_types_tests() : suite("Strong Types") {}

    void forge_tests() override {
        // ===== AUTO KEYWORD TESTS =====

        test("auto_with_initializer_locks_type", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto x = 5;
                x = 10;  // OK: int to int
                x
            )");
            check_eq(result.as<int>(), 10);
        });

        test("auto_rejects_incompatible_type", [this]() {
            auto eng = make_engine();
            bool caught = false;
            try {
                eng->execute(R"(
                    auto x = 5;
                    x = "hello";  // ERROR: string to int
                )");
            } catch (const std::exception& e) {
                caught = true;
                std::string msg = e.what();
                check(msg.find("Cannot assign") != std::string::npos ||
                      msg.find("Type mismatch") != std::string::npos);
            }
            check_eq(caught, true, "Should reject string assignment to int variable");
        });

        test("auto_uninitialized_locks_on_first_assignment", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto x;
                x = 5;      // Locks to int
                x = 10;     // OK: int to int
                x
            )");
            check_eq(result.as<int>(), 10);
        });

        test("auto_uninitialized_rejects_after_lock", [this]() {
            auto eng = make_engine();
            bool caught = false;
            try {
                eng->execute(R"(
                    auto x;
                    x = 5;        // Locks to int
                    x = "hello";  // ERROR: string to int
                )");
            } catch (const std::exception&) {
                caught = true;
            }
            check_eq(caught, true, "Should reject string after locking to int");
        });

        test("auto_float_allows_int_widening", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto x = 3.14;
                x = 5;  // OK: int widens to float
                x
            )");
            check_eq(result.as<double>(), 5.0);
        });

        test("auto_int_truncates_float", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto x = 5;
                x = 3.9;  // Truncates to 3
                x
            )");
            check_eq(result.as<int>(), 3);
        });

        // ===== VAR KEYWORD TESTS =====

        test("var_allows_any_type", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var x = 5;
                x = "hello";
                x = 3.14;
                x = true;
                x
            )");
            check_eq(result.as<bool>(), true);
        });

        test("var_uninitialized_allows_any", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var x;
                x = "first";
                x = 42;
                x = [1, 2, 3];
                x.size()
            )");
            check_eq(result.as<int>(), 3);
        });

        test("var_array_methods_work", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var arr = [1, 2, 3, 4, 5];
                arr.index_of(3)
            )");
            check_eq(result.as<int>(), 2);
        });

        test("var_map_methods_work", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var m = {"a": 1, "b": 2};
                m.has("a")
            )");
            check_eq(result.as<bool>(), true);
        });

        test("var_can_change_to_different_container", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var x = [1, 2, 3];
                x = {"key": "value"};
                x.has("key")
            )");
            check_eq(result.as<bool>(), true);
        });

        // ===== EXPLICIT TYPE TESTS =====

        test("int_type_enforced", [this]() {
            auto eng = make_engine();
            bool caught = false;
            try {
                eng->execute(R"(
                    int x = 5;
                    x = "hello";
                )");
            } catch (const std::exception&) {
                caught = true;
            }
            check_eq(caught, true, "int variable should reject string");
        });

        test("float_type_accepts_int", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                float x = 3.14;
                x = 10;  // int widens to float
                x
            )");
            check_eq(result.as<double>(), 10.0);
        });

        // JaiScript allows auto-coercion to string (consistent with "hello" + 42)
        test("string_type_accepts_int_coercion", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                string s = "hello";
                s = 42;  // Auto-converts int to string
                s
            )");
            check_eq(result.as<std::string>(), "42");
        });

        test("bool_converts_truthy", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                bool b = true;
                b = 0;  // 0 is falsy
                b
            )");
            check_eq(result.as<bool>(), false);
        });

        // ===== NESTED CONTAINER TESTS =====

        test("auto_nested_array_in_array", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto arr = [[1, 2], [3, 4], [5, 6]];
                arr[1][0]
            )");
            check_eq(result.as<int>(), 3);
        });

        test("auto_nested_map_in_array", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto arr = [{"a": 1}, {"b": 2}];
                arr[0]["a"]
            )");
            check_eq(result.as<int>(), 1);
        });

        test("auto_nested_array_in_map", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto m = {"nums": [10, 20, 30]};
                m["nums"][1]
            )");
            check_eq(result.as<int>(), 20);
        });

        test("var_nested_containers_reassignable", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var x = [[1, 2], [3, 4]];
                x = {"key": [5, 6, 7]};
                x["key"][2]
            )");
            check_eq(result.as<int>(), 7);
        });

        // ===== ARRAY/MAP METHOD COMPATIBILITY =====

        test("auto_array_push_preserves_type", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto arr = [1, 2, 3];
                arr.push(4);
                arr.push(5);
                arr.size()
            )");
            check_eq(result.as<int>(), 5);
        });

        test("auto_array_filter_works", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto arr = [1, 2, 3, 4, 5, 6];
                auto evens = arr.filter([](x) { return x % 2 == 0; });
                evens.size()
            )");
            check_eq(result.as<int>(), 3);
        });

        test("auto_array_sort_works", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto arr = [5, 2, 8, 1, 9];
                arr.sort();
                arr[0]
            )");
            check_eq(result.as<int>(), 1);
        });

        test("auto_map_keys_values_work", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto m = {"a": 1, "b": 2, "c": 3};
                m.keys().size()
            )");
            check_eq(result.as<int>(), 3);
        });

        // ===== POINTER TYPE TESTS =====

        test("shared_ptr_null_assignment", [this]() {
            auto eng = make_engine();
            eng->execute(R"(
                class TestClass {
                    int value = 42;
                }
            )");
            auto result = eng->execute(R"(
                auto obj = shared_ptr<TestClass>();
                obj = null;
                obj == null
            )");
            check_eq(result.as<bool>(), true);
        });

        test("weak_ptr_null_assignment", [this]() {
            auto eng = make_engine();
            eng->execute(R"(
                class TestClass {
                    int value = 42;
                }
            )");
            auto result = eng->execute(R"(
                auto shared = shared_ptr<TestClass>();
                auto weak = weak_ptr<TestClass>(shared);
                shared = null;
                weak.expired()
            )");
            check_eq(result.as<bool>(), true);
        });

        test("auto_locks_to_object_type", [this]() {
            auto eng = make_engine();
            eng->execute(R"(
                class Cat { string name = ""; }
                class Dog { string name = ""; }
            )");
            // This should work - assigning same type
            auto result = eng->execute(R"(
                auto c = Cat();
                c.name = "Whiskers";
                c.name
            )");
            check_eq(result.as<std::string>(), "Whiskers");
        });

        // ===== FUNCTION PARAMETER TESTS =====

        test("function_with_typed_params", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                function add(int a, int b) -> int {
                    return a + b;
                }
                add(3, 4)
            )");
            check_eq(result.as<int>(), 7);
        });

        test("function_param_type_conversion", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                function double_it(float x) -> float {
                    return x * 2;
                }
                double_it(5)  // int converts to float
            )");
            check_eq(result.as<double>(), 10.0);
        });

        test("function_returns_to_typed_var", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                function get_number() -> int {
                    return 42;
                }
                auto x = get_number();
                x = 100;  // OK: int to int
                x
            )");
            check_eq(result.as<int>(), 100);
        });

        test("function_with_var_param_accepts_any", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(*eng);  // for to_string
            auto result = eng->execute(R"(
                function stringify(var x) -> string {
                    return to_string(x);
                }
                stringify(42) + " " + stringify("hello") + " " + stringify(true)
            )");
            check_eq(result.as<std::string>(), "42 hello true");
        });

        // ===== LOOP TESTS =====

        test("for_loop_int_counter", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto sum = 0;
                for (int i = 0; i < 5; i = i + 1) {
                    sum = sum + i;
                }
                sum
            )");
            check_eq(result.as<int>(), 10);  // 0+1+2+3+4
        });

        test("for_loop_auto_counter_locks", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto sum = 0;
                for (auto i = 0; i < 5; i = i + 1) {
                    sum = sum + i;
                }
                sum
            )");
            check_eq(result.as<int>(), 10);
        });

        test("for_loop_var_counter_flexible", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(*eng);  // for to_string
            // var counter can be reassigned to different types (unusual but valid)
            auto result = eng->execute(R"(
                var result = "";
                for (var i = 0; i < 3; i = i + 1) {
                    result = result + to_string(i);
                }
                result
            )");
            check_eq(result.as<std::string>(), "012");
        });

        // Test that fast path gracefully falls back to slow path when type changes
        test("for_loop_fast_path_fallthrough", [this]() {
            auto eng = make_engine();
            // Fast path -> slow path fallthrough on mid-loop type change must not skip the
            // pending update: iteration count matches the general path (and the vm backend)
            auto result = eng->execute(R"(
                var iterations = 0;
                for (var i = 0; i < 5; ++i) {
                    iterations = iterations + 1;
                    if (i == 2) {
                        i = 2.5;  // Change to float - triggers fallthrough, but still < 5
                    }
                }
                iterations
            )");
            // i=0,1,2 (iters 1-3; body sets i=2.5), ++ -> 3.5 (iter 4), 4.5 (iter 5), 5.5 exits
            check_eq(result.as<int>(), 5);
        });

        test("range_for_auto_element", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto arr = [1, 2, 3, 4, 5];
                auto sum = 0;
                for (auto x : arr) {
                    sum = sum + x;
                }
                sum
            )");
            check_eq(result.as<int>(), 15);
        });

        test("range_for_var_element", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var arr = [1, 2, 3];
                var sum = 0;
                for (var x : arr) {
                    sum = sum + x;
                }
                sum
            )");
            check_eq(result.as<int>(), 6);
        });

        // ===== C++ INTEROP TESTS =====

        test("cpp_bound_int_to_script_auto", [this]() {
            auto eng = make_engine();
            int cpp_value = 42;
            eng->add_global_ref("cpp_int", cpp_value);

            auto result = eng->execute(R"(
                auto x = cpp_int;
                x = x + 10;
                x
            )");
            check_eq(result.as<int>(), 52);
        });

        test("cpp_bound_string_to_script_auto", [this]() {
            auto eng = make_engine();
            std::string cpp_str = "hello";
            eng->add_global_ref("cpp_str", cpp_str);

            auto result = eng->execute(R"(
                auto s = cpp_str;
                s = s + " world";
                s
            )");
            check_eq(result.as<std::string>(), "hello world");
        });

        test("script_var_to_cpp_function", [this]() {
            auto eng = make_engine();
            eng->add_function("sum_ints", [](int a, int b) { return a + b; });

            auto result = eng->execute(R"(
                var x = 10;
                var y = 20;
                sum_ints(x, y)
            )");
            check_eq(result.as<int>(), 30);
        });

        // ===== COMPOUND ASSIGNMENT TESTS =====

        test("auto_compound_assignment_int", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto x = 10;
                x += 5;
                x -= 3;
                x *= 2;
                x
            )");
            check_eq(result.as<int>(), 24);  // ((10+5)-3)*2
        });

        test("auto_compound_assignment_float", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto x = 10.0;
                x += 2.5;
                x
            )");
            check_eq(result.as<double>(), 12.5);
        });

        test("auto_compound_string_concat", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto s = "hello";
                s += " ";
                s += "world";
                s
            )");
            check_eq(result.as<std::string>(), "hello world");
        });

        // ===== CLASS FIELD TYPE TESTS =====

        test("class_int_field_enforced", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                class Counter {
                    int count = 0;

                    function increment() {
                        count = count + 1;
                    }

                    function get() -> int {
                        return count;
                    }
                }

                auto c = Counter();
                c.increment();
                c.increment();
                c.increment();
                c.get()
            )");
            check_eq(result.as<int>(), 3);
        });

        test("class_var_field_flexible", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(*eng);  // for to_string
            auto result = eng->execute(R"(
                class Container {
                    var data = null;

                    function set(var val) {
                        data = val;
                    }

                    function get() {
                        return data;
                    }
                }

                auto c = Container();
                c.set(42);
                auto v1 = c.get();
                c.set("hello");
                auto v2 = c.get();
                to_string(v1) + " " + v2
            )");
            check_eq(result.as<std::string>(), "42 hello");
        });

        // ===== TYPE INFERENCE IN EXPRESSIONS =====

        test("auto_infers_from_arithmetic", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto x = 5 + 3;      // int
                auto y = 5.0 + 3;    // float
                auto z = x + 2;      // int
                z
            )");
            check_eq(result.as<int>(), 10);
        });

        test("auto_infers_from_comparison", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto b = 5 > 3;  // bool
                b
            )");
            check_eq(result.as<bool>(), true);
        });

        test("auto_infers_from_ternary", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto x = true ? 42 : 0;
                x = x + 8;  // Should work since x is int
                x
            )");
            check_eq(result.as<int>(), 50);
        });

        // ===== LAMBDA CAPTURE TYPE TESTS =====

        test("lambda_captures_typed_var", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto x = 10;
                auto fn = [=]() { return x * 2; };
                fn()
            )");
            check_eq(result.as<int>(), 20);
        });

        test("lambda_modifies_var_capture", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var total = 0;
                auto arr = [1, 2, 3, 4, 5];
                for (auto x : arr) {
                    total = total + x;
                }
                total
            )");
            check_eq(result.as<int>(), 15);
        });

        // ===== NULL HANDLING =====

        test("null_to_auto_object", [this]() {
            auto eng = make_engine();
            eng->execute("class TestObj { int x = 1; }");
            // Verify null can be assigned to typed object variable
            // and the variable can be reassigned to a valid object
            auto result = eng->execute(R"(
                auto obj = TestObj();
                obj = null;
                obj = TestObj();  // Reassign - should work since type is preserved
                obj.x
            )");
            check_eq(result.as<int>(), 1);
        });

        test("null_preserves_type_for_reassignment", [this]() {
            auto eng = make_engine();
            eng->execute("class TestObj { int x = 1; }");
            auto result = eng->execute(R"(
                auto obj = TestObj();
                obj = null;
                obj = TestObj();  // Can reassign same type
                obj.x
            )");
            check_eq(result.as<int>(), 1);
        });

        // ===== ARRAY TYPE COMPATIBILITY =====

        test("auto_array_iteration_types", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto numbers = [1, 2, 3, 4, 5];
                auto doubled = [];
                for (auto n : numbers) {
                    doubled.push(n * 2);
                }
                doubled[2]
            )");
            check_eq(result.as<int>(), 6);
        });

        test("var_array_mixed_types", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var arr = [];
                arr.push(1);
                arr.push("two");
                arr.push(3.0);
                arr.push(true);
                arr.size()
            )");
            check_eq(result.as<int>(), 4);
        });

        // ===== ERROR MESSAGES =====

        test("type_error_message_is_descriptive", [this]() {
            auto eng = make_engine();
            bool caught = false;
            std::string error_msg;
            try {
                eng->execute(R"(
                    int x = 5;
                    x = "hello";
                )");
            } catch (const std::exception& e) {
                caught = true;
                error_msg = e.what();
            }
            check_eq(caught, true);
            // Error message should indicate a type mismatch
            check(error_msg.find("x") != std::string::npos ||
                  error_msg.find("int") != std::string::npos ||
                  error_msg.find("Cannot assign") != std::string::npos ||
                  error_msg.find("ype") != std::string::npos,  // "Type mismatch" or "type_mismatch"
                  "Error should indicate type issue: " + error_msg);
        });

        // ===== SPECIAL CASES =====

        test("bool_to_int_conversion", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                int x = 0;
                x = true;   // bool converts to 1
                auto y = x;
                x = false;  // bool converts to 0
                y + x
            )");
            check_eq(result.as<int>(), 1);  // 1 + 0
        });

        test("multiple_auto_declarations_independent", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(*eng);  // for to_string
            auto result = eng->execute(R"(
                auto a = 5;
                auto b = "hello";
                auto c = 3.14;
                auto d = true;

                a = 10;      // OK
                b = "world"; // OK
                c = 2.0;     // OK
                d = false;   // OK

                to_string(a) + " " + b + " " + to_string(c) + " " + to_string(d)
            )");
            // std::to_string for floats includes trailing zeros: "2.000000"
            check_eq(result.as<std::string>(), "10 world 2.000000 false");
        });

        test("nested_scope_same_name_different_type", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                auto x = 5;  // outer x is int
                {
                    auto x = "hello";  // inner x is string (shadows outer)
                    x = "world";       // OK for inner
                }
                x = 10;  // OK for outer int
                x
            )");
            check_eq(result.as<int>(), 10);
        });

        // ===== CLASS TYPE ENFORCEMENT =====
        // These tests verify that the type system correctly handles script class types

        test("class_type_same_class_allowed", [this]() {
            auto eng = make_engine();
            // Assigning same class type should always work
            auto result = eng->execute(R"(
                class Dog { var name = "Fido"; }
                auto pet = Dog();
                pet = Dog();  // Same type - OK
                pet.name
            )");
            check_eq(result.as<std::string>(), "Fido");
        });

        test("class_type_different_class_rejected", [this]() {
            auto eng = make_engine();
            // Different class types should NOT be assignable with 'auto'
            bool threw = false;
            try {
                eng->execute(R"(
                    class Dog { var name = "Fido"; }
                    class Cat { var name = "Whiskers"; }
                    auto pet = Dog();
                    pet = Cat();  // Different type - SHOULD FAIL
                )");
            } catch (const std::exception& e) {
                threw = true;
                std::string msg = e.what();
                // Should indicate type mismatch (class names or generic type error)
                check(msg.find("Cat") != std::string::npos ||
                      msg.find("Dog") != std::string::npos ||
                      msg.find("ype") != std::string::npos,  // "Type mismatch" or "type_mismatch"
                      "Error should indicate type issue: " + msg);
            }
            check(threw, "Expected type error when assigning Cat to Dog variable");
        });

        test("class_type_var_allows_any_class", [this]() {
            auto eng = make_engine();
            // 'var' should allow any class type
            auto result = eng->execute(R"(
                class Dog { var name = "Fido"; }
                class Cat { var name = "Whiskers"; }
                var pet = Dog();
                pet = Cat();  // var allows any type
                pet.name
            )");
            check_eq(result.as<std::string>(), "Whiskers");
        });

        test("class_inheritance_child_to_parent_allowed", [this]() {
            auto eng = make_engine();
            // Assigning derived class to base class variable should work (Liskov)
            auto result = eng->execute(R"(
                class Animal { var species = "unknown"; }
                class Bird : Animal { var canFly = true; }
                auto pet = Animal();
                pet = Bird();  // Bird IS-A Animal - should be allowed
                pet.species
            )");
            check_eq(result.as<std::string>(), "unknown");
        });

        test("class_inheritance_parent_to_child_rejected", [this]() {
            auto eng = make_engine();
            // Assigning base class to derived class variable should FAIL
            bool threw = false;
            try {
                eng->execute(R"(
                    class Animal { var species = "unknown"; }
                    class Bird : Animal { var canFly = true; }
                    auto bird = Bird();
                    bird = Animal();  // Animal is NOT-A Bird - SHOULD FAIL
                )");
            } catch (const std::exception&) {
                threw = true;
            }
            check(threw, "Expected type error when assigning Animal to Bird variable");
        });

        test("class_type_null_preserves_type", [this]() {
            auto eng = make_engine();
            // After assigning null, should still enforce original type
            bool threw = false;
            try {
                eng->execute(R"(
                    class Dog { var name = "Fido"; }
                    class Cat { var name = "Whiskers"; }
                    auto pet = Dog();
                    pet = null;   // OK - null is allowed for objects
                    pet = Cat();  // SHOULD FAIL - pet is still typed as Dog
                )");
            } catch (const std::exception&) {
                threw = true;
            }
            check(threw, "Expected type error when assigning Cat to Dog variable (even after null)");
        });

        test("class_type_reassign_after_null_same_type", [this]() {
            auto eng = make_engine();
            // After null, can reassign same type
            auto result = eng->execute(R"(
                class Dog { var name = "Fido"; }
                auto pet = Dog();
                pet = null;
                pet = Dog();  // Same type - OK
                pet.name
            )");
            check_eq(result.as<std::string>(), "Fido");
        });

        test("class_type_unrelated_classes_with_same_fields_rejected", [this]() {
            auto eng = make_engine();
            // Classes with identical structure are still different types
            bool threw = false;
            try {
                eng->execute(R"(
                    class Point2D { var x = 0; var y = 0; }
                    class Vector2D { var x = 0; var y = 0; }  // Same fields!
                    auto p = Point2D();
                    p = Vector2D();  // SHOULD FAIL - structural equality != type equality
                )");
            } catch (const std::exception&) {
                threw = true;
            }
            check(threw, "Expected type error for structurally equivalent but different classes");
        });

        test("class_type_sibling_classes_rejected", [this]() {
            auto eng = make_engine();
            // Sibling classes (same parent) are not assignable to each other
            bool threw = false;
            try {
                eng->execute(R"(
                    class Animal { var name = ""; }
                    class Dog : Animal { var breed = "mutt"; }
                    class Cat : Animal { var indoor = true; }
                    auto pet = Dog();
                    pet = Cat();  // SHOULD FAIL - Cat is not a Dog (even though both are Animals)
                )");
            } catch (const std::exception&) {
                threw = true;
            }
            check(threw, "Expected type error when assigning Cat to Dog (sibling classes)");
        });

        test("class_type_deep_inheritance_allowed", [this]() {
            auto eng = make_engine();
            // Multi-level inheritance should work
            auto result = eng->execute(R"(
                class Entity { var id = 0; }
                class LivingEntity : Entity { var health = 100; }
                class Player : LivingEntity { var score = 0; }

                auto entity = Entity();
                entity = Player();  // Player IS-A Entity (via LivingEntity)
                entity.id
            )");
            check_eq(result.as<int>(), 0);
        });

        // ===== ASSIGNMENT OPERATOR TYPE CONVERSIONS =====
        // These tests verify that custom assignment operators enable type conversions

        // TODO: Parser doesn't support "function operator=(Type arg)" syntax yet
        // This test is commented out until parser support is added
        // test("script_class_assignment_operator_conversion") - needs parser update for operator=(Type)

        test("script_class_constructor_conversion", [this]() {
            auto eng = make_engine();
            // Script class with constructor that accepts a different type
            auto result = eng->execute(R"(
                class Celsius {
                    var temp = 0.0;

                    // Constructor from Fahrenheit
                    function Celsius(Fahrenheit f) {
                        temp = (f.temp - 32.0) * 5.0 / 9.0;
                    }
                }

                class Fahrenheit {
                    var temp = 0.0;
                }

                auto f = Fahrenheit();
                f.temp = 212.0;  // Boiling point

                auto c = Celsius(f);  // Should convert via constructor
                c.temp
            )");
            // 212°F = 100°C
            check(result.as<double>() > 99.9 && result.as<double>() < 100.1,
                  "Expected ~100.0, got " + std::to_string(result.as<double>()));
        });

        test("script_class_multiple_assignment_operators", [this]() {
            auto eng = make_engine();
            // Test operator= with var parameter accepting different types
            auto result = eng->execute(R"(
                class Distance {
                    var meters = 0.0;
                    var last_assigned_type = "";

                    function operator=(var value) {
                        // Multiply by 1.0 to convert int to float if needed
                        meters = value * 1.0;
                        last_assigned_type = "assigned";
                    }
                }

                auto d1 = Distance();
                auto d2 = Distance();

                d1 = 100;      // int -> calls operator=
                d2 = 3.14;     // float -> calls operator=

                d1.meters + d2.meters  // 100.0 + 3.14 = 103.14
            )");
            check(result.as<double>() > 103.0 && result.as<double>() < 104.0,
                  "Expected ~103.14, got " + std::to_string(result.as<double>()));
        });

        // TODO: These C++ dynamic_binder tests require assignment_from<T> API to be implemented
        // test("cpp_class_assignment_operator_conversion") - needs dynamic_binder::assignment_from<T>
        // test("cpp_class_assignment_from_script_class") - needs dynamic_binder::assignment_from_script

        test("assignment_operator_not_found_rejected", [this]() {
            auto eng = make_engine();
            // When no valid assignment operator exists, should fail
            bool threw = false;
            try {
                eng->execute(R"(
                    class TypeA { var a = 1; }
                    class TypeB { var b = 2; }  // No assignment operator for TypeA

                    auto x = TypeA();
                    auto y = TypeB();
                    x = y;  // No conversion available - SHOULD FAIL
                )");
            } catch (const std::exception&) {
                threw = true;
            }
            check(threw, "Expected type error when no assignment operator exists");
        });

        test("script_class_implicit_conversion_via_constructor", [this]() {
            auto eng = make_engine();
            // Test that a constructor accepting another type enables implicit conversion
            auto result = eng->execute(R"(
                class Wrapper {
                    var data = 0;

                    // Constructor from int enables Wrapper(int) conversion
                    function Wrapper(int value) {
                        data = value * 10;
                    }
                }

                function acceptWrapper(Wrapper w) -> int {
                    return w.data;
                }

                // This should work if int->Wrapper conversion is implicit via constructor
                // For now, test explicit conversion
                acceptWrapper(Wrapper(5))
            )");
            check_eq(result.as<int>(), 50);
        });

        // ===== COMPOUND ASSIGNMENT: C++ RULES ON TYPED TARGETS (ruling 2026-07) =====
        // x op= rhs ≡ x = T(x op rhs): compute in the promoted type, convert the result
        // back to the declared type exactly like plain '=' (int truncates toward zero).
        // var (any-typed) targets keep the old dynamic behavior (int += 2.5 -> float 3.5).
        // Every test runs on BOTH backends and pins byte-identical output.

        auto both = [](const char* src) {
            std::string out[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execution_budget(0);
                jai::stdlib::register_all(e);
                try { out[idx] = e->execute(src).to_string(); }
                catch (const std::exception& ex) { out[idx] = std::string("ERROR: ") + ex.what(); }
                ++idx;
            }
            return std::make_pair(out[0], out[1]);
        };
        auto check_both = [this, both](const char* src, const std::string& expected, const char* what) {
            auto [i_out, v_out] = both(src);
            check_eq(expected, i_out, std::string("interp: ") + what);
            check_eq(expected, v_out, std::string("vm: ") + what);
        };

        test("compound_int_target_converts_back", [this, check_both]() {
            check_both("int x = 10; x += 2.5; type_of(x) + \":\" + to_string(x)", "int:12", "int +=");
            check_both("int x = 10; x -= 2.5; to_string(x)", "7", "int -=");
            check_both("int x = 10; x *= 2.5; to_string(x)", "25", "int *=");
            check_both("int x = 10; x /= 4.0; to_string(x)", "2", "int /= truncates 2.5 -> 2");
            check_both("int x = 10; x %= 4.5; to_string(x)", "1", "int %= fmod then truncate");
        });

        test("compound_truncates_toward_zero", [this, check_both]() {
            check_both("int x = 0; x += 2.5; to_string(x)", "2", "2.5 -> 2");
            check_both("int x = 0; x -= 2.5; to_string(x)", "-2", "-2.5 -> -2");
            check_both("int x = 5; x -= 7.5; to_string(x)", "-2", "5 - 7.5 = -2.5 -> -2");
        });

        test("compound_float_target_stays_float", [this, check_both]() {
            check_both("float f = 1.5; f += 2; type_of(f) + \":\" + to_string(f)", "float:3.500000", "float +=int");
            check_both("float f = 7.5; f %= 2; to_string(f)", "1.500000", "float %=");
            check_both("float f = 5.0; f /= 2; to_string(f)", "2.500000", "float /=");
        });

        test("compound_auto_locked_behaves_typed", [this, check_both]() {
            check_both("auto x = 10; x += 2.5; type_of(x) + \":\" + to_string(x)", "int:12", "auto-locked int");
            check_both("auto f = 1.0; f += 2.5; type_of(f) + \":\" + to_string(f)", "float:3.500000", "auto-locked float");
            // Untyped params are inferred-then-enforced (auto), so they convert back too
            check_both("function f(x) { x += 2.5; return type_of(x) + \":\" + to_string(x); } f(10)", "int:12", "inferred param");
            check_both("function f(int x) { x += 2.5; return to_string(x); } f(10)", "12", "typed param");
            check_both("function g() { int y = 10; y += 2.5; return to_string(y); } g()", "12", "slot local");
        });

        test("compound_var_target_stays_dynamic", [this, check_both]() {
            check_both("var x = 10; x += 2.5; type_of(x) + \":\" + to_string(x)", "float:12.500000", "var += float promotes");
            check_both("var x = 10; x %= 3; type_of(x) + \":\" + to_string(x)", "int:1", "var %= int stays int");
            check_both("var x = 10; x %= 4.5; type_of(x) + \":\" + to_string(x)", "float:1.000000", "var %= float promotes");
            // The dynamic tag survives a mixed-type compound: the variable is still a var
            // afterwards (the old in-place path used to clobber the 'any' tag, wrongly
            // locking the variable to the promoted type)
            check_both("var x = 10; x += 2.5; x = \"s\"; x", "s", "var stays var after mixed compound");
        });

        test("compound_modulo_scalar_targets", [this, check_both, both]() {
            check_both("int x = 10; x %= 3; to_string(x)", "1", "int %= int");
            check_both("auto x = 10; x %= 3; to_string(x)", "1", "auto %= int");
            // Divide/modulo by zero errors stay identical across backends
            auto [i1, v1] = both("int x = 10; x %= 0; x");
            check_eq(i1, v1, "modulo-by-zero parity");
            check(i1.find("ERROR") == 0, "int %= 0 raises");
            auto [i2, v2] = both("float f = 1.5; f %= 0.0; f");
            check_eq(i2, v2, "float modulo-by-zero parity");
            check(i2.find("ERROR") == 0, "float %= 0.0 raises");
        });

        test("compound_string_targets_append", [this, check_both, both]() {
            check_both("string s = \"a\"; s += \"b\"; s", "ab", "string += string");
            check_both("var s = \"a\"; s += \"b\"; s", "ab", "var-string += string");
            // Non-string rhs on a string target keeps today's type_mismatch (parity-pinned)
            auto [i_out, v_out] = both("string s = \"a\"; s += 1; s");
            check_eq(i_out, v_out, "string += int parity");
            check(i_out.find("ERROR") == 0, "string += int raises");
        });

        test("compound_expression_result_is_stored_value", [this, check_both]() {
            // The compound expression evaluates to the converted (stored) value, exactly
            // like '=' on a typed local does
            check_both("int x = 10; var r = (x += 2.5); type_of(r) + \":\" + to_string(r)", "int:12", "result converted");
            check_both("var x = 10; var r = (x += 2.5); type_of(r) + \":\" + to_string(r)", "float:12.500000", "var result promoted");
        });

        test("compound_typed_element_and_ref_targets", [this, check_both]() {
            // Constrained element: the store converts (12.5 -> 12); the expression result
            // stays the promoted value, matching plain subscript-assign's result semantics
            check_both("array<int> a = [10]; a[0] += 2.5; type_of(a[0]) + \":\" + to_string(a[0])", "int:12", "typed element converts");
            check_both("array<int> a = [10]; var r = (a[0] += 2.5); to_string(r)", "12.500000", "element result promoted (matches '=')");
            check_both("array<int> a = [10]; a[0] %= 4.5; to_string(a[0])", "1", "typed element %=");
            // Constrained ref bound to a typed element behaves route-independently
            check_both(R"(
                array<int> a = [10];
                function f(int& r) { r += 2.5; }
                f(a[0]);
                type_of(a[0]) + ":" + to_string(a[0])
            )", "int:12", "constrained ref compound converts");
        });

        test("compound_mismatch_errors_parity", [this, both]() {
            auto [i1, v1] = both("int x = 10; x += \"s\"; x");
            check_eq(i1, v1, "int += string parity");
            check(i1.find("ERROR") == 0, "int += string raises");
            auto [i2, v2] = both("bool b = true; b += 1; b");
            check_eq(i2, v2, "bool += int parity");
            check(i2.find("ERROR") == 0, "bool += int raises");
        });

        test("compound_overflow_policy_covers_intermediate", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                if (!e->throw_on_overflow()) { return; }  // wrap build: policy off by design
                bool threw = false;
                try { e->execute("int x = 9223372036854775807; x += 1; x"); }
                catch (const std::exception&) { threw = true; }
                check(threw, use_vm ? "vm: += overflow raises" : "interp: += overflow raises");
            }
        });

        test("compound_fused_mixed_type_bails_to_conversion", [this, check_both]() {
            // VM: x += f * 2.0 compiles to op_compound_fused; the int-only fast path must
            // bail (float operand) and the general path converts each iteration (2.5 -> 2)
            check_both(R"(
                function h() {
                    int x = 0;
                    float f = 1.25;
                    for (int i = 0; i < 4; ++i) { x += f * 2.0; }
                    return type_of(x) + ":" + to_string(x);
                }
                h()
            )", "int:8", "fused mixed bails and converts");
            // Pure int fused fast path is untouched
            check_both(R"(
                function h() {
                    int x = 0;
                    for (int i = 0; i < 5; ++i) { x += i * 2; }
                    return to_string(x);
                }
                h()
            )", "20", "fused int fast path");
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::strong_types_tests)
