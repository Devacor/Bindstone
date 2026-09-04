#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <array>
#include <string>
#include <vector>

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
            const char* src = R"(
                auto x = 5;
                x = "hello";  // ERROR: string to int
            )";
            // strict (default): the locked auto is a typed local, the store is a load error
            auto eng = make_engine();
            std::string text;
            try { eng->execute(src); } catch (const static_check_error& e) { text = e.what(); }
            check_true(text.find("cannot assign 'string' to 'x' declared 'int'") != std::string::npos,
                "strict rejects the store before anything runs (got: " + text + ")");
            // warn: the runtime's own rejection
            auto warn = make_engine();
            warn->static_checking(check_mode::warn);
            bool caught = false;
            try {
                warn->execute(src);
            } catch (const std::exception& e) {
                caught = true;
                std::string msg = e.what();
                check(msg.find("Cannot assign") != std::string::npos ||
                      msg.find("Type mismatch") != std::string::npos);
            }
            check_eq(caught, true, "Should reject string assignment to int variable");
            // strict admits a var-sourced store; the runtime still rejects it
            auto dynamic = make_engine();
            caught = false;
            try {
                dynamic->execute("auto x = 5; var s = \"hello\"; x = s;");
            } catch (const static_check_error&) {
            } catch (const std::exception& e) {
                caught = true;
                check_true(std::string(e.what()).find("Type mismatch") != std::string::npos);
            }
            check_eq(caught, true, "var-sourced string into a locked int raises at runtime");
        });

        test("extern_is_a_runtime_no_op_on_both_backends", [this]() {
            auto eng = make_engine();
            // typed, untyped and function externs run as nothing; the names resolve dynamically
            auto r = eng->execute(R"(
                extern int hp;
                extern loose;
                extern function host_fn;
                class Game { int v = 3; }
                extern shared_ptr<Game> G;
                function total() -> int { return hp + G.v; }
                var caught = "";
                try { total(); } catch (e) { caught = e; }
                caught
            )");
            check_eq(std::string("Undefined variable 'hp'"), r.as<std::string>());
            eng->add_global("hp", script_value((int64_t)4, eng.get()));
            eng->execute("var G = Game();");
            check_eq((int64_t)7, eng->execute("total()").as_int());
            // a typed extern is a checked receiver: the store below is rejected before anything runs
            auto strict = make_engine();
            std::string text;
            try {
                strict->execute("class Game { int v = 3; }\nextern Game H;\nfunction poke() { H.v = \"s\"; }");
            } catch (const static_check_error& e) {
                text = e.what();
            }
            check_true(text.find("cannot assign 'string' to field 'v'") != std::string::npos,
                "typed extern flows into member checks (got: " + text + ")");
        });

        test("warn_mode_regions_keep_the_runtime_conversion_errors", [this]() {
            // unit-level, statement-level and block-first: strict loads each, the runtime's own
            // rejection is byte-identical to the warn engine's (both backends)
            for (const char* src : {"[[warn_mode]];\nint x = \"s\";\nx",
                                    "int a = 1;\n[[warn_mode]] int x = \"s\";\nx",
                                    "int a = 1;\n{ [[warn_mode]] a = 2; int x = \"s\"; }\na"}) {
                auto eng = make_engine();
                std::string text;
                try {
                    eng->execute(src);
                } catch (const static_check_error& e) {
                    text = std::string("STATIC ") + e.what();
                } catch (const std::exception& e) {
                    text = e.what();
                }
                check_true(text.find("Cannot initialize") == 0, "runtime rejects the store (got: " + text + ")");
                auto warn = make_engine();
                warn->static_checking(check_mode::warn);
                std::string warn_text;
                try { warn->execute(src); } catch (const std::exception& e) { warn_text = e.what(); }
                check_eq(text, warn_text);
            }
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

        auto both_in = [](check_mode mode, const char* src) {
            std::string out[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execution_budget(0);
                e->static_checking(mode);
                jai::stdlib::register_all(e);
                try { out[idx] = e->execute(src).to_string(); }
                catch (const std::exception& ex) { out[idx] = std::string("ERROR: ") + ex.what(); }
                ++idx;
            }
            return std::make_pair(out[0], out[1]);
        };
        auto both = [both_in](const char* src) { return both_in(check_mode::strict, src); };
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

        // ===== TYPED CLASS FIELDS ENFORCE LIKE LOCALS (ruling 2026-07) =====
        // Declared field types are stored on the class definition and every write path
        // converts through the field kernel (same table '=' uses on locals): direct member
        // assign, implicit-this assign, compound, ctor field-init, set_field C++ API,
        // from_json. var fields stay dynamic; auto fields infer from the initialized value
        // then enforce.

        test("typed_field_direct_assign_converts", [this, check_both, both, both_in]() {
            check_both(R"(
                class C { int v = 0; }
                auto c = C();
                c.v = 4.7;
                type_of(c.v) + ":" + to_string(c.v)
            )", "int:4", "int field truncates float assign");
            check_both(R"(
                class C { float f = 0.0; }
                auto c = C();
                c.f = 3;
                type_of(c.f) + ":" + to_string(c.f)
            )", "float:3.000000", "float field widens int assign");
            check_both(R"(
                class C { string s = ""; }
                auto c = C();
                c.s = 42;
                c.s
            )", "42", "string field converts via to_string");
            // a string literal into an int field: a strict load error; the locals-identical
            // runtime text under warn and, under strict, for a var-sourced value
            const char* typed_mismatch = R"(
                class C { int v = 0; }
                auto c = C();
                c.v = "nope";
                c.v
            )";
            auto [i_out, v_out] = both(typed_mismatch);
            check_eq(i_out, v_out, "string-into-int field strict listing parity");
            check_true(i_out.find("ERROR: static check failed") == 0 &&
                i_out.find("cannot assign 'string' to field 'v' declared 'int'") != std::string::npos,
                std::string("strict rejects the field store at load, got: ") + i_out);
            auto [w_out, wv_out] = both_in(check_mode::warn, typed_mismatch);
            check_eq(w_out, wv_out, "string-into-int field error parity");
            check_true(w_out.find("ERROR") == 0 && w_out.find("Cannot assign string to field 'v' of C (declared int)") != std::string::npos,
                std::string("locals-identical error text, got: ") + w_out);
            auto [d_out, dv_out] = both(R"(
                class C { int v = 0; }
                auto c = C();
                var s = "nope";
                c.v = s;
                c.v
            )");
            check_eq(d_out, dv_out, "var-sourced string-into-int field error parity");
            check_true(d_out.find("ERROR") == 0 && d_out.find("Cannot assign string to field 'v' of C (declared int)") != std::string::npos,
                std::string("var-sourced store keeps the runtime text under strict, got: ") + d_out);
        });

        test("typed_field_implicit_this_and_ctor_convert", [this, check_both, both]() {
            check_both(R"(
                class C {
                    int v = 0;
                    void set(var x) { v = x; }
                }
                auto c = C();
                c.set(9.9);
                type_of(c.v) + ":" + to_string(c.v)
            )", "int:9", "implicit-this assign converts");
            check_both(R"(
                class C { int v = 4.7; }
                auto c = C();
                type_of(c.v) + ":" + to_string(c.v)
            )", "int:4", "ctor field-init converts the initializer");
            check_both(R"(
                class C {
                    int v = 0;
                    C(var x) { v = x; }
                }
                auto c = C(6.9);
                to_string(c.v)
            )", "6", "ctor body assign converts");
            auto [i_out, v_out] = both(R"(
                class C {
                    int v = 0;
                    void set(var x) { v = x; }
                }
                auto c = C();
                c.set("bad");
                c.v
            )");
            check_eq(i_out, v_out, "implicit-this mismatch parity");
            check_true(i_out.find("ERROR") == 0, "implicit-this mismatch raises");
        });

        test("typed_field_compound_converts", [this, check_both]() {
            check_both(R"(
                class C { int v = 10; }
                auto c = C();
                c.v += 2.5;
                type_of(c.v) + ":" + to_string(c.v)
            )", "int:12", "member compound converts back");
            check_both(R"(
                class C {
                    int v = 10;
                    void bump() { v += 2.5; }
                }
                auto c = C();
                c.bump();
                type_of(c.v) + ":" + to_string(c.v)
            )", "int:12", "implicit-this compound converts back");
            check_both(R"(
                class C { int v = 10; }
                auto c = C();
                c.v %= 4.5;
                to_string(c.v)
            )", "1", "member %= fmod then truncate");
        });

        test("var_and_auto_fields", [this, check_both, both]() {
            // var fields stay fully dynamic
            check_both(R"(
                class C { var v = 1; }
                auto c = C();
                c.v = 2.5;
                var t1 = type_of(c.v);
                c.v = "s";
                t1 + "|" + c.v
            )", "float|s", "var field stays dynamic");
            check_both(R"(
                class C { var v = 10; }
                auto c = C();
                c.v += 2.5;
                type_of(c.v) + ":" + to_string(c.v)
            )", "float:12.500000", "var field compound stays dynamic");
            // auto fields infer from the initializer then enforce
            check_both(R"(
                class C { auto v = 1; }
                auto c = C();
                c.v = 2.5;
                type_of(c.v) + ":" + to_string(c.v)
            )", "int:2", "auto field locks to inferred int");
            auto [i_out, v_out] = both(R"(
                class C { auto v = 1; }
                auto c = C();
                c.v = "s";
                c.v
            )");
            check_eq(i_out, v_out, "auto field mismatch parity");
            check_true(i_out.find("ERROR") == 0, "auto field rejects a string");
        });

        test("typed_field_inheritance_enforced", [this, check_both, both]() {
            check_both(R"(
                class Base { int bv = 0; }
                class Derived : Base { var dv = 0; }
                auto d = Derived();
                d.bv = 7.7;
                type_of(d.bv) + ":" + to_string(d.bv)
            )", "int:7", "base typed field enforced on derived instance");
            auto [i_out, v_out] = both(R"(
                class Base { int bv = 0; }
                class Derived : Base { var dv = 0; }
                auto d = Derived();
                d.bv = "bad";
                d.bv
            )");
            check_eq(i_out, v_out, "inherited field mismatch parity");
            check_true(i_out.find("ERROR") == 0, "inherited typed field rejects a string");
        });

        test("typed_field_object_fields_keep_object_model", [this, check_both, both, both_in]() {
            // Declared class-typed fields accept same class, subtypes, null, and shared_ptr
            // wrappers (JaiScript objects travel as object OR shared_ptr over one holder).
            // A subtype member is reached through cast<T&> or a var receiver: the field's
            // static class is S, and S has no 't' (exactly like C++).
            check_both(R"(
                class S { int x = 1; }
                class T : S { int t = 2; }
                class H { S p = null; }
                auto h = H();
                h.p = S();
                var a = h.p.x;
                h.p = T();
                var b = cast<T&>(h.p).t;
                var hv = h.p;
                var b2 = hv.t;
                h.p = null;
                var c2 = "n";
                if (h.p == null) { c2 = "null"; }
                to_string(a) + "|" + to_string(b) + "|" + to_string(b2) + "|" + c2
            )", "1|2|2|null", "same class, subtype (cast<T&> and var receiver), and null accepted");
            const char* base_receiver = R"(
                class S { int x = 1; }
                class T : S { int t = 2; }
                class H { S p = null; }
                auto h = H();
                h.p = T();
                h.p.t
            )";
            auto [s_out, sv_out] = both(base_receiver);
            check_eq(s_out, sv_out, "subtype member on the base-typed field: strict listing parity");
            check_true(s_out.find("ERROR: static check failed") == 0 &&
                s_out.find("no member 't' in class 'S'") != std::string::npos,
                std::string("strict rejects the subtype member on the static base class, got: ") + s_out);
            auto [w_out, wv_out] = both_in(check_mode::warn, base_receiver);
            check_eq(std::string("2"), w_out, "warn: the dynamic object still answers");
            check_eq(w_out, wv_out, "warn subtype-member parity");
            const char* unrelated = R"(
                class S { int x = 1; }
                class B { int x = 1; }
                class H { S p = null; }
                auto h = H();
                h.p = B();
                h.p
            )";
            auto [i_out, v_out] = both(unrelated);
            check_eq(i_out, v_out, "unrelated class rejection parity");
            check_true(i_out.find("ERROR: static check failed") == 0 &&
                i_out.find("cannot assign 'B' to field 'p' declared 'S'") != std::string::npos,
                std::string("unrelated class rejected at load, got: ") + i_out);
            auto [wu_out, wuv_out] = both_in(check_mode::warn, unrelated);
            check_eq(wu_out, wuv_out, "warn unrelated class rejection parity");
            check_true(wu_out.find("ERROR") == 0 && wu_out.find("static check failed") == std::string::npos,
                std::string("warn: the runtime rejects the unrelated class, got: ") + wu_out);
            auto [du_out, duv_out] = both(R"(
                class S { int x = 1; }
                class B { int x = 1; }
                class H { S p = null; }
                auto h = H();
                var b = B();
                h.p = b;
                h.p
            )");
            check_eq(wu_out, du_out, "var-sourced unrelated class: the same runtime text under strict");
            check_eq(du_out, duv_out, "var-sourced unrelated class rejection parity");
        });

        // strict vs warn at the typed boundary: a strongly-typed -> strongly-typed mismatch is
        // a load error under strict and the runtime's conversion error under warn; a var-sourced
        // value reaches the runtime in both modes; clean typed programs are mode-blind. Every
        // row is byte-identical across backends.
        test("strict_vs_warn_boundary", [this, both_in]() {
            struct row { const char* src; const char* strict_needle; const char* warn_needle; };
            const row typed_mismatches[] = {
                {"int x = \"s\"; x",
                 "cannot initialize 'x' declared 'int' with 'string'", "Cannot initialize 'x' (declared int) with string"},
                {"function bad() -> int { return \"nope\"; } bad()",
                 "cannot return 'string' from 'bad' declared to return 'int'", "Cannot return string from 'bad' (declared -> int)"},
                {"function takes(int x) -> int { return x; } takes([1, 2])",
                 "argument 1 of 'takes': cannot pass 'array' to 'x' declared 'int'", "Cannot pass array to parameter 'x' of 'takes' (declared int)"},
                {"class A { int a = 0; } class B { int b = 0; } function f(A v) -> int { return v.a; } f(B())",
                 "argument 1 of 'f': cannot pass 'B' to 'v' declared 'A'", "Type mismatch"},
                {"class C { int v = 0; } auto c = C(); c.v = \"nope\"; c.v",
                 "cannot assign 'string' to field 'v' declared 'int'", "Cannot assign string to field 'v' of C (declared int)"},
                {"auto x = 5; x = \"hello\"; x",
                 "cannot assign 'string' to 'x' declared 'int'", "Cannot assign string to 'x' (declared int)"},
                {"int x = 1; x += \"s\"; x",
                 "cannot assign 'string' to 'x' declared 'int'", "Type mismatch"},
            };
            for (const row& r : typed_mismatches) {
                auto [si, sv] = both_in(check_mode::strict, r.src);
                check_eq(si, sv, std::string("strict listing parity: ") + r.src);
                check_true(si.find("ERROR: static check failed") == 0 && si.find(r.strict_needle) != std::string::npos,
                    std::string("strict load error: ") + r.src + " (got: " + si + ")");
                auto [wi, wv] = both_in(check_mode::warn, r.src);
                check_eq(wi, wv, std::string("warn runtime text parity: ") + r.src);
                check_true(wi.find("ERROR: ") == 0 && wi.find("static check failed") == std::string::npos &&
                    wi.find(r.warn_needle) != std::string::npos,
                    std::string("warn runtime error: ") + r.src + " (got: " + wi + ")");
            }
            struct var_row { const char* src; const char* needle; };
            const var_row var_sourced[] = {
                {"var s = \"s\"; int x = s; x", "Cannot initialize 'x' (declared int) with string"},
                {"var v = \"nope\"; function bad() -> int { return v; } bad()", "Cannot return string from 'bad' (declared -> int)"},
                {"var arr = [1, 2]; function takes(int x) -> int { return x; } takes(arr)", "Cannot pass array to parameter 'x' of 'takes' (declared int)"},
                {"class A { int a = 0; } class B { int b = 0; } function f(A v) -> int { return v.a; } var b = B(); f(b)", "Type mismatch"},
                {"class C { int v = 0; } auto c = C(); var s = \"nope\"; c.v = s; c.v", "Cannot assign string to field 'v' of C (declared int)"},
                {"auto x = 5; var s = \"hello\"; x = s; x", "Cannot assign string to 'x' (declared int)"},
                {"var caught = \"\"; var s = \"s\"; try { int x = s; } catch (e) { caught = e; } caught", "Cannot initialize 'x' (declared int) with string"},
            };
            for (const var_row& r : var_sourced) {
                auto [si, sv] = both_in(check_mode::strict, r.src);
                auto [wi, wv] = both_in(check_mode::warn, r.src);
                check_eq(si, sv, std::string("var-sourced strict parity: ") + r.src);
                check_eq(si, wi, std::string("var-sourced: strict and warn agree: ") + r.src);
                check_eq(wi, wv, std::string("var-sourced warn parity: ") + r.src);
                check_true(si.find("static check failed") == std::string::npos && si.find(r.needle) != std::string::npos,
                    std::string("var-sourced runtime text: ") + r.src + " (got: " + si + ")");
            }
            const char* last_var = var_sourced[6].src;
            check_eq(std::string("Cannot initialize 'x' (declared int) with string"), both_in(check_mode::strict, last_var).first,
                "the var-sourced runtime error is catchable in script under strict");
            struct clean_row { const char* src; const char* expected; };
            const clean_row clean[] = {
                {"int x = 4.7; x", "4"},
                {"string s = 42; s", "42"},
                {"float f = 2; type_of(f) + \":\" + to_string(f)", "float:2.000000"},
                {"class S { int x = 1; } class T : S { int t = 2; } function f(S s) -> int { return s.x; } f(T())", "1"},
                {"function twice(int v) -> int { return v * 2; } twice(21)", "42"},
                {"class C { int v = 0; } auto c = C(); c.v = 4.7; type_of(c.v) + \":\" + to_string(c.v)", "int:4"},
                {"class S { int x = 1; } class T : S { int t = 2; } class H { S p = null; } auto h = H(); h.p = T(); cast<T&>(h.p).t", "2"},
            };
            for (const clean_row& r : clean) {
                auto [si, sv] = both_in(check_mode::strict, r.src);
                auto [wi, wv] = both_in(check_mode::warn, r.src);
                check_eq(std::string(r.expected), si, std::string("clean under strict: ") + r.src);
                check_eq(si, sv, std::string("clean strict parity: ") + r.src);
                check_eq(si, wi, std::string("clean: strict and warn agree: ") + r.src);
                check_eq(wi, wv, std::string("clean warn parity: ") + r.src);
            }
        });

        // cast<T>(e) / cast<T&>(e) / cast<shared_ptr<T>>(e) (docs/grammar.md, detail/cast_kernel.hpp):
        // every row is byte-identical on both backends under the strict default
        const std::string cast_prelude =
            "class S { int x = 1; } class T : S { int t = 2; } class H { S p = null; } "
            "auto h = H(); h.p = T(); ";
        auto with_prelude = [cast_prelude](const char* tail) { return cast_prelude + tail; };

        test("cast_value_reference_and_shared_forms", [this, check_both, with_prelude]() {
            check_both(with_prelude("cast<T&>(h.p).t").c_str(), "2", "reference form reads the subtype member");
            check_both(with_prelude("cast<T&>(h.p).t = 5; var d = h.p; to_string(d.t)").c_str(), "5", "reference form writes through");
            check_both(with_prelude("T tv = cast<T>(h.p); tv.t = 9; var d = h.p; to_string(d.t) + \",\" + to_string(tv.t)").c_str(), "2,9", "value form is an independent copy");
            check_both(with_prelude("shared_ptr<T> th = cast<shared_ptr<T>>(h.p); th.t = 11; var d = h.p; to_string(d.t)").c_str(), "11", "shared_ptr form shares the instance");
            check_both(with_prelude("auto& r = cast<T&>(h.p); r.t = 21; cast<T&>(h.p).t").c_str(), "21", "auto& aliases the cast reference");
            check_both(with_prelude("T tmp = T(); auto& r = cast<T&>(tmp); r.t = 33; to_string(tmp.t)").c_str(), "33", "identifier operand binds the variable");
            check_both(with_prelude("cast<T&>(T()).t").c_str(), "2", "temporary operand is pinned");
            check_both(with_prelude("array<S> arr = [T(), T()]; cast<T&>(arr[1]).t = 44; var e = arr[1]; to_string(e.t)").c_str(), "44", "subscript operand aliases the element");
            check_both(with_prelude("shared_ptr<T> sp = new T(); T v = cast<T>(sp); v.t = 8; to_string(sp.t)").c_str(), "2", "value form deep-copies a handle");
            check_both(with_prelude("T t0 = T(); auto& r = t0; T v = cast<T>(r); v.t = 8; to_string(t0.t) + \",\" + to_string(v.t)").c_str(), "2,8", "value form copies a reference operand");
        });

        test("cast_upcast_and_null", [this, check_both, with_prelude]() {
            check_both(with_prelude("S up = cast<S>(T()); to_string(up.x)").c_str(), "1", "upcast by value");
            check_both(with_prelude("cast<S&>(h.p).x").c_str(), "1", "upcast by reference");
            check_both(with_prelude("shared_ptr<S> sh = cast<shared_ptr<S>>(h.p); to_string(sh.x)").c_str(), "1", "upcast handle");
            check_both("class T { int t = 2; } var n = null; to_string(cast<T>(n) == null) + \",\" + to_string(cast<shared_ptr<T>>(n) == null)", "true,true", "null passes the value and shared_ptr forms");
            check_both("class T { int t = 2; } var n = null; var c = \"\"; try { cast<T&>(n).t; } catch (e) { c = e; } c", "Cannot cast 'null' to 'T&'", "null fails the reference form");
        });

        test("cast_failures_are_catchable_with_exact_text", [this, check_both, with_prelude]() {
            check_both(with_prelude("var c = \"\"; try { var bad = cast<H>(h.p); } catch (e) { c = e; } c").c_str(), "Cannot cast 'T' to 'H'", "unrelated class, value form");
            check_both(with_prelude("var c = \"\"; try { cast<H&>(h.p).p; } catch (e) { c = e; } c").c_str(), "Cannot cast 'T' to 'H&'", "unrelated class, reference form");
            check_both(with_prelude("var c = \"\"; try { var bad = cast<shared_ptr<H>>(h.p); } catch (e) { c = e; } c").c_str(), "Cannot cast 'T' to 'shared_ptr<H>'", "unrelated class, shared_ptr form");
            check_both("class T { int t = 2; } var v = 5; var c = \"\"; try { var bad = cast<T>(v); } catch (e) { c = e; } c", "Cannot cast 'int' to 'T'", "non-object into a class");
            check_both("var v = \"abc\"; var c = \"\"; try { var bad = cast<int>(v); } catch (e) { c = e; } c", "Cannot cast 'string' to 'int'", "unconvertible primitive");
            check_both(with_prelude("var c = \"\"; try { var ok = cast<T>(h.p); c = \"ran\"; } catch (e) { c = e; } c").c_str(), "ran", "admitted casts do not raise");
        });

        test("cast_primitive_conversions", [this, check_both]() {
            check_both("var f = 2.7; to_string(cast<int>(f)) + \":\" + type_of(cast<int>(f))", "2:int", "float -> int truncates");
            check_both("var i = 5; cast<string>(i) + \":\" + type_of(cast<string>(i))", "5:string", "int -> string");
            check_both("var i = 1; to_string(cast<float>(i)) + \":\" + type_of(cast<float>(i))", "1.000000:float", "int -> float widens");
            check_both("var z = 0; to_string(cast<bool>(z))", "false", "-> bool is truthiness");
            check_both("int total = 0; var xs = [1.5, 2.5]; for (auto x : xs) { total += cast<int>(x); } to_string(total)", "3", "cast inside a typed compound store");
            check_both("int n = 0; var v = 7.9; n = cast<int>(v); to_string(n)", "7", "cast<int> is a parse-proven typed store");
        });

        test("cast_char_rules", [this, check_both, both]() {
            // char <-> int/float/string convert through the cast alone (docs/char_semantics.md
            // "Casts"); plain assignment keeps refusing the pair
            check_both("var ch = 'a'; to_string(cast<int>(ch)) + \":\" + type_of(cast<int>(ch))", "97:int", "char -> int is the code point");
            check_both("to_string(cast<int>('\\xC3'))", "195", "char -> int is unsigned (0..255)");
            check_both("var ch = 'a'; to_string(cast<float>(ch)) + \":\" + type_of(cast<float>(ch))", "97.000000:float", "char -> float is the code point widened");
            check_both("var ch = 'a'; cast<string>(ch) + \":\" + type_of(cast<string>(ch))", "a:string", "char -> string is one text unit");
            check_both("var ch = 'a'; to_string(cast<char>(ch)) + \":\" + type_of(cast<char>(ch))", "a:char", "char -> char is identity");
            check_both("var i = 97; to_string(cast<char>(i)) + \":\" + type_of(cast<char>(i))", "a:char", "int -> char in range");
            check_both("to_string(cast<char>(0) == '\\x00') + \",\" + to_string(cast<char>(255) == '\\xFF')", "true,true", "int -> char range ends");
            check_both("var s = \"a\"; to_string(cast<char>(s)) + \":\" + type_of(cast<char>(s))", "a:char", "one-character string -> char");
            check_both("to_string(cast<char>(cast<int>('z')) == 'z')", "true", "char -> int -> char round trip");
            check_both("to_string(cast<char>(cast<string>('q')) == 'q')", "true", "char -> string -> char round trip");
            check_both("to_string(cast<int>(cast<char>(200)))", "200", "int -> char -> int round trip");
            check_both("char c = cast<char>(65); to_string(c) + \":\" + type_of(c)", "A:char", "cast<char> is a parse-proven typed store");
            check_both("int code = cast<int>('a'); to_string(code)", "97", "cast<int> of a char into a typed local");
            check_both("var i = 256; var c = \"\"; try { var bad = cast<char>(i); } catch (e) { c = e; } c", "Cannot cast 'int' to 'char'", "int above 255 fails");
            check_both("var i = -1; var c = \"\"; try { var bad = cast<char>(i); } catch (e) { c = e; } c", "Cannot cast 'int' to 'char'", "negative int fails");
            check_both("var s = \"ab\"; var c = \"\"; try { var bad = cast<char>(s); } catch (e) { c = e; } c", "Cannot cast 'string' to 'char'", "multi-character string fails");
            check_both("var s = \"\"; var c = \"\"; try { var bad = cast<char>(s); } catch (e) { c = e; } c", "Cannot cast 'string' to 'char'", "empty string fails");
            check_both("var b = true; var c = \"\"; try { var bad = cast<char>(b); } catch (e) { c = e; } c", "Cannot cast 'bool' to 'char'", "bool -> char stays refused");
            check_both("var f = 66.5; var c = \"\"; try { var bad = cast<char>(f); } catch (e) { c = e; } c", "Cannot cast 'float' to 'char'", "float -> char stays refused");
            check_both("var i = 97; var c = \"\"; try { cast<char&>(i); } catch (e) { c = e; } c", "Cannot cast 'int' to 'char&'", "reference form never converts");
            check_both("var v = 65; var c = \"\"; try { char bad = v; } catch (e) { c = e; } c", "Cannot initialize 'bad' (declared char) with int", "plain assignment still has no int -> char rule");
            auto [si, sv] = both("char c = 65;");
            check_eq(si, sv, "static rejection parity: char c = 65");
            check(si.find("ERROR: ") == 0 && si.find("cannot initialize 'c' declared 'char' with 'int'") != std::string::npos,
                  "char c = 65 is still rejected before anything runs: " + si);
        });

        test("cast_inside_method_on_field", [this, check_both]() {
            check_both(R"(
                class S { int x = 1; }
                class T : S { int t = 2; }
                class M {
                    S field = null;
                    M() { field = T(); }
                    int peek() { return cast<T&>(field).t; }
                    int poke() { cast<T&>(field).t = 77; return cast<T&>(this.field).t; }
                    T copy() { return cast<T>(field); }
                }
                auto m = M();
                to_string(m.peek()) + "," + to_string(m.poke()) + "," + to_string(m.copy().t)
            )", "2,77,77", "field operand through the implicit and explicit receiver");
        });

        test("typed_field_set_field_cpp_api", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute("class C { int v = 0; var w = 0; }  auto c = C();");
                auto obj = e->execute("c");
                auto instance = obj.as<std::shared_ptr<jai::class_instance>>();
                check_not_null(instance.get());
                uint64_t v_id = e->symbolize("v");
                uint64_t w_id = e->symbolize("w");
                // Converts like '='
                instance->set_field(v_id, jai::script_value(4.7, e.get()));
                check_eq((int64_t)4, e->execute("c.v").as_int());
                // var fields stay dynamic through the C++ API
                instance->set_field(w_id, jai::script_value(std::string("s"), e.get()));
                check_eq(std::string("s"), e->execute("c.w").as<std::string>());
                // No conversion -> throws with the locals-identical text
                bool threw = false;
                try { instance->set_field(v_id, jai::script_value(std::string("bad"), e.get())); }
                catch (const std::exception& ex) {
                    threw = std::string(ex.what()).find("Cannot assign string to field 'v' of C (declared int)") != std::string::npos;
                }
                check(threw, "set_field rejects an inconvertible value");
            }
        });

        test("typed_field_json_roundtrip", [this, check_both]() {
            check_both(R"(
                class P { int hp = 0; float speed = 0.0; string name = ""; }
                auto p = P();
                p.hp = 42; p.speed = 1.5; p.name = "hero";
                var j = to_json(p);
                var q = from_json(j);
                type_of(q.hp) + ":" + to_string(q.hp) + "|" + type_of(q.speed) + ":" + to_string(q.speed) + "|" + q.name
            )", "int:42|float:1.500000|hero", "JSON round-trip preserves typed fields");
            // A float JSON payload into an int field converts on the way in
            check_both(R"(
                class P { int hp = 0; }
                var q = from_json("{\"_type_\": \"P\", \"hp\": 7.9}");
                type_of(q.hp) + ":" + to_string(q.hp)
            )", "int:7", "JSON float into int field truncates");
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

        // ===== TYPED CONTAINER BOUNDARIES ARE ELEMENT-CHECKED (typed-array stage 0) =====
        // An array/map value crossing into a concretely-typed container binding (decl,
        // assign, field write) validates every element with push's rules: exact match
        // passes, int<->float converts, anything else raises. The stored value is
        // retagged to the declared type so later pushes stay enforced. Pre-fix, the
        // same-base fast path admitted ANY array into array<T> — a string could sit
        // behind an array<int> tag (silently breaking the parallel all-primitive proof).

        test("typed_array_decl_validates_elements", [this, both, check_both]() {
            auto [i1, v1] = both("var a = [\"s\"]; array<int> b = a; b[0]");
            check_eq(i1, v1, "string-into-array<int> parity");
            check(i1.find("ERROR") == 0, "laundered string array into array<int> raises");
            auto [i2, v2] = both("var a = [1, \"s\", 3]; array<int> b = a; b.size()");
            check_eq(i2, v2, "mixed-into-array<int> parity");
            check(i2.find("ERROR") == 0, "mixed elements into array<int> raises");
            check_both("var a = [1.5, 2.5]; array<int> b = a; type_of(b[0]) + \":\" + to_string(b[0]) + \",\" + to_string(b[1])",
                       "int:1,2", "float elements truncate into array<int>");
            check_both("var a = [1, 2]; array<float> b = a; type_of(b[0]) + \":\" + to_string(b[0])",
                       "float:1.000000", "int elements widen into array<float>");
            check_both("var a = [1.5]; array<int> b = a; type_of(a[0]) + \":\" + to_string(a[0])",
                       "float:1.500000", "source unchanged after converting decl");
        });

        test("typed_array_literal_init_converts_elements", [this, check_both]() {
            // decl contract (see visit_variable_decl): typed decls convert their
            // initializer exactly like assignment — element payloads included
            check_both("array<float> a = [1, 2, 3]; type_of(a[0]) + \":\" + to_string(a[0])",
                       "float:1.000000", "literal ints become float payloads");
            check_both("array<int> a = [1.5]; type_of(a[0]) + \":\" + to_string(a[0])",
                       "int:1", "literal float truncates into array<int>");
        });

        test("typed_array_assign_validates_elements", [this, both, check_both]() {
            auto [i1, v1] = both("array<int> b = [1]; var a = [\"s\"]; b = a; b[0]");
            check_eq(i1, v1, "assign parity");
            check(i1.find("ERROR") == 0, "assigning string array into array<int> raises");
            check_both("array<int> b = [1]; var a = [2.5]; b = a; to_string(b[0])", "2", "assign converts");
        });

        test("typed_array_field_validates_elements", [this, both, check_both]() {
            auto [i1, v1] = both("class C { array<int> f = []; } auto c = C(); var a = [\"s\"]; c.f = a; c.f[0]");
            check_eq(i1, v1, "field parity");
            check(i1.find("ERROR") == 0, "string array into array<int> field raises");
            // after a valid store the field's declared tag governs pushes (retag at the boundary)
            auto [i2, v2] = both("class C { array<int> f = []; } auto c = C(); var a = [1, 2]; c.f = a; c.f.push(\"s\"); c.f.size()");
            check_eq(i2, v2, "field push-after-store parity");
            check(i2.find("ERROR") == 0, "push string into stored array<int> field raises");
            check_both("class C { array<int> f = []; } auto c = C(); var a = [2.5]; c.f = a; to_string(c.f[0])", "2", "field store converts");
        });

        test("typed_map_decl_validates_values", [this, both, check_both]() {
            auto [i1, v1] = both("var m = {\"k\": \"s\"}; map<string, int> t = m; t.size()");
            check_eq(i1, v1, "map parity");
            check(i1.find("ERROR") == 0, "string values into map<string,int> raises");
            check_both("var m = {\"k\": 2.5}; map<string, int> t = m; to_string(t[\"k\"])", "2", "map value converts");
        });

        test("typed_array_var_ref_keeps_enforcement", [this, both]() {
            // pin: var& shares and PRESERVES the array's tag — stores through the alias
            // stay typed (the demote-on-stamp design leans on this)
            auto [i1, v1] = both("array<int> b = [1]; var& r = b; r.push(\"s\"); b.size()");
            check_eq(i1, v1, "var& push parity");
            check(i1.find("ERROR") == 0, "push through var& alias stays enforced");
        });

        // ===== TYPED FLAT ARRAY STORAGE (typed_array_design.md) =====
        // array<int>/array<float> hold raw primitive buffers by their declared type (the
        // representation, not a mode - Dev ruling 2026-09-02). The contract: typed
        // storage is observationally invisible - every snippet runs on both backends and
        // the outputs pin the plain-semantics answer.

        auto quad = [](const char* src) {
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
            return std::array<std::string, 2>{out[0], out[1]};
        };
        auto check_quad = [this, quad](const char* src, const std::string& expected, const char* what) {
            auto r = quad(src);
            check_eq(expected, r[0], std::string("interp/typed: ") + what);
            check_eq(expected, r[1], std::string("vm/typed: ") + what);
        };
        auto check_quad_error = [this, quad](const char* src, const char* what) {
            auto r = quad(src);
            check_eq(r[0], r[1], std::string("backend parity: ") + what);
            check(r[0].find("ERROR") == 0, std::string("raises: ") + what);
        };

        test("object_buffer_behaviour", [check_quad, check_quad_error]() {
            // array<UserType> is an object slot buffer (kind obj); every row below is the
            // exact pre-change output of both backends (representation change only)
            const std::string K = "class K { int a = 1; K() {} K(int v) { a = v; } string f() { return \"k\" + to_string(a); } } ";
            auto q = [&](const char* tail) { return K + tail; };
            check_quad(q("array<K> ks = [K(1), K(2)]; to_string(ks[0].a) + \",\" + to_string(ks[1].a)").c_str(), "1,2", "reads");
            check_quad(q("array<K> ks = [K(1), K(2)]; ks[1].a = 9; to_string(ks[1].a)").c_str(), "9", "field write through the element");
            check_quad(q("array<K> ks = [K(1), K(2)]; ks[0] = K(5); to_string(ks[0].a)").c_str(), "5", "element store");
            check_quad(q("array<K> ks = [K(1), K(2)]; ks.push(K(3)); to_string(ks.size()) + \":\" + to_string(ks[2].a)").c_str(), "3:3", "push");
            check_quad(q("array<K> ks = [K(1), K(2)]; auto c = ks; c[0].a = 42; to_string(ks[0].a) + \",\" + to_string(c[0].a)").c_str(), "1,42", "copy is a deep copy");
            check_quad(q("array<K> ks = [K(1), K(2)]; int s = 0; for (auto k : ks) { s += k.a; } to_string(s)").c_str(), "3", "range-for by value reads");
            check_quad(q("array<K> ks = [K(1), K(2)]; for (auto k : ks) { k.a = 77; } to_string(ks[0].a + ks[1].a)").c_str(), "3", "range-for by value mutates copies");
            check_quad(q("array<K> ks = [K(1), K(2)]; for (auto& k : ks) { k.a = k.a * 10; } to_string(ks[0].a + ks[1].a)").c_str(), "30", "range-for by reference writes through");
            check_quad(q("array<K> ks = [K(1), K(2)]; ks.reverse(); to_string(ks[0].a)").c_str(), "2", "reverse");
            check_quad(q("array<K> ks = [K(1), K(2)]; ks[1].f()").c_str(), "k2", "method call on an element");
            check_quad(q("array<K> ks = [K(1)]; type_of(ks) + \"|\" + type_of(ks[0])").c_str(), "array|object", "type_of");
            check_quad(q("array<K> ks = [K(1), K(2)]; to_json(ks)").c_str(), "[{\"_type_\":\"K\",\"a\":1},{\"_type_\":\"K\",\"a\":2}]", "json");
            check_quad(q("array<K> a = [K(1)]; array<K> b = a; array<K> c = [K(2)]; to_string(a == b) + \",\" + to_string(a == c)").c_str(), "false,false", "equality is identity");
            check_quad(q("shared_ptr<K> sp = K(7); array<K> ks = [sp]; sp.a = 8; to_string(ks[0].a) + \"|\" + type_of(ks[0])").c_str(), "8|object", "a shared_ptr element shares its instance");
            check_quad(q("shared_ptr<K> sp = K(7); array<K> ks = [sp]; auto c = ks; c[0].a = 9; to_string(ks[0].a) + \",\" + to_string(sp.a)").c_str(), "7,7", "copying an array with a shared element clones it");
            check_quad(q("array<K> ks = [K(1), K(2), K(3)]; to_string(ks.filter([](K k) { return k.a > 1; }).size())").c_str(), "2", "filter");
            check_quad(q("array<K> ks = [K(1)]; ks.clear(); ks.push(K(4)); to_string(ks.size()) + \":\" + to_string(ks[0].a)").c_str(), "1:4", "clear then push");
            check_quad(q("array<K> ks = [K(1)]; var v = ks; v.push(5); to_string(v.size()) + \":\" + type_of(v)").c_str(), "2:array", "var laundering demotes (then anything goes)");
            check_quad(q("var v = [K(1), K(2)]; array<K> ks = v; v[0].a = 50; to_string(ks[0].a) + \",\" + to_string(ks.size())").c_str(), "1,2", "array<K> from a dynamic array copies");
            check_quad(q("function total(array<K> xs) -> int { int s = 0; for (auto x : xs) { s += x.a; } xs[0].a = 100; return s; } array<K> ks = [K(1), K(2)]; to_string(total(ks)) + \",\" + to_string(ks[0].a)").c_str(), "3,1", "by-value parameter");
            check_quad(q("function bump(array<K>& xs) { xs[0].a = 100; xs.push(K(9)); } array<K> ks = [K(1)]; bump(ks); to_string(ks[0].a) + \",\" + to_string(ks.size())").c_str(), "100,2", "by-reference parameter");
            check_quad(q("function mk() -> array<K> { array<K> r = [K(3), K(4)]; return r; } auto got = mk(); to_string(got[1].a) + \"|\" + type_of(got)").c_str(), "4|array", "returned array");
            check_quad(q("array<array<K>> grid = [[K(1)], [K(2), K(3)]]; to_string(grid[1][1].a)").c_str(), "3", "nested arrays stay dynamic outside");
            check_quad(q("array<K> ks = []; ks.push(K(2)); to_string(ks.size()) + \":\" + to_string(ks[0].a)").c_str(), "1:2", "empty then push");
            check_quad(q("coroutine int walk(array<K> xs) { for (auto x : xs) { yield x.a; } return 0; } array<K> ks = [K(5), K(6)]; auto h = walk(ks); to_string(h.resume()) + \",\" + to_string(h.resume())").c_str(), "5,6", "coroutine over an object buffer");
            check_quad(q("array<K> ks = [K(1), K(2)]; int acc = 0; for (int i = 0; i < 1000; i++) { acc += ks[i & 1].a; } to_string(acc)").c_str(), "1500", "hot element field reads");
            check_quad_error(q("class J { int b = 0; } array<K> ks = [K()]; ks.push(J())").c_str(), "another class does not push");
            check_quad_error(q("array<K> ks = [K()]; ks.push(5)").c_str(), "an int does not push");
            check_quad_error(q("class J { int b = 0; } array<K> x = [J()]").c_str(), "another class does not declare");
            check_quad_error(q("class J { int b = 0; } array<K> ks = [K()]; ks[0] = J()").c_str(), "another class does not store");
            check_quad(q("class D : K { } array<D> ds = [D()]; array<K> ks = ds; to_string(ks.size()) + \",\" + to_string(ks[0].a)").c_str(), "1,1", "array<Derived> converts to array<Base> (Dev ruling 2026-09-02: subclasses admitted)");
        });

        test("map_behaviour_battery", [check_quad, check_quad_error]() {
            // Every row is the pre-change output of BOTH backends (scratchpad mapprobes_before.txt,
            // 2026-09-02): the typed-map campaign (typed_array_design.md stage 6) must keep them
            // byte-identical, and the two EDGE rows are rulings for Dev, not behaviour to copy
            check_quad("var m = {\"a\": 1, \"b\": 2}; to_string(m[\"a\"] + m[\"b\"]) + \",\" + to_string(m.size())", "3,2", "literal read + size");
            check_quad("map<string,int> m = {\"a\": 1, \"b\": 2}; to_string(m[\"b\"]) + \"|\" + type_of(m)", "2|map", "typed declaration");
            check_quad("map<int,string> m = {3: \"c\", 1: \"a\", 2: \"b\"}; string s = \"\"; for (auto kv : m) { s += kv.second; } s", "abc", "int keys iterate in key order");
            check_quad("var m = {\"zeta\": 1, \"alpha\": 2, \"mid\": 3}; string s = \"\"; for (auto kv : m) { s += kv.first + \",\"; } s", "alpha,mid,zeta,", "string keys iterate in byte order");
            check_quad("var m = {2: \"i\", \"x\": \"s\", 1.5: \"f\", true: \"b\"}; string s = \"\"; for (auto kv : m) { s += type_of(kv.first) + \",\"; } s", "int,float,string,bool,", "mixed keys order by type rank");
            check_quad("map<string,int> m = {}; m[\"k\"] = 5; m[\"k\"] = m[\"k\"] + 1; to_string(m[\"k\"]) + \",\" + to_string(m.size())", "6,1", "store and re-store");
            check_quad("map<string,int> m = {\"a\": 1}; to_string(m[\"nope\"] == null)", "true", "missing key reads null, no auto-insert");
            check_quad("map<string,int> m = {\"a\": 1}; to_string(m.contains(\"a\")) + \",\" + to_string(m.contains(\"b\"))", "true,false", "contains");
            check_quad("map<string,int> m = {\"a\": 1, \"b\": 2}; m.erase(\"a\"); to_string(m.size()) + \",\" + to_string(m.contains(\"a\"))", "1,false", "erase");
            check_quad("map<string,int> m = {\"b\": 2, \"a\": 1}; to_json(m.keys()) + to_json(m.values())", "[\"a\",\"b\"][1,2]", "keys and values in order");
            check_quad("map<string,int> m = {\"a\": 1}; auto c = m; c[\"a\"] = 9; to_string(m[\"a\"]) + \",\" + to_string(c[\"a\"])", "1,9", "copy is a deep copy");
            check_quad("class K { int a = 1; K() {} K(int v) { a = v; } } map<string,K> m = {\"x\": K(4)}; m[\"x\"].a = 7; to_string(m[\"x\"].a)", "7", "field write through an entry");
            check_quad("class K { int a = 1; K() {} K(int v) { a = v; } } map<string,K> m = {\"x\": K(4)}; auto c = m; c[\"x\"].a = 9; to_string(m[\"x\"].a) + \",\" + to_string(c[\"x\"].a)", "4,9", "object values clone on copy");
            check_quad("map<string,int> m = {\"n\": 1}; m[\"n\"] += 10; to_string(m[\"n\"])", "11", "compound store on an entry");
            check_quad("map<string,int> m = {\"n\": 1}; auto& r = m[\"n\"]; r = 42; to_string(m[\"n\"])", "42", "reference to an entry writes through");
            check_quad("var m = {\"inner\": {\"x\": 1}}; m[\"inner\"][\"x\"] = 2; to_string(m[\"inner\"][\"x\"])", "2", "nested map write");
            check_quad("map<string,int> m = {\"b\": 2, \"a\": 1}; to_json(m)", "{\"a\":1,\"b\":2}", "json in key order");
            check_quad("map<float,int> m = {1.5: 1, 0.5: 2}; string s = \"\"; for (auto kv : m) { s += to_string(kv.second); } s", "21", "float keys order by value");
            check_quad("map<string,int> m = {\"a\": 1, \"b\": 2}; for (auto& kv : m) { kv.second = kv.second * 10; } to_string(m[\"a\"] + m[\"b\"])", "30", "reference range-for writes values");
            check_quad("map<string,int> m = {}; to_string(m.size()) + \",\" + to_string(m.empty())", "0,true", "empty");
            check_quad("function total(map<string,int> mm) -> int { int s = 0; for (auto kv : mm) { s += kv.second; } mm[\"z\"] = 100; return s; } map<string,int> m = {\"a\": 1, \"b\": 2}; to_string(total(m)) + \",\" + to_string(m.size())", "3,2", "by-value parameter");
            check_quad("function grow(map<string,int>& mm) { mm[\"z\"] = 100; } map<string,int> m = {\"a\": 1}; grow(m); to_string(m.size()) + \",\" + to_string(m[\"z\"])", "2,100", "by-reference parameter");
            check_quad("map<string,int> m = {\"a\": 1}; var v = m; v[5] = 2; to_string(v.size()) + \",\" + type_of(v)", "2,map", "var laundering then anything goes");
            check_quad("var v = {\"a\": 1, \"b\": 2}; map<string,int> m = v; v[\"a\"] = 50; to_string(m[\"a\"]) + \",\" + to_string(m.size())", "1,2", "typed map from a dynamic map copies");
            check_quad("map<string,int> a = {\"x\": 1}; map<string,int> b = a; to_string(a == b)", "false", "equality is identity");
            check_quad("map<string,int> m = {\"px\": 3}; int acc = 0; for (int i = 0; i < 1000; i++) { acc += m[\"px\"]; } to_string(acc)", "3000", "constant-key reads");
            check_quad("map<string,int> m = {}; for (int i = 0; i < 50; i++) { m[\"k\" + to_string(i)] = i; } int acc = 0; for (int i = 0; i < 50; i++) { acc += m[\"k\" + to_string(i)]; } to_string(acc) + \",\" + to_string(m.size())", "1225,50", "dynamic-key stores and reads");
            check_quad("map<string,int> m = {\"a\": 1}; m.clear(); m[\"b\"] = 2; to_string(m.size()) + \",\" + to_string(m.contains(\"a\"))", "1,false", "clear then store");
            check_quad("map<string,array<int>> m = {\"a\": [1, 2]}; m[\"a\"].push(3); to_string(m[\"a\"].size()) + \",\" + to_string(m[\"a\"][2])", "3,3", "array value mutates through the entry");
            check_quad("coroutine int walk(map<string,int> mm) { for (auto kv : mm) { yield kv.second; } return 0; } map<string,int> m = {\"a\": 5, \"b\": 6}; auto h = walk(m); to_string(h.resume()) + \",\" + to_string(h.resume())", "5,6", "coroutine over a map");
            check_quad_error("map<string,int> m = {}; m[5] = 1", "key type enforced on a direct store (Dev ruling 2026-09-02)");
            check_quad_error("map<string,int> m = {}; m[\"k\"] = \"str\"", "value type enforced on store");
            check_quad_error("map<string,int> m = {\"a\": \"s\"}", "value type enforced on declaration");
            check_quad("map<string,int> m = {\"n\": 1}; m[\"n\"]++; to_string(m[\"n\"])", "2", "increment of a map entry (Dev ruling 2026-09-02: works like += 1)");
        });

        test("null_compare_battery", [check_quad]() {
            // Null compares (`x == null` / `x != null`) on both backends: every row is the
            // exact pre-lane output of both backends. The vm branch lane answers a null
            // operand from storage (handle_equal's verdict); weak_ptr, cpp-bound and
            // operator-overloaded engines keep the general path
            const std::string P = "class N { int a = 1; shared_ptr<N> next; N other; } class E { int v = 0; bool operator==(E o) { return true; } bool operator!=(E o) { return true; } } class L { shared_ptr<L> left; int f() { if (left == null) { return 1; } return 2; } int g() { if (left != null) { return 1; } return 2; } } auto n = N(); auto e = E(); auto l = L(); var v; int i = 5; float fl = 1.5; string s = \"s\"; char c = 'c'; bool b = true; array<var> arr = [1]; map<string,int> m = {\"a\": 1}; function fn() { return 1; } auto sp = shared_ptr<N>(); weak_ptr<N> w = sp; ";
            auto q = [&](const char* tail) { return P + tail; };
            check_quad(q("n == null").c_str(), "false", "object vs null");
            check_quad(q("n != null").c_str(), "true", "object != null");
            check_quad(q("n.next == null").c_str(), "true", "unset shared_ptr field");
            check_quad(q("n.other == null").c_str(), "true", "typed-null object field");
            check_quad(q("null == null").c_str(), "true", "null == null");
            check_quad(q("null != null").c_str(), "false", "null != null");
            check_quad(q("v == null").c_str(), "true", "unset var");
            check_quad(q("i == null").c_str(), "false", "int vs null");
            check_quad(q("fl != null").c_str(), "true", "float != null");
            check_quad(q("s == null").c_str(), "false", "string vs null");
            check_quad(q("c == null").c_str(), "false", "char vs null (no promotion)");
            check_quad(q("b != null").c_str(), "true", "bool != null");
            check_quad(q("arr == null").c_str(), "false", "array vs null");
            check_quad(q("m != null").c_str(), "true", "map != null");
            check_quad(q("fn == null").c_str(), "false", "function vs null");
            check_quad(q("e == null").c_str(), "false", "class operator== is NOT consulted for a null operand");
            check_quad(q("e != null").c_str(), "true", "class operator!= is NOT consulted for a null operand");
            check_quad(q("null == e").c_str(), "false", "null on the left");
            check_quad(q("l.f()").c_str(), "1", "method body: if (field == null) - the branch lane");
            check_quad(q("l.g()").c_str(), "2", "method body: if (field != null)");
            check_quad(q("l.left = shared_ptr<L>(); l.f()").c_str(), "2", "set shared_ptr field: == null false");
            check_quad(q("l.left = shared_ptr<L>(); l.g()").c_str(), "1", "set shared_ptr field: != null true");
            check_quad(q("sp == null").c_str(), "false", "live shared_ptr");
            check_quad(q("w == null").c_str(), "false", "weak_ptr to a live object (general path)");
            check_quad(q("w != null").c_str(), "true", "live weak_ptr != null");
            check_quad(q("sp = null; w == null").c_str(), "true", "expired weak_ptr reads as null");
            check_quad(q("sp = null; w != null").c_str(), "false", "expired weak_ptr != null");
            check_quad(q("n.next = shared_ptr<N>(); n.next == null").c_str(), "false", "assigned shared_ptr field");
            check_quad(q("n.next = shared_ptr<N>(); n.next != null").c_str(), "true", "assigned shared_ptr field != null");
            check_quad(q("array<var> xs = [N(), null, N()]; int cnt = 0; for (auto x : xs) { if (x != null) { cnt += 1; } } cnt").c_str(), "2", "loop guard != null over mixed elements");
            check_quad(q("array<var> xs = [N(), null, N()]; int cnt = 0; for (auto x : xs) { if (x == null) { cnt += 1; } } cnt").c_str(), "1", "loop guard == null over mixed elements");
            check_quad(q("bool bb = (n == null); bb").c_str(), "false", "value position (binary, not the branch lane)");
            check_quad(q("bool bc = (n.next != null); bc").c_str(), "false", "value position on an unset field");
            check_quad(q("string r = \"\"; try { throw \"boom\"; } catch (err) { r = to_string(err == null) + \",\" + to_string(n == null); } r").c_str(), "false,false", "inside a catch (catch-var gate keeps the general path)");
            check_quad(q("(null == null) == true").c_str(), "true", "nested compare");
        });

        test("null_compare_defers_to_global_operator_overload", [this]() {
            // A host-registered global `==` latches the custom-binary-op gate: the vm lane
            // must step aside so the overload sees `obj == null` exactly like the interpreter
            std::string out[2];
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                jai::stdlib::register_all(e);
                e->add_function("==", [](const jai::script_value&, const jai::script_value&) { return true; });
                out[use_vm ? 1 : 0] = e->execute("class N { int a = 1; } auto n = N(); int hits = 0; if (n == null) { hits += 1; } if (null == null) { hits += 10; } to_string(hits) + \",\" + to_string(n == null)").to_string();
            }
            check_eq(out[0], out[1], "backends agree under a global == overload");
        });

        test("truthiness_battery", [check_quad]() {
            // Object truthiness (`if (x)` / `if (!x)` / ternary) on both backends: rows are
            // the exact pre-change outputs. A class with no to_bool() anywhere in its chain
            // answers from a class-level memo; `!e` in branch position compiles to an
            // inverted jump on the vm (same is_truthy, one op fewer)
            const std::string P = "class T { int a = 1; } class B { int a = 1; bool to_bool() { return a > 5; } } class D : B { int b = 2; } class Q { bool to_bool() { throw \"qbang\"; } } auto t = T(); auto b = B(); auto d = D(); auto sp = shared_ptr<T>(); shared_ptr<T> e; var v; int i0 = 0; float f0 = 0.0; string s0 = \"\"; array<var> a0 = []; auto qq = Q(); ";
            auto q = [&](const char* tail) { return P + tail; };
            check_quad(q("t ? 1 : 2").c_str(), "1", "plain object is truthy");
            check_quad(q("!t").c_str(), "false", "!object");
            check_quad(q("b ? 1 : 2").c_str(), "2", "to_bool() decides (false)");
            check_quad(q("!b").c_str(), "true", "!object with to_bool() false");
            check_quad(q("b.a = 9; b ? 1 : 2").c_str(), "1", "to_bool() decides (true)");
            check_quad(q("b.a = 9; !b").c_str(), "false", "!object with to_bool() true");
            check_quad(q("d ? 1 : 2").c_str(), "2", "inherited to_bool() (parent chain)");
            check_quad(q("!d").c_str(), "true", "!derived with inherited to_bool()");
            check_quad(q("sp ? 1 : 2").c_str(), "1", "live shared_ptr is truthy");
            check_quad(q("!sp").c_str(), "false", "!live shared_ptr");
            check_quad(q("e ? 1 : 2").c_str(), "2", "unset shared_ptr is falsy");
            check_quad(q("!e").c_str(), "true", "!unset shared_ptr");
            check_quad(q("v ? 1 : 2").c_str(), "2", "unset var is falsy");
            check_quad(q("!v").c_str(), "true", "!unset var");
            check_quad(q("int r = 0; if (t) { r += 1; } if (!t) { r += 10; } if (!e) { r += 100; } if (e) { r += 1000; } if (!b) { r += 10000; } r").c_str(), "10101", "if guards: object, !object, !unset shared_ptr, unset shared_ptr, !to_bool object");
            check_quad(q("int w = 0; while (!e) { w += 1; e = shared_ptr<T>(); } w").c_str(), "1", "while (!ptr) exits once assigned");
            check_quad(q("to_string(!i0) + \",\" + to_string(!f0) + \",\" + to_string(!s0) + \",\" + to_string(!a0) + \",\" + to_string(!'c') + \",\" + to_string(!true) + \",\" + to_string(!null)").c_str(), "true,true,true,true,false,false,true", "!primitive value forms");
            check_quad(q("int r2 = 0; if (!i0) { r2 += 1; } if (!f0) { r2 += 2; } if (!s0) { r2 += 4; } if (!a0) { r2 += 8; } if (!'c') { r2 += 16; } if (!null) { r2 += 32; } if (!(t && e)) { r2 += 64; } if (!t || !e) { r2 += 128; } r2").c_str(), "239", "if (!x) over every primitive kind, && and || operands");
            check_quad(q("string msg = \"none\"; try { if (!qq) { msg = \"neg\"; } else { msg = \"pos\"; } } catch (err) { msg = err; } msg").c_str(), "qbang", "a throwing to_bool() under if (!x) propagates");
            check_quad(q("string msg = \"none\"; try { if (qq) { msg = \"pos2\"; } else { msg = \"neg2\"; } } catch (err) { msg = err; } msg").c_str(), "qbang", "a throwing to_bool() under if (x) propagates");
            check_quad(q("class H { bool to_bool() { return false; } } auto h = H(); int r3 = 0; if (!h) { r3 += 1; } if (h) { r3 += 2; } r3").c_str(), "1", "class declared after the battery prefix: memo per class");
        });

        test("field_store_battery", [check_quad]() {
            // Bare field stores inside methods (`i = x`, the ctor-body and creature-state
            // shape) on both backends: rows are the exact pre-lane outputs. The vm's store-side
            // this-field cache lands typed enforcement + the store on the cell directly;
            // texts, conversions and value semantics must not move
            const std::string P = "class F { int i = 1; float f = 1.5; string s = \"a\"; bool b = false; auto a = 10; var v = 0; array<int> arr = [1, 2]; F other; shared_ptr<F> sp; static int st = 5; string setInt(var x) { string r = \"ok\"; try { i = x; } catch (err) { r = err; } return r; } string setFloat(var x) { string r = \"ok\"; try { f = x; } catch (err) { r = err; } return r; } string setStr(var x) { string r = \"ok\"; try { s = x; } catch (err) { r = err; } return r; } string setBool(var x) { string r = \"ok\"; try { b = x; } catch (err) { r = err; } return r; } string setAuto(var x) { string r = \"ok\"; try { a = x; } catch (err) { r = err; } return r; } string setVar(var x) { string r = \"ok\"; try { v = x; } catch (err) { r = err; } return r; } string setArr(var x) { string r = \"ok\"; try { arr = x; } catch (err) { r = err; } return r; } string setOther(var x) { string r = \"ok\"; try { other = x; } catch (err) { r = err; } return r; } string setSp(var x) { string r = \"ok\"; try { sp = x; } catch (err) { r = err; } return r; } int loopStore(int n) { for (int k = 0; k < n; ++k) { i = k; f = k; } return i; } int chainStore() { i = 7; int t = i; i = t + 1; return i; } int storeResult() { int r = (i = 42); return r; } float storeResultConv() { float r = (i = 3.9); return r; } string setStatic(var x) { string r = \"ok\"; try { st = x; } catch (err) { r = err; } return r; } string dump() { return to_string(i) + \"|\" + to_string(f) + \"|\" + s + \"|\" + to_string(b) + \"|\" + to_string(a) + \"|\" + to_string(v) + \"|\" + to_string(arr.size()) + \"|\" + to_string(other == null) + \"|\" + to_string(sp == null); } } class G : F { int g = 2; string setInherited(var x) { string r = \"ok\"; try { i = x; } catch (err) { r = err; } return r; } string setG(var x) { string r = \"ok\"; try { g = x; } catch (err) { r = err; } return r; } } auto o = F(); ";
            auto q = [&](const char* tail) { return P + tail; };
            check_quad(q("o.setInt(5) + \" \" + o.dump()").c_str(), "ok 5|1.500000|a|false|10|0|2|true|true", "typed int field, int");
            check_quad(q("o.setInt(2.7) + \" \" + o.dump()").c_str(), "ok 2|1.500000|a|false|10|0|2|true|true", "typed int field, float truncates");
            check_quad(q("o.setInt(\"x\") + \" \" + o.dump()").c_str(), "Cannot assign string to field 'i' of F (declared int) 1|1.500000|a|false|10|0|2|true|true", "typed int field, string raises, field untouched");
            check_quad(q("o.setInt(true) + \" \" + o.dump()").c_str(), "ok 1|1.500000|a|false|10|0|2|true|true", "typed int field, bool converts");
            check_quad(q("o.setInt(null) + \" \" + o.dump()").c_str(), "Cannot assign null to field 'i' of F (declared int) 1|1.500000|a|false|10|0|2|true|true", "typed int field, null raises");
            check_quad(q("o.setFloat(2) + \" \" + o.dump()").c_str(), "ok 1|2.000000|a|false|10|0|2|true|true", "typed float field, int promotes");
            check_quad(q("o.setFloat(\"q\") + \" \" + o.dump()").c_str(), "Cannot assign string to field 'f' of F (declared float) 1|1.500000|a|false|10|0|2|true|true", "typed float field, string raises");
            check_quad(q("o.setStr(12) + \" \" + o.dump()").c_str(), "ok 1|1.500000|12|false|10|0|2|true|true", "typed string field, int converts via to_string");
            check_quad(q("o.setBool(1) + \" \" + o.dump()").c_str(), "ok 1|1.500000|a|true|10|0|2|true|true", "typed bool field, int converts");
            check_quad(q("o.setAuto(2.5) + \" \" + o.dump()").c_str(), "ok 1|1.500000|a|false|2|0|2|true|true", "auto field infers int from its initializer, float truncates");
            check_quad(q("o.setAuto(\"str\") + \" \" + o.dump()").c_str(), "Cannot assign string to field 'a' of F (declared int) 1|1.500000|a|false|10|0|2|true|true", "auto field rejects a string");
            check_quad(q("o.setVar(\"anything\") + \" \" + o.setVar([1,2,3]) + \" \" + o.dump()").c_str(), "ok ok 1|1.500000|a|false|10|[array]|2|true|true", "var field takes anything");
            check_quad(q("o.setArr([4,5,6]) + \" \" + o.setArr([\"a\"]) + \" \" + o.setArr(7) + \" \" + o.dump()").c_str(), "ok Cannot assign array to field 'arr' of F (declared array<int>) Cannot assign int to field 'arr' of F (declared array<int>) 1|1.500000|a|false|10|0|3|true|true", "typed array field: typed array ok, wrong element type and scalar raise");
            check_quad(q("auto o2 = F(); string r1 = o.setOther(o2); o2.i = 99; r1 + \" \" + to_string(o.other.i) + \" \" + o.setOther(5)").c_str(), "ok 1 Cannot assign int to field 'other' of F (declared F)", "object field: by-value copy, scalar raises");
            check_quad(q("o.setSp(shared_ptr<F>()) + \" \" + o.setSp(F()) + \" \" + o.setSp(3) + \" \" + o.dump()").c_str(), "ok ok Cannot assign int to field 'sp' of F (declared shared_ptr<F>) 1|1.500000|a|false|10|0|2|true|false", "shared_ptr field: handle, value, scalar");
            check_quad(q("to_string(o.loopStore(1000)) + \" \" + o.dump()").c_str(), "999 999|999.000000|a|false|10|0|2|true|true", "1000 stores at one site (the cached lane)");
            check_quad(q("to_string(o.chainStore()) + \",\" + to_string(o.storeResult()) + \",\" + to_string(o.storeResultConv()) + \",\" + to_string(o.i)").c_str(), "8,42,3.000000,3", "store as expression: value is the stored (converted) value");
            check_quad(q("o.setStatic(9) + \" \" + o.setStatic(\"bad\") + \" \" + to_string(F::st)").c_str(), "ok Cannot assign string to static 'st' of F (declared int) 9", "static field stores enforce like instance fields (not the this-field lane)");
            check_quad(q("auto g = G(); g.setInherited(11) + \" \" + g.setInherited(\"no\") + \" \" + g.dump() + \" \" + g.setG(3.2) + \" \" + to_string(g.g)").c_str(), "ok Cannot assign string to field 'i' of G (declared int) 11|1.500000|a|false|10|0|2|true|true ok 3", "inherited field through a derived instance");
            check_quad(q("array<F> fs = [F(), G()]; string acc = \"\"; for (auto& e : fs) { acc = acc + e.setInt(1) + \",\"; } acc = acc + to_string(fs[0].i) + to_string(fs[1].i); acc").c_str(), "ok,ok,11", "one site, two classes (polymorphic site re-arms)");
            check_quad(q("class H { int i = 0; int bump() { for (int k = 0; k < 5; ++k) { i += 1; } return i; } } auto h = H(); to_string(h.bump()) + \" \" + to_string(h.bump())").c_str(), "5 10", "compound field stores");
            check_quad(q("class R { int x = 1; int y = 2; void swap() { int t = x; x = y; y = t; } string dump() { return to_string(x) + \",\" + to_string(y); } } auto r = R(); r.swap(); r.swap(); r.swap(); r.dump()").c_str(), "2,1", "two fields swapped through a local");
            check_quad(q("class S { array<int> xs = [1]; void put(array<int> src) { xs = src; } int total() { int t = 0; for (auto q : xs) { t += q; } return t; } } auto sobj = S(); array<int> mine = [1, 2, 3]; sobj.put(mine); mine.push(100); to_string(sobj.total()) + \" \" + to_string(mine.size())").c_str(), "6 4", "lvalue rhs clones (value semantics)");
            check_quad(q("class Q { int i = 1; string setThis(var x) { string r = \"ok\"; try { this.i = x; } catch (err) { r = err; } return r; } } auto q = Q(); q.setThis(4) + \" \" + to_string(q.i) + \" \" + q.setThis(\"z\") + \" \" + to_string(q.i)").c_str(), "ok 4 Cannot assign string to field 'i' of Q (declared int) 4", "explicit this.field (member path, not the bare lane)");
            check_quad(q("class W { weak_ptr<F> w; string setW(var x) { string r = \"ok\"; try { w = x; } catch (err) { r = err; } return r; } } auto w = W(); auto keep = shared_ptr<F>(); w.setW(keep) + \" \" + w.setW(F()) + \" \" + to_string(w.w == null)").c_str(), "Cannot assign F to field 'w' of W (declared weak_ptr<F>) Cannot assign F to field 'w' of W (declared weak_ptr<F>) true", "weak_ptr field: handle ok, value raises");
        });

        auto quad_mode = [](const char* src, check_mode mode) {
            std::string out[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execution_budget(0);
                e->static_checking(mode);
                jai::stdlib::register_all(e);
                try { out[idx] = e->execute(src).to_string(); }
                catch (const std::exception& ex) { out[idx] = std::string("ERROR: ") + ex.what(); }
                ++idx;
            }
            return std::array<std::string, 2>{out[0], out[1]};
        };
        auto check_quad_off = [this, quad_mode](const char* src, const std::string& expected, const char* what) {
            auto r = quad_mode(src, check_mode::off);
            check_eq(expected, r[0], std::string("interp/off: ") + what);
            check_eq(expected, r[1], std::string("vm/off: ") + what);
        };
        auto check_quad_error_text = [this, quad_mode](const char* src, const std::string& needle, const char* what) {
            auto r = quad_mode(src, check_mode::off);
            check_eq(r[0], r[1], std::string("backend parity: ") + what);
            check(r[0].find("ERROR") == 0, std::string("raises: ") + what);
            check(r[0].find(needle) != std::string::npos, std::string("text: ") + what + " (got: " + r[0] + ")");
        };

        test("static_store_battery", [check_quad]() {
            // Every supported static-store spelling (C::s anywhere; bare s in instance methods,
            // constructors and static methods) runs the instance-field store table on both
            // backends: same conversions, same text, the static untouched on a miss
            const std::string P = "class C { static int i = 1; static float f = 1.5; static string s = \"a\"; static bool b = false; static auto a = 10; static var v = 0; static array<int> arr = [1, 2]; static C other; static shared_ptr<C> sp; static weak_ptr<C> w; int x = 0; C() {} C(var val) { i = val; } string setQ(var val) { string r = \"ok\"; try { i = val; } catch (err) { r = err; } return r; } static string setS(var val) { string r = \"ok\"; try { i = val; } catch (err) { r = err; } return r; } string dump() { return to_string(i) + \"|\" + to_string(f) + \"|\" + s + \"|\" + to_string(b) + \"|\" + to_string(a) + \"|\" + to_string(v) + \"|\" + to_string(arr.size()) + \"|\" + to_string(other == null) + \"|\" + to_string(sp == null) + \"|\" + to_string(w == null); } } function setI(var val) { string r = \"ok\"; try { C::i = val; } catch (err) { r = err; } return r; } function setF(var val) { string r = \"ok\"; try { C::f = val; } catch (err) { r = err; } return r; } function setStr(var val) { string r = \"ok\"; try { C::s = val; } catch (err) { r = err; } return r; } function setB(var val) { string r = \"ok\"; try { C::b = val; } catch (err) { r = err; } return r; } function setA(var val) { string r = \"ok\"; try { C::a = val; } catch (err) { r = err; } return r; } function setV(var val) { string r = \"ok\"; try { C::v = val; } catch (err) { r = err; } return r; } function setArr(var val) { string r = \"ok\"; try { C::arr = val; } catch (err) { r = err; } return r; } function setOther(var val) { string r = \"ok\"; try { C::other = val; } catch (err) { r = err; } return r; } function setSp(var val) { string r = \"ok\"; try { C::sp = val; } catch (err) { r = err; } return r; } function setW(var val) { string r = \"ok\"; try { C::w = val; } catch (err) { r = err; } return r; } auto o = C(); ";
            auto q = [&](const char* tail) { return P + tail; };
            const std::string base = "1|1.500000|a|false|10|0|2|true|true|true";
            check_quad(q("setI(5) + \" \" + o.dump()").c_str(), "ok 5|1.500000|a|false|10|0|2|true|true|true", "C::i, int");
            check_quad(q("setI(2.7) + \" \" + o.dump()").c_str(), "ok 2|1.500000|a|false|10|0|2|true|true|true", "C::i, float truncates");
            check_quad(q("setI(\"x\") + \" \" + o.dump()").c_str(), "Cannot assign string to static 'i' of C (declared int) " + base, "C::i, string raises, static untouched");
            check_quad(q("setI(true) + \" \" + o.dump()").c_str(), "ok 1|1.500000|a|false|10|0|2|true|true|true", "C::i, bool converts");
            check_quad(q("setI(null) + \" \" + o.dump()").c_str(), "Cannot assign null to static 'i' of C (declared int) " + base, "C::i, null raises");
            check_quad(q("setI([1]) + \" \" + o.dump()").c_str(), "Cannot assign array to static 'i' of C (declared int) " + base, "C::i, array raises");
            check_quad(q("setI(C()) + \" \" + o.dump()").c_str(), "Cannot assign C to static 'i' of C (declared int) " + base, "C::i, object raises");
            check_quad(q("o.setQ(5) + \" \" + o.setQ(2.7) + \" \" + o.setQ(\"x\") + \" \" + to_string(C::i)").c_str(), "ok ok Cannot assign string to static 'i' of C (declared int) 2", "bare static in an instance method");
            check_quad(q("C::setS(6) + \" \" + C::setS(3.9) + \" \" + C::setS(\"x\") + \" \" + to_string(C::i)").c_str(), "ok ok Cannot assign string to static 'i' of C (declared int) 3", "bare static in a static method");
            check_quad(q("C(7); string r = \"ok\"; try { C(\"x\"); } catch (err) { r = err; } r + \" \" + to_string(C::i)").c_str(), "Cannot assign string to static 'i' of C (declared int) 7", "bare static in a constructor");
            check_quad(q("auto lam = [](var val) { string r = \"ok\"; try { C::i = val; } catch (err) { r = err; } return r; }; lam(\"x\") + \" \" + lam(3) + \" \" + to_string(C::i)").c_str(), "Cannot assign string to static 'i' of C (declared int) ok 3", "C::i inside a lambda");
            check_quad(q("var vv = 2.9; C::i = vv; to_string(C::i) + \"|\" + type_of(C::i)").c_str(), "2|int", "C::i at top level keeps the declared type");
            check_quad(q("setF(2) + \" \" + setF(\"q\") + \" \" + o.dump()").c_str(), "ok Cannot assign string to static 'f' of C (declared float) 1|2.000000|a|false|10|0|2|true|true|true", "float static: int promotes, string raises");
            check_quad(q("setStr(12) + \" \" + setStr(null) + \" \" + setStr(C()) + \" \" + o.dump()").c_str(), "ok ok ok 1|1.500000|[object]|false|10|0|2|true|true|true", "string static takes anything through to_string (the instance-field row)");
            check_quad(q("setB(1) + \" \" + o.dump()").c_str(), "ok 1|1.500000|a|true|10|0|2|true|true|true", "bool static: int converts");
            check_quad(q("setA(2.5) + \" \" + setA(\"str\") + \" \" + o.dump()").c_str(), "ok Cannot assign string to static 'a' of C (declared int) 1|1.500000|a|false|2|0|2|true|true|true", "auto static infers int from its initializer");
            check_quad(q("setV(\"anything\") + \" \" + setV([1, 2, 3]) + \" \" + o.dump()").c_str(), "ok ok 1|1.500000|a|false|10|[array]|2|true|true|true", "var static takes anything");
            check_quad(q("setArr([4, 5, 6]) + \" \" + setArr([\"a\"]) + \" \" + setArr(7) + \" \" + o.dump()").c_str(), "ok Cannot assign array to static 'arr' of C (declared array<int>) Cannot assign int to static 'arr' of C (declared array<int>) 1|1.500000|a|false|10|0|3|true|true|true", "array<int> static: typed array ok, wrong element type and scalar raise");
            check_quad(q("auto o2 = C(); string r1 = setOther(o2); o2.x = 99; r1 + \" \" + to_string(C::other.x) + \" \" + setOther(5)").c_str(), "ok 0 Cannot assign int to static 'other' of C (declared C)", "object static: by-value copy, scalar raises");
            check_quad(q("setSp(shared_ptr<C>()) + \" \" + setSp(C()) + \" \" + setSp(3) + \" \" + o.dump()").c_str(), "ok ok Cannot assign int to static 'sp' of C (declared shared_ptr<C>) 1|1.500000|a|false|10|0|2|true|false|true", "shared_ptr static: handle, value, scalar");
            check_quad(q("auto keep = shared_ptr<C>(); setW(keep) + \" \" + setW(C()) + \" \" + o.dump()").c_str(), "Cannot assign C to static 'w' of C (declared weak_ptr<C>) Cannot assign C to static 'w' of C (declared weak_ptr<C>) " + base, "weak_ptr static: the instance-field rows");
            check_quad("class P { int v = 1; } class S { static shared_ptr<P> p; } auto h = shared_ptr<P>(); S::p = h; h.v = 5; to_string(S::p.v)", "5", "shared_ptr static shares the handle on both backends");
            check_quad(q("int r = (C::i = 42); to_string(r)").c_str(), "42", "store as expression");
            check_quad(q("float r = (C::i = 3.9); to_string(r) + \"|\" + to_string(C::i)").c_str(), "3.000000|3", "store as expression yields the stored (converted) value");
        });

        test("static_compound_battery", [check_quad, check_quad_error]() {
            // `C::s op= v` and ++/-- on a class-qualified static are lvalues on both backends:
            // C::s = T(C::s op v) with the compound texts; the bare forms keep working
            const std::string P = "class C { static int i = 1; static float f = 1.5; static string s = \"a\"; static var v = 1; string bumpQ() { i += 2; i++; ++i; return to_string(i); } static string bumpS() { i += 2; i++; ++i; return to_string(i); } } function L(var x) { return x; } ";
            auto q = [&](const char* tail) { return P + tail; };
            check_quad(q("C::i += 1; to_string(C::i)").c_str(), "2", "+=");
            check_quad(q("C::i -= 1; to_string(C::i)").c_str(), "0", "-=");
            check_quad(q("C::i *= 2; to_string(C::i)").c_str(), "2", "*=");
            check_quad(q("C::i = 8; C::i /= 2; to_string(C::i) + \"|\" + type_of(C::i)").c_str(), "4|int", "/= converts back to the declared int");
            check_quad(q("C::i = 7; C::i %= 3; to_string(C::i)").c_str(), "1", "%=");
            check_quad(q("C::i += 1.5; to_string(C::i) + \"|\" + type_of(C::i)").c_str(), "2|int", "int static += float truncates back to int");
            check_quad(q("C::v += 1.5; to_string(C::v) + \"|\" + type_of(C::v)").c_str(), "2.500000|float", "var static += float becomes float");
            check_quad(q("C::f *= 2; to_string(C::f)").c_str(), "3.000000", "float static *=");
            check_quad(q("C::s += \"b\"; C::s").c_str(), "ab", "string static concat");
            check_quad(q("auto old = C::i++; to_string(old) + \",\" + to_string(C::i)").c_str(), "1,2", "postfix ++ yields the old value");
            check_quad(q("auto nw = ++C::i; to_string(nw) + \",\" + to_string(C::i)").c_str(), "2,2", "prefix ++ yields the new value");
            check_quad(q("C::i--; --C::i; to_string(C::i)").c_str(), "-1", "-- both forms");
            check_quad(q("auto o = C(); o.bumpQ() + \" \" + C::bumpS() + \" \" + to_string(C::i)").c_str(), "5 9 9", "bare compound and ++ in instance and static methods");
            check_quad(q("for (int k = 0; k < 1000; k++) { C::i += 1; } to_string(C::i)").c_str(), "1001", "1000 compound stores at one site");
            check_quad("class K { int x = 1; K(int v) { x = v; } function operator+(int n) -> K { return K(x + n); } } class H { static K k = K(1); } H::k += 5; to_string(H::k.x)", "6", "object static compound dispatches operator+");
            check_quad(q("string r = \"ok\"; try { C::i += L(\"x\"); } catch (err) { r = err; } r + \" \" + to_string(C::i)").c_str(), "Invalid operands for += 1", "+= string into an int static raises the compound text, static untouched");
            check_quad(q("string r = \"ok\"; try { C::i /= L(0); } catch (err) { r = err; } r").c_str(), "Division by zero", "/= 0");
            check_quad(q("string r = \"ok\"; try { C::i %= L(0); } catch (err) { r = err; } r").c_str(), "Modulo by zero", "%= 0");
            check_quad(q("C::i = 9223372036854775807; string r = \"ok\"; try { C::i++; } catch (err) { r = err; } r + \" \" + to_string(C::i)").c_str(), "Integer overflow in '+=' 9223372036854775807", "++ overflow raises, static untouched");
            check_quad_error(q("C::s++").c_str(), "++ on a string static");
            check_quad_error(q("C::nosuch = 1").c_str(), "store to a missing static");
            check_quad_error(q("C::nosuch += 1").c_str(), "compound to a missing static");
            check_quad_error(q("Nope::x = 1").c_str(), "store to a missing class");
        });

        test("static_definition_typing", [check_quad, check_quad_error, check_quad_error_text]() {
            // A static's declared type is recorded at definition and its initializer converts
            // through the field table; a missing initializer is a readable typed null
            check_quad("class C { static float f = 3; } type_of(C::f) + \" \" + to_string(C::f)", "float 3.000000", "static float from an int initializer");
            check_quad("class C { static string t = 5; } C::t + \"|\" + type_of(C::t)", "5|string", "static string from an int initializer");
            check_quad("class C { static int n = true; } to_string(C::n) + \"|\" + type_of(C::n)", "1|int", "static int from a bool initializer");
            check_quad_error("function L(var x) { return x; } class C { static int s = L(\"x\"); }", "an unconvertible static initializer raises at definition");
            check_quad_error_text("function L(var x) { return x; } class C { static int s = L(\"x\"); }", "Cannot assign string to static 's' of C (declared int)", "the definition raises the store text");
            check_quad("class C { static int s; } to_string(C::s == null)", "true", "typed static without an initializer reads null");
            check_quad("class C { static var v = null; } to_string(C::v == null)", "true", "null var static reads");
            check_quad("class P { int a = 0; } class C { static shared_ptr<P> p; } to_string(C::p == null)", "true", "null shared_ptr static reads");
            check_quad("class P { int a = 0; } class C { static P obj; } to_string(C::obj == null)", "true", "null object static reads");
            check_quad("function L(var x) { return x; } class C { static int s; } C::s = L(4); string r = \"ok\"; try { C::s = L(\"x\"); } catch (err) { r = err; } to_string(C::s) + \" \" + r", "4 Cannot assign string to static 's' of C (declared int)", "a null-initialized static still enforces its declared type");
            check_quad("function L(var x) { return x; } class C { static auto a = 2; } C::a = L(2.5); string r = \"ok\"; try { C::a = L(\"str\"); } catch (err) { r = err; } to_string(C::a) + \" \" + r", "2 Cannot assign string to static 'a' of C (declared int)", "auto static enforces the inferred int");
        });

        test("static_spellings_rejected", [check_quad_error, check_quad_off, check_quad_error_text]() {
            // `C.s` and `this.s` are not static spellings: their read and write errors stay,
            // byte-identical across backends (the write raises a STRING, never a bool)
            const std::string P = "class C { static int s = 0; int q = 1; string viaThis() { string r = \"ok\"; try { this.s = 1; } catch (err) { r = type_of(err) + \":\" + to_string(err); } return r; } string readThis() { string r = \"ok\"; try { int t = this.s; } catch (err) { r = to_string(err); } return r; } } ";
            check_quad_error("class C { static int s = 0; } C.s", "dot read on a class name");
            check_quad_off("class C { static int s = 0; } string r = \"\"; try { C.s = 8; } catch (err) { r = type_of(err) + \":\" + to_string(err); } r + \" \" + to_string(C::s)", "string:Cannot assign to member of non-object type 0", "dot store on a class name");
            check_quad_error("class C { static int s = 0; } C.s += 1", "dot compound on a class name");
            check_quad_error("class C { static int s = 0; } C.s++", "dot ++ on a class name");
            check_quad_off((P + "auto o = C(); o.viaThis() + \" \" + to_string(C::s)").c_str(), "string:Cannot assign to 's': C has no such member 0", "this.s store");
            check_quad_off((P + "auto o = C(); o.readThis()").c_str(), "C has no member 's'", "this.s read");
            check_quad_error_text("class S { private: static int counter = 0; public: static int peek() { return counter; } } S::counter = 5", "Cannot access private member 'counter' of class 'S'", "private static store is refused before any conversion");
        });

        test("increment_targets_battery", [check_quad, check_quad_error]() {
            // Dev ruling 2026-09-02: ++/-- work on every lvalue, not only identifiers. Non-
            // identifier targets desugar at parse time to `+= 1` / `-= 1` (the compound
            // machinery both backends share); postfix yields the exact old value
            check_quad("array<int> a = [1, 2]; a[0]++; to_string(a[0])", "2", "postfix ++ on a typed element");
            check_quad("array<int> a = [1, 2]; ++a[1]; to_string(a[1])", "3", "prefix ++ on a typed element");
            check_quad("array<int> a = [5]; a[0]--; to_string(a[0])", "4", "postfix -- on a typed element");
            check_quad("var a = [1.5]; a[0]++; to_string(a[0])", "2.500000", "++ on a float element");
            check_quad("var a = [7]; a[0]++; to_string(a[0])", "8", "++ on a dynamic element");
            check_quad("array<int> a = [5]; auto x = a[0]++; to_string(x) + \",\" + to_string(a[0])", "5,6", "postfix in expression position yields the old value");
            check_quad("array<int> a = [5]; auto y = ++a[0]; to_string(y) + \",\" + to_string(a[0])", "6,6", "prefix in expression position yields the new value");
            check_quad("array<float> f = [9007199254740994.0]; auto o = f[0]++; to_string(o == 9007199254740994.0) + \",\" + to_string(f[0] == 9007199254740995.0 || f[0] == 9007199254740996.0)", "true,true", "postfix old value is exact for large floats");
            check_quad("class K { int a = 1; } auto k = K(); k.a++; to_string(k.a)", "2", "postfix ++ on a member");
            check_quad("class K { int a = 1; } auto k = K(); ++k.a; to_string(k.a)", "2", "prefix ++ on a member");
            check_quad("class K { int a = 1; } auto k = K(); auto o = k.a++; to_string(o) + \",\" + to_string(k.a)", "1,2", "postfix member yields the old value");
            check_quad("class K { int a = 1; void bump() { this.a++; } } auto k = K(); k.bump(); to_string(k.a)", "2", "explicit this member");
            check_quad("class K { int a = 1; void bump() { a++; } } auto k = K(); k.bump(); to_string(k.a)", "2", "implicit this member (unchanged path)");
            check_quad("var m = {\"n\": 1}; m[\"n\"]++; to_string(m[\"n\"])", "2", "map entry");
            check_quad("map<string,int> m = {\"n\": 1}; auto o = m[\"n\"]++; to_string(o) + \",\" + to_string(m[\"n\"])", "1,2", "map entry postfix yields the old value");
            check_quad("var a = [[1]]; a[0][0]++; to_string(a[0][0])", "2", "nested element");
            check_quad("class K { array<int> v = [1]; } auto k = K(); k.v[0]++; to_string(k.v[0])", "2", "member-index element");
            check_quad("int n = 5; auto x = n++; auto y = ++n; to_string(x) + \",\" + to_string(y) + \",\" + to_string(n)", "5,7,7", "identifiers keep their path");
            check_quad("array<int> a = [1, 2, 3]; for (int i = 0; i < 3; i++) { a[i]++; } to_string(a[0] + a[1] + a[2])", "9", "loop over elements");
            check_quad_error("var a = [\"s\"]; a[0]++", "++ on a string element raises the same text on both backends");
            check_quad_error("var m = {}; m[\"missing\"]++", "++ on a missing map entry raises the same text on both backends");
            check_quad_error("5++", "++ on a literal raises the same text on both backends");
            check_quad_error("array<int> a = [9223372036854775807]; a[0]++", "++ overflow on a typed element follows the overflow policy (checked build raises)");
        });

        test("map_key_types_battery", [check_quad, check_quad_error]() {
            // Dev ruling 2026-09-02: the declared key type is enforced on direct stores; map<var,V>
            // takes any key; an `auto` key or value slot infers from a uniform literal (mixed = error)
            // and stays dynamic for an empty literal - the array rule applied to maps
            check_quad("map<var,int> m = {}; m[5] = 1; m[\"x\"] = 2; to_string(m.size())", "2", "map<var,V> takes any key");
            check_quad("map<auto,int> m = {}; m[\"x\"] = 1; m[2] = 3; to_string(m.size())", "2", "map<auto,V> with an empty literal stays dynamic");
            check_quad("map<auto,int> m = {5: 1}; m[6] = 2; to_string(m.size()) + \",\" + to_string(m[6])", "2,2", "map<auto,V> infers an int key from a uniform literal");
            check_quad("map<string,auto> m = {\"a\": 1}; m[\"b\"] = 2; to_string(m[\"b\"])", "2", "map<K,auto> infers an int value");
            check_quad("auto m = {\"a\": 1, \"b\": 2}; m[\"c\"] = 3; to_string(m.size())", "3", "auto with a uniform map literal infers map<string,int>");
            check_quad("map<auto,auto> m = {1.5: \"x\"}; m[2.5] = \"y\"; to_string(m.size())", "2", "both slots infer");
            check_quad("map<auto,int> m = {5: 1}; type_of(m)", "map", "the inferred map is still a map");
            check_quad_error("map<auto,int> m = {5: 1}; m[\"x\"] = 1", "an inferred int key rejects a string key");
            check_quad_error("map<auto,int> m = {5: 1, \"hi\": 1}", "mixed keys under an auto key slot raise");
            check_quad_error("map<string,auto> m = {\"a\": 1}; m[\"b\"] = \"s\"", "an inferred int value rejects a string value");
            check_quad_error("auto m = {\"a\": 1}; m[5] = 1", "auto-inferred map<string,int> rejects an int key");
            check_quad_error("map<int,int> m = {}; m[1.5] = 1", "keys never coerce: a float key into an int map raises");
        });

        test("subclass_admission_battery", [check_quad, check_quad_error]() {
            // Dev ruling 2026-09-02: class-typed arrays admit the class and its subclasses (exact
            // ids compare first; the parent-chain walk runs only on a name mismatch); each
            // slot keeps its own class tag; whole-array conversions prove the source class once
            check_quad("class B { int a = 1; } class D : B { int b = 2; } array<B> arr = [B(), D()]; to_string(arr.size()) + \",\" + to_string(arr[1].b)", "2,2", "a subclass element keeps its own class (per-slot tag): its fields stay reachable");
            check_quad("class B { int a = 1; } class D : B { int b = 2; } array<B> arr = [B()]; arr.push(D()); to_string(arr.size()) + \",\" + to_string(arr[1].a)", "2,1", "push of a subclass into array<Base>");
            check_quad("class B { int a = 1; } class D : B { int b = 2; } array<B> arr = [B()]; arr[0] = D(); to_string(arr[0].b)", "2", "store of a subclass into an element");
            check_quad("class B { int a = 1; } class D : B { int b = 2; } class E : D { int c = 3; } array<B> deep = [E()]; to_string(deep[0].c)", "3", "two inheritance levels");
            check_quad("class B { int a = 1; } class D : B { int b = 2; } array<D> ds = [D()]; array<B> bs = ds; bs[0].a = 9; to_string(ds[0].a) + \",\" + to_string(bs[0].a)", "1,9", "array<Derived> into array<Base> is a copy (value semantics)");
            check_quad("class B { int a = 1; } class D : B { int b = 2; } array<B> bs = [D(), D()]; array<D> back = bs; to_string(back.size()) + \",\" + to_string(back[1].b)", "2,2", "array<Base> whose elements are all Derived converts to array<Derived>");
            check_quad("class B { int a = 1; } class D : B { int b = 2; } function total(array<B> xs) -> int { int t = 0; for (auto x : xs) { t += x.a; } return t; } array<D> ds = [D(), D()]; to_string(total(ds))", "2", "array<Derived> binds a by-value array<Base> parameter");
            check_quad("class B { int a = 1; } class D : B { int b = 2; } function grow(array<B>& xs) { xs.push(B()); } array<B> bs = [D()]; grow(bs); to_string(bs.size())", "2", "reference parameter over array<Base> holding a subclass");
            check_quad("class B { int a = 1; } class D : B { int b = 2; } array<B> arr = [D()]; auto c = arr; c[0].b = 7; to_string(arr[0].b) + \",\" + to_string(c[0].b)", "2,7", "copy of an array<Base> clones the subclass element");
            check_quad_error("class B { int a = 1; } class D : B { int b = 2; } array<B> bs = [B()]; array<D> x = bs", "array<Base> holding a Base does not convert to array<Derived>");
            check_quad_error("class B { int a = 1; } class D : B { int b = 2; } function grow(array<B>& xs) { xs.push(B()); } array<D> ds = [D()]; grow(ds)", "a reference parameter aliases the caller's array under ITS tag: pushing a Base into array<Derived> still raises");
            check_quad_error("class B { int a = 1; } class J { int j = 0; } array<B> bs = [B()]; bs.push(J())", "an unrelated class is still rejected");
        });

        test("typed_decl_initializer_exception_wins", [check_quad_error]() {
            // fuzz seed 22684 (2026-09-02): the interpreter's typed declaration enforced the
            // declared type on the null a raising member read leaves behind, reporting
            // 'Type mismatch in assignment' where the vm reports the member exception
            check_quad_error("class K { int a = 1; } auto k = K(); int y = k.missing", "int decl from a missing member raises the member text");
            check_quad_error("class K { int a = 1; } auto k = K(); double d = (k.missing == 1) ? 1.0 : 2.0", "double decl from a missing-member comparison raises the member text");
            check_quad_error("class K { int a = 1; } auto k = K(); string s = k.missing", "string decl from a missing member raises the member text");
            check_quad_error("class K { int a = 1; } auto k = K(); shared_ptr<K> p = k.missing", "shared_ptr decl from a missing member raises the member text");
            check_quad_error("class K { int a = 1; } auto k = K(); weak_ptr<K> w = k.missing", "weak_ptr decl from a missing member raises the member text");
        });

        test("typed_storage_reads_and_stores", [check_quad, check_quad_error]() {
            check_quad("array<int> a = [1,2,3]; to_string(a[0]) + \",\" + to_string(a[2])", "1,3", "reads");
            check_quad("array<int> a = [1,2,3]; a[1] = 9; to_string(a[1])", "9", "store");
            check_quad("array<int> a = [1]; a[0] = 2.7; type_of(a[0]) + \":\" + to_string(a[0])", "int:2", "store converts");
            check_quad("array<float> a = [1.5]; a[0] = 2; type_of(a[0]) + \":\" + to_string(a[0])", "float:2.000000", "float store widens");
            check_quad("array<int> a = [10]; a[0] += 2.5; type_of(a[0]) + \":\" + to_string(a[0])", "int:12", "compound converts");
            check_quad("array<int> a = [10]; a[0] %= 4.5; to_string(a[0])", "1", "compound modulo");
            // byte kinds (bool/char buffers): reads, stores, push, sort, reverse, copies, json
            check_quad("array<bool> f = [true, false, true]; f[1] = true; to_string(f[0]) + to_string(f[1]) + to_string(f.size())", "truetrue3", "bool buffer store/read");
            check_quad("array<bool> f = []; f.push(false); f.push(true); f.sort(); to_string(f[0]) + to_string(f[1])", "falsetrue", "bool buffer push/sort");
            check_quad("array<char> c = ['q', 'a', 'z']; c.sort(); c.reverse(); to_string(c[0]) + to_string(c[2])", "za", "char buffer sort/reverse");
            check_quad("array<char> c = ['a', 'b']; array<char> d = c; d[0] = 'x'; to_string(c[0]) + to_string(d[0])", "ax", "char buffer copy is a copy");
            check_quad("array<bool> f = [true, false]; to_json(f)", "[true,false]", "bool buffer serializes");
            check_quad("array<char> c = ['h', 'i']; var s = \"\"; for (auto ch : c) { s = s + ch; } s", "hi", "char buffer range-for");
            check_quad_error("array<bool> f = [true]; f.push(1)", "bool buffer rejects int");
            check_quad_error("array<char> c = ['a']; c[0] = \"s\"", "char buffer rejects string");
            // string handles: reads, stores, push, sort, join, copy, in-place mutator through an element, json
            check_quad("array<string> s = [\"b\", \"a\"]; s.push(\"c\"); s.sort(); s.join(\"-\")", "a-b-c", "string buffer push/sort/join");
            check_quad("array<string> s = [\"x\"]; s[0] = \"y\"; s[0] + to_string(s.size())", "y1", "string buffer store/read");
            check_quad("array<string> s = [\"ab\"]; array<string> t = s; t[0] = \"zz\"; s[0] + t[0]", "abzz", "string buffer copy is a copy");
            check_quad("array<string> s = [\"q\", \"r\"]; var o = \"\"; for (auto w : s) { o = o + w; } o", "qr", "string buffer range-for");
            check_quad("array<string> s = [\"hi\"]; to_json(s)", "[\"hi\"]", "string buffer serializes");
            check_quad("array<string> s = [\"a\", \"b\"]; s.reverse(); s[0]", "b", "string buffer reverse");
            check_quad_error("array<string> s = [\"a\"]; s.push(1)", "string buffer rejects int");
            // fuzz seed 129 (2026-09-02): the vm's unfused element-store lane took ANY typed
            // node with a numeric rhs - an int landed unchecked in a string buffer
            check_quad_error("array<string> s = [\"a\"]; int g = 5; s[0] = g", "string buffer element store rejects int");
            check_quad("array<string> s = [\"a\"]; s[0] += 1; s[0]", "a1", "string buffer compound store concatenates through the enforcing path");
            check_quad_error("array<bool> f = [true]; int g = 1; f[0] = g", "bool buffer element store rejects int");
            check_quad_error("array<char> c = ['a']; c[0] += 1", "char buffer compound store rejects int");
            check_quad("array<string> s = [\"a\"]; string t = \"b\"; s[0] = t; s[0]", "b", "string buffer element store of a string");
            // fuzz seed 6977 (2026-09-02): the vm's inline push named an ELEMENT-REFERENCE
            // argument 'reference' in the mismatch text where the interpreter names the
            // referent (pre-existing for dynamic tables too; ref locals and fields were fine)
            check_quad_error("array<string> t = [\"a\"]; array<int> v = [1]; v.push(t[0])", "push of a string-buffer element into array<int> names the referent");
            check_quad_error("array<var> t = [\"a\"]; array<int> v = [1]; v.push(t[0])", "push of a dynamic-table element into array<int> names the referent");
            check_quad("array<string> t = [\"a\"]; array<string> v = []; v.push(t[0]); v[0]", "a", "push of a string-buffer element into array<string>");
        });

        test("typed_storage_errors_match_plain", [check_quad_error]() {
            check_quad_error("array<int> a = [1]; a[0] = \"s\"; a[0]", "store mismatch");
            check_quad_error("array<int> a = []; a.push(\"s\"); a.size()", "push mismatch");
            check_quad_error("array<int> a = [1]; a[5]", "read OOB");
            check_quad_error("array<int> a = [1]; a[3] = 2; a[0]", "store OOB");
            check_quad_error("array<int> a = [1]; for (auto& x : a) { x = \"s\"; } a[0]", "range-for ref mismatch");
        });

        test("typed_storage_builtins", [check_quad]() {
            check_quad("array<float> f = []; f.push(1); f.push(2.5); to_string(f.size()) + type_of(f[0]) + to_string(f.back())",
                       "2float2.500000", "push/size/back");
            check_quad("array<int> a = [3,1,2]; a.sort(); to_string(a[0]) + to_string(a[1]) + to_string(a[2])", "123", "default sort");
            check_quad("array<int> a = [3,1,2]; a.sort([](auto x, auto y) -> auto { return x > y; }); to_string(a[0]) + to_string(a[2])", "31", "comparator sort");
            check_quad("array<int> a = [1,2,3]; a.reverse(); to_string(a[0])", "3", "reverse");
            check_quad("array<int> a = [1,2,3]; a.remove(1); to_string(a.size()) + to_string(a[1])", "23", "remove");
            check_quad("array<int> a = [1,2,3,4]; var n = a.remove_if([](auto x) -> auto { return x % 2 == 0; }); to_string(n) + \":\" + to_string(a.size()) + to_string(a[1])", "2:23", "remove_if");
            check_quad("array<int> a = [5,6,7]; to_string(a.index_of(6)) + to_string(a.has(7)) + to_string(a.has(9))", "1truefalse", "index_of/has");
            check_quad("array<int> a = [1,2,3,4]; var s = a.slice(1,3); to_string(s.size()) + to_string(s[0])", "22", "slice");
            check_quad("array<int> a = [1,2]; to_string(a.pop()) + \":\" + to_string(a.size())", "2:1", "pop");
            check_quad("array<int> a = [1,2,3]; a.join(\"-\")", "1-2-3", "join");
            check_quad("array<int> a = [9]; to_string(a.front()) + to_string(a.first()) + to_string(a.back()) + to_string(a.last())", "9999", "front/back");
            check_quad("array<float> f = [1.5,2.5]; var t = 0.0; var r = f.filter([](auto x) -> auto { return x > 2.0; }); to_string(r.size()) + to_string(r[0])", "12.500000", "filter");
        });

        test("typed_storage_iteration_and_refs", [check_quad]() {
            check_quad("array<int> a = [1,2,3]; var s = 0; for (auto x : a) { s += x; } to_string(s)", "6", "range-for copy");
            check_quad("array<int> a = [1,2,3]; for (auto& x : a) { x *= 2; } to_string(a[0]) + to_string(a[2])", "26", "range-for ref mutates");
            check_quad("array<int> a = [5]; var& r = a[0]; r = 9; to_string(a[0])", "9", "var& element write");
            check_quad("array<int> a = [5]; var& r = a[0]; r += 2.5; type_of(a[0]) + \":\" + to_string(a[0])", "int:7", "ref compound converts");
            check_quad("array<int> a = [5]; var& r = a[0]; r++; to_string(a[0]) + to_string(r)", "66", "ref increment commits");
            check_quad("array<float> a = [1.5]; var& r = a[0]; r--; to_string(a[0])", "0.500000", "float ref decrement");
            check_quad(R"(
                array<int> a = [10];
                function f(int& r) { r += 2.5; }
                f(a[0]);
                type_of(a[0]) + ":" + to_string(a[0])
            )", "int:12", "int& param through element");
            check_quad("array<int> a = [1,2]; var [x, y] = a; to_string(x) + to_string(y)", "12", "destructure");
        });

        test("typed_storage_value_semantics_and_laundering", [check_quad]() {
            // decl copy is a deep copy (kind-preserving buffer copy)
            check_quad("array<int> a = [1,2]; array<int> b = a; b[0] = 9; to_string(a[0]) + to_string(b[0])", "19", "typed->typed copy");
            // var laundering demotes the (fresh) node: heterogeneous writes then work
            check_quad("array<int> a = [1,2]; var v = a; v.push(\"s\"); to_string(v.size()) + type_of(v[2]) + to_string(a.size())", "3string2", "laundered var accepts strings");
            // var& preserves the tag: still enforced
            check_quad("array<int> a = [1]; var& r = a; r.push(2); to_string(a.size())", "2", "var& push int");
            // shrink-under-ref errors identically
            check_quad("array<int> a = [1,2]; var v = a.pop(); to_string(v) + to_string(a.size())", "21", "pop shrink");
            // nested in maps/untyped containers: element keeps its tag + storage
            check_quad("var m = []; array<int> a = [7]; m.push(a); to_string(m[0][0])", "7", "typed inside untyped");
        });

        test("typed_storage_parallel_bridge", [check_quad]() {
            // stage-2a bridge: a typed parallel source demotes in place at the barrier
            // (stage 3 replaces this with raw typed worker reads) — results identical
            check_quad(R"(
                array<int> a = [1, 2, 3, 4];
                parallel_for (auto& x : a) { x = x * 2; }
                to_string(a[0]) + to_string(a[3])
            )", "28", "parallel_for over typed");
            check_quad(R"(
                array<float> f = [1.5, 2.5];
                function dbl(var x) { return x * 2.0; }
                var r = parallel_transform(f, dbl);
                to_string(r[1])
            )", "5.000000", "parallel_transform over typed");
            check_quad(R"(
                array<int> pal = [10, 20, 30];
                var src = [0, 1, 2];
                function lookup(var i) { return pal[i]; }
                var r = parallel_transform(src, lookup);
                to_string(r[2])
            )", "30", "typed array as parallel capture");
        });

        test("typed_storage_engages_raw_buffers", [this]() {
            // Not just parity: declared element types must actually produce typed nodes
            // (and the laundering path must actually demote)
            auto e = jai::engine::make();
            jai::stdlib::register_all(e);
            auto v = e->execute("array<int> a = [1,2,3]; a");
            check(v.is_array(), "returns array");
            check(v.unchecked_array_node()->kind() == jai::script_array::kind_t::i64, "array<int> holds an i64 buffer");
            auto f = e->execute("array<float> b = [1.5]; b");
            check(f.unchecked_array_node()->kind() == jai::script_array::kind_t::f64, "array<float> holds an f64 buffer");
            auto b = e->execute("array<bool> flags = [true, false, true]; flags");
            check(b.unchecked_array_node()->kind() == jai::script_array::kind_t::b8, "array<bool> holds a byte buffer");
            auto c = e->execute("array<char> cs = ['a', 'z']; cs");
            check(c.unchecked_array_node()->kind() == jai::script_array::kind_t::c8, "array<char> holds a byte buffer");
            auto ab = e->execute("auto inferred = [true, false]; inferred");
            check(ab.unchecked_array_node()->kind() == jai::script_array::kind_t::b8, "auto bool literal infers the b8 buffer");
            auto st = e->execute("array<string> names = [\"a\", \"b\"]; names");
            check(st.unchecked_array_node()->kind() == jai::script_array::kind_t::str, "array<string> holds a handle buffer");
            auto as = e->execute("auto words = [\"x\", \"y\"]; words");
            check(as.unchecked_array_node()->kind() == jai::script_array::kind_t::str, "auto string literal infers the handle buffer");
            auto d = e->execute("array<int> c = [1]; var laundered = c; laundered");
            check(d.unchecked_array_node()->kind() == jai::script_array::kind_t::hetero, "var laundering demotes the node");
            auto o = e->execute("class K { int a = 1; } array<K> ks = [K(), K()]; ks");
            check(o.unchecked_array_node()->kind() == jai::script_array::kind_t::obj, "array<K> holds an object slot buffer");
            auto oe = e->execute("class K2 { int a = 1; } array<K2> none = []; none");
            check(oe.unchecked_array_node()->kind() == jai::script_array::kind_t::obj, "an empty array<K> is an object slot buffer");
            auto ol = e->execute("class K3 { int a = 1; } array<K3> k3 = [K3()]; var laundered3 = k3; laundered3");
            check(ol.unchecked_array_node()->kind() == jai::script_array::kind_t::hetero, "var laundering demotes an object buffer");
            auto oa = e->execute("class K4 { int a = 1; } auto inferred4 = [K4(), K4()]; inferred4");
            check(oa.unchecked_array_node()->kind() == jai::script_array::kind_t::hetero, "an auto object literal stays dynamic");
            auto os = e->execute("class K5 { int a = 1; } shared_ptr<K5> sp = K5(); array<K5> ks5 = [sp]; ks5");
            check(os.unchecked_array_node()->kind() == jai::script_array::kind_t::obj, "a shared_ptr-tagged element lives in the object buffer");
        });

        // type_of on a shared-pointer-semantic instance must match a value instance: both "object".
        test("type_of_shared_and_value_instance_both_object", [this, check_both]() {
            const char* decl = "class P { int v = 0; } ";
            check_both((std::string(decl) + "auto p = P(); type_of(p)").c_str(), "object", "value instance");
            check_both((std::string(decl) + "var p = new P(); type_of(p)").c_str(), "object", "new P()");
            check_both((std::string(decl) + "shared_ptr<P> p = P(); type_of(p)").c_str(), "object", "shared_ptr<P> p = P()");
            check_both((std::string(decl) + "auto p = shared_ptr<P>(); type_of(p)").c_str(), "object", "auto p = shared_ptr<P>()");
            // value == shared, on the same class, in one program
            check_both((std::string(decl) +
                "auto a = P(); auto b = new P(); type_of(a) + \"==\" + type_of(b)").c_str(),
                "object==object", "value == shared");
        });

        // ===== PARSE-TIME TYPE PROOFS (docs/static_checking.md "Proofs") =====
        // A proof selects a lane and a total runtime guard makes it sound, so every program
        // must read the same on {interp, vm} x {type_proofs on, off}. Segments run in order on
        // one engine per lane; a class unit is executed three times so its field proofs arm
        // (first execute -> stamped -> identical-reload hit) before the rows that use them.
        auto proof_run = [](const std::vector<std::string>& segments) {
            std::array<std::string, 4> out;   // interp/proofs, interp/plain, vm/proofs, vm/plain
            int idx = 0;
            for (bool use_vm : {false, true}) {
                for (bool proofs : {true, false}) {
                    auto e = jai::engine::make();
                    if (use_vm) { e->set_backend(jai::backend_type::vm); }
                    e->type_proofs(proofs);
                    e->execution_budget(0);
                    jai::stdlib::register_all(e);
                    std::string acc;
                    for (const auto& seg : segments) {
                        try { acc += e->execute(seg).to_string(); }
                        catch (const std::exception& ex) { acc += std::string("ERROR: ") + ex.what(); }
                        acc += '\x1f';
                    }
                    out[idx++] = acc;
                }
            }
            return out;
        };
        auto last_outcome = [](const std::string& joined) {
            const std::string body = joined.substr(0, joined.empty() ? 0 : joined.size() - 1);
            const size_t cut = body.rfind('\x1f');
            return cut == std::string::npos ? body : body.substr(cut + 1);
        };
        auto check_proof_lanes = [this](const std::array<std::string, 4>& r, const std::string& what) {
            const char* lane[4] = {"interp/proofs", "interp/plain", "vm/proofs", "vm/plain"};
            for (int i = 0; i < 4; ++i) { check_eq(r[1], r[i], std::string(lane[i]) + " vs interp/plain: " + what); }
        };
        // every lane equal; the final segment's outcome pinned when `expected` is non-empty
        auto check_proof_run = [this, proof_run, last_outcome, check_proof_lanes](const std::vector<std::string>& segments, const std::string& expected, const std::string& what) {
            auto r = proof_run(segments);
            check_proof_lanes(r, what);
            if (!expected.empty()) { check_eq(expected, last_outcome(r[0]), "value: " + what); }
        };
        auto check_proof_error = [this, proof_run, last_outcome, check_proof_lanes](const std::vector<std::string>& segments, const std::string& what) {
            auto r = proof_run(segments);
            check_proof_lanes(r, what);
            check(last_outcome(r[0]).rfind("ERROR", 0) == 0, "raises: " + what + " -> " + last_outcome(r[0]));
        };
        auto thrice = [](const std::string& unit) { return std::vector<std::string>{unit, unit, unit}; };
        auto thrice_then = [](const std::string& unit, const std::string& tail) { return std::vector<std::string>{unit, unit, unit, tail}; };

        test("proof_battery_same_type_stores", [check_proof_run, check_proof_error, thrice, thrice_then]() {
            // per certain kind: env-global `=`, decl init, bare `f =`, `this.f =`, `o.f =`,
            // typed and var parameters, typed return, type_of after the stores, then a
            // mismatching store that must take the ladder behind the (now stale) proof
            struct kind_row { const char* type; const char* a; const char* b; const char* mismatch; const char* expect; const char* expect_mismatch; };
            const kind_row rows[] = {
                {"int",    "1",     "2",     "2.5", "int:2:int:2:2:1:2",                      "int:2:int:2"},
                {"float",  "1.5",   "2.5",   "3",   "float:2.500000:float:2.500000:2.500000:1.500000:2.500000", "float:3.000000:float:3.000000"},
                {"bool",   "true",  "false", "1",   "bool:false:bool:false:false:true:false", "bool:true:bool:true"},
                {"char",   "'a'",   "'b'",   "65",  "char:b:char:b:b:a:b",                    ""},
                {"string", "\"a\"", "\"b\"", "3",   "string:b:string:b:b:a:b",                "string:3:string:3"},
            };
            for (const kind_row& k : rows) {
                const std::string T = k.type;
                const std::string unit =
                    "class C { " + T + " f = " + k.a + ";\n"
                    "  " + T + " set(" + T + " v) { f = v; this.f = v; return f; }\n"
                    "  " + T + " pass(var v) { " + T + " local = v; local = v; return local; }\n"
                    "  " + T + " viaArg(" + T + " v) { return set(v); } }\n"
                    + T + " g = " + k.a + "; g = " + k.b + ";\n"
                    + T + " fn(" + T + " v) { " + T + " local = v; local = " + k.b + "; return local; }\n"
                    "C c = C(); c.f = " + k.b + "; " + T + " r = c.set(" + k.a + "); r = c.viaArg(" + k.b + "); " + T + " q = c.pass(" + k.a + ");\n"
                    "type_of(g) + \":\" + to_string(g) + \":\" + type_of(c.f) + \":\" + to_string(c.f) + \":\" + to_string(r) + \":\" + to_string(q) + \":\" + to_string(fn(" + k.a + "))";
                const std::string mismatch =
                    "c.f = " + std::string(k.mismatch) + "; g = " + k.mismatch + "; type_of(c.f) + \":\" + to_string(c.f) + \":\" + type_of(g) + \":\" + to_string(g)";
                check_proof_run(thrice(unit), k.expect, T + " same-type lanes");
                if (T == "char") { check_proof_error(thrice_then(unit, mismatch), "int into a proven char field/global raises"); }
                else { check_proof_run(thrice_then(unit, mismatch), k.expect_mismatch, T + " mismatching store takes the ladder"); }
            }
            const std::string obj_unit =
                "class K { int n = 0; K(int v) { n = v; } }\n"
                "class C { K f = K(0);\n"
                "  K set(K v) { f = v; this.f = v; return f; }\n"
                "  K pass(var v) { K local = v; local = v; return local; }\n"
                "  K viaArg(K v) { return set(v); } }\n"
                "K g = K(1); g = K(2);\n"
                "K fn(K v) { K local = v; local = K(2); return local; }\n"
                "C c = C(); c.f = K(2); K r = c.set(K(1)); r = c.viaArg(K(2)); K q = c.pass(K(1));\n"
                "type_of(g) + \":\" + to_string(g.n) + \":\" + type_of(c.f) + \":\" + to_string(c.f.n) + \":\" + to_string(r.n) + \":\" + to_string(q.n) + \":\" + to_string(fn(K(1)).n)";
            check_proof_run(thrice(obj_unit), "object:2:object:2:2:1:2", "object same-type lanes");
            check_proof_run(thrice_then(obj_unit, "c.f = 5; g = 6; to_string(c.f.n) + \":\" + to_string(g.n)"), "", "int into an object slot takes the ctor-conversion ladder");
            check_proof_error(thrice_then(obj_unit, "class J { int m = 0; } g = J(); g.n"), "unrelated class into an object slot raises");
            check_proof_run(thrice_then(obj_unit, "c.f = null; type_of(c.f)"), "", "null into an object field takes the ladder");
        });

        test("proof_battery_not_certain", [check_proof_run, check_proof_error, thrice_then]() {
            // nothing here is certain (var, auto without initializer, handles, containers,
            // references, by-ref params, callable locals, coroutines, null, placeholders,
            // derived-into-base, handle-into-value): the ladder runs and all lanes agree
            const std::string cls = "class C { int v = 1; } class D : C { int w = 2; }\n";
            check_proof_run({"var v = 1; v = \"s\"; v = 2.5; type_of(v) + \":\" + to_string(v)"}, "float:2.500000", "var");
            check_proof_run({"auto n; n = 3; n = 4; type_of(n) + \":\" + to_string(n)"}, "int:4", "auto without initializer");
            check_proof_error({"auto n; n = 3; n = \"s\"; n"}, "auto locks at its first assignment");
            check_proof_run({cls + "shared_ptr<C> sp = C(); shared_ptr<C> sq = sp; sq.v = 5; to_string(sp.v)"}, "5", "shared_ptr handle");
            check_proof_run({cls + "shared_ptr<C> sp = C(); weak_ptr<C> wp = sp; to_string(wp.expired()) + \":\" + to_string(wp.lock().v)"}, "false:1", "weak_ptr handle");
            check_proof_run({"array<int> a = [1, 2]; array<int> b = a; b[0] = 9; to_string(a[0]) + to_string(b[0])"}, "19", "typed array copies");
            check_proof_run({"map<string,int> m = {\"a\": 1}; map<string,int> n = m; n[\"a\"] = 2; to_string(m[\"a\"]) + to_string(n[\"a\"])"}, "12", "typed map copies");
            check_proof_run({"int x = 1; int& r = x; r = 5; to_string(x)"}, "5", "reference declaration");
            check_proof_run({"void bump(int& r) { r = r + 1; } int x = 1; bump(x); to_string(x)"}, "2", "by-reference parameter");
            check_proof_run({"int twice(int v) { return v * 2; } var f = twice; to_string(f(4))"}, "8", "callable local");
            check_proof_error({"int twice(int v) { return v * 2; } var f = twice; f(\"s\")"}, "callable local still enforces its parameter");
            check_proof_run({"coroutine int gen(int n) { yield n; return n + 1; } auto h = gen(3); to_string(h.resume()) + \":\" + to_string(h.resume())"}, "3:4", "coroutine callee");
            check_proof_run({cls + "C a = C(); a = null; type_of(a)"}, "", "null into an object slot");
            check_proof_run({cls + "C a; C b = a; type_of(b)"}, "", "typed-null placeholder");
            check_proof_run({cls + "C b = D(); to_string(b.v) + \":\" + type_of(b)"}, "1:object", "derived into base");
            check_proof_run({cls + "shared_ptr<C> sp = C(); C c = sp; c.v = 9; to_string(sp.v) + \":\" + to_string(c.v)"}, "", "handle into a value slot");
            check_proof_error({"int x = 1; x = \"s\"; x"}, "string into int raises");
            check_proof_error({"char c = 'a'; c = 65; c"}, "int into char raises");
            check_proof_error({"int f(int v) { return v; } f(\"s\")"}, "string argument into an int parameter raises");
            check_proof_error({"int g() { return \"s\"; } g()"}, "string returned from an int function raises");
            check_proof_error({cls + "class J { int m = 1; } C c = C(); c = J(); c"}, "unrelated class into an object slot raises");
            const std::string armed = "class F { int v = 1; int set(int x) { v = x; return v; } } F f = F(); f.set(2); f.v";
            check_proof_error(thrice_then(armed, "f.set(\"z\")"), "string into a proven int parameter raises after arming");
            check_proof_error(thrice_then(armed, "f.v = \"z\"; f.v"), "string into a proven member store raises after arming");
        });

        test("proof_call_arg_identity_guard", [check_proof_run, check_proof_error, thrice_then]() {
            // a stamped call site names its callee body: another function bound to the same
            // name, a later redefinition, overload sets, defaults and by-ref params all run
            // the unproven path and every lane agrees
            check_proof_run({"int f(int v) { return v + 1; } int other(int v) { return v * 10; } int a = f(1); f = other; int b = f(1); to_string(a) + \":\" + to_string(b)"}, "", "global function rebound between calls");
            check_proof_run({"int f(int v) { return v + 1; } var use(var a) { return f(a); } to_string(use(2))",
                             "string f(string s) { return s + \"!\"; } to_string(use(\"2\"))",
                             "to_string(use(\"3\"))"}, "3!", "later execute redefines the callee with another signature");
            check_proof_run({"int f(int v) { return v + 1; } var use() { return f(2); } to_string(use())",
                             "string f(string s) { return s + \"!\"; } to_string(use())"}, "2!", "a stamped int argument meets a string parameter after the redefinition: the guard falls to the string conversion");
            check_proof_run({"class O { int m(int v) { return v; } int m(int a, int b) { return a + b; } int m(string s) { return 100; } }\n"
                             "O o = O(); to_string(o.m(1)) + \":\" + to_string(o.m(1, 2)) + \":\" + to_string(o.m(\"s\"))"}, "1:3:100", "overloaded methods");
            check_proof_run({"int d(int a, int b = 5) { return a + b; } to_string(d(1)) + \":\" + to_string(d(1, 2))"}, "6:3", "defaults");
            check_proof_run({"void inc(int& r, int by) { r = r + by; } int x = 1; inc(x, 2); to_string(x)"}, "3", "by-reference parameter");
            const std::string unit = "class O { int m(int v) { return v * 2; } } O o = O(); int use2() { return o.m(3); } to_string(use2())";
            check_proof_run(thrice_then(unit, "class O { int m(int v) { return v * 2; } int m(string s) { return 7; } } to_string(use2()) + \":\" + to_string(o.m(\"s\"))"), "6:7", "reload grows the overload set behind a stamped site");
            check_proof_run(thrice_then(unit, "class O { string m(int v) { return \"m\"; } } to_string(use2())"), "", "reload retypes the callee's return behind a stamped site");
            check_proof_error(thrice_then("int twice(int v) { return v * 2; } int r = twice(4); to_string(twice(r))", "twice(\"s\")"), "string into a proven int parameter raises");
        });

        test("proof_member_second_execute", [check_proof_run, thrice]() {
            // the same source three times on one engine: first execute (no field proofs),
            // second (stamped, then the first redefinition's full-path reload), third
            // (re-stamped; identical reloads keep hitting) - equal across passes and lanes
            const std::string unit =
                "class P { int hp = 10; float x = 1.5; string tag = \"p\"; bool alive = true; char c = 'a';\n"
                "  int tick(int d) { hp = hp - d; this.hp = this.hp + 1; x = x * 2.0; tag = tag + \"!\"; alive = !alive; c = 'b'; return hp; } }\n"
                "P p = P(); int r = p.tick(3); r = r + p.tick(1); p.hp = r;\n"
                "to_string(r) + \":\" + to_string(p.hp) + \":\" + type_of(p.hp) + \":\" + type_of(p.x) + \":\" + p.tag + \":\" + to_string(p.alive) + \":\" + to_string(p.c)";
            check_proof_run(thrice(unit), "16:16:int:float:p!!:true:b", "self-defining unit executed three times");
            // module pattern: the class unit runs three times before its users' first check
            const std::string cls = "class Q { int f = 10; string tag = \"q\"; int tick(int d) { f = f - d; this.f = this.f + 1; tag = tag + \"!\"; return f; } Q dup() { Q q = Q(); q.f = f; q.tag = tag; return q; } }";
            const std::string users = "Q q = Q(); int r2 = q.tick(3); Q d = q.dup(); d.f = r2; d.tag = q.tag; to_string(r2) + \":\" + to_string(q.f) + \":\" + to_string(d.f) + \":\" + d.tag + \":\" + to_string(q.dup().f)";
            check_proof_run({cls, cls, cls, users, users, users}, "8:8:8:q!:8", "module pattern");
        });

        test("proof_stats_fire_and_kill_switch", [this]() {
            // the runner's backend: the proof pass stamps every kind on an armed unit (the
            // counters prove the lanes are reachable), a type_proofs(false) twin stamps nothing,
            // and both read identically at every step - including a switch flipped mid-run
            auto on = make_engine();
            auto off = make_engine();
            off->type_proofs(false);
            jai::stdlib::register_all(on);
            jai::stdlib::register_all(off);
            auto outcome = [](jai::engine& e, const std::string& src) -> std::string {
                try { return "VALUE " + e.execute(src).to_string(); }
                catch (const std::exception& ex) { return std::string("ERROR ") + ex.what(); }
            };
            const std::string unit =
                "class P { int hp = 10; float x = 1.5; string tag = \"p\"; bool alive = true; char c = 'a';\n"
                "  int tick(int d) { hp = hp - d; this.hp = this.hp + 1; x = x * 2.0; tag = tag + \"!\"; alive = !alive; c = 'b'; return hp; } }\n"
                "P p = P(); int r = p.tick(3); r = r + p.tick(1); p.hp = r;\n"
                "to_string(r) + \":\" + to_string(p.hp) + \":\" + type_of(p.hp) + \":\" + p.tag";
            const std::string use = "r = p.tick(2); p.hp = r; p.tag = \"t\"; to_string(p.hp) + \":\" + p.tag";
            const std::string bad = "p.hp = \"x\"; p.hp";
            for (int pass = 0; pass < 3; ++pass) {
                check_eq(outcome(*off, unit), outcome(*on, unit), "unit pass " + std::to_string(pass));
            }
            const proof_stats st = on->last_proof_stats();
            check_ge(st.members, (uint32_t)8, "field proofs on the hp/x/tag/alive/c sites");
            check_ge(st.stores, (uint32_t)5, "same-type stores");
            check_ge(st.decls, (uint32_t)2, "same-type decls");
            check_ge(st.calls, (uint32_t)1, "statically resolved method calls");
            check_ge(st.args, (uint32_t)1, "int argument into an int parameter");
            check_ge(st.returns, (uint32_t)1, "int returned from an int method");
            const proof_stats none = off->last_proof_stats();
            check_eq((uint32_t)0, none.members + none.stores + none.decls + none.calls + none.args + none.returns, "the twin stamps nothing");
            for (int pass = 0; pass < 3; ++pass) {
                const std::string got = outcome(*on, use);
                check_eq(outcome(*off, use), got, "use pass " + std::to_string(pass));
                if (pass == 0) { check_eq(std::string("VALUE 15:t"), got); }
                const std::string rejected = outcome(*on, bad);
                check_eq(outcome(*off, bad), rejected, "bad pass " + std::to_string(pass));
                check(rejected.rfind("ERROR", 0) == 0, "string into a proven int field raises: " + rejected);
            }
            on->type_proofs(false);
            check_eq(outcome(*off, use), outcome(*on, use), "switched off mid-run");
            check_eq((uint32_t)0, on->last_proof_stats().members, "stripped at the next execute");
            on->type_proofs(true);
            check_eq(outcome(*off, use), outcome(*on, use), "switched back on");
            check_ge(on->last_proof_stats().members, (uint32_t)1, "re-stamped at the next execute");
        });

        // ===== USER ACCESSORS ON MEMBER ACCESS =====
        // expr.x (any receiver) reads through the nearest USER _get_x in the chain and stores
        // through the nearest USER _set_x; the synthesized field accessors never shadow one.
        // Bare x inside the chain's methods is the backing field. Both backends, strict mode.
        test("accessor_member_reads_and_stores", [check_both]() {
            check_both(
                "class Q { int b = 1; int _get_b() { return 42; } int _set_b(int v) { b = v * 10; return b; } int raw() { return b; } }\n"
                "var q = Q();\n"
                "string r = to_string(q.b);\n"
                "q.b = 2;\n"
                "r = r + \":\" + to_string(q.b) + \":\" + to_string(q.raw());\n"
                "r = r + \":\" + to_string(q._get_b()) + \":\" + to_string(q._set_b(3)) + \":\" + to_string(q.raw());\n"
                "var y = (q.b = 5);\n"
                "r + \":\" + to_string(y) + \":\" + to_string(q.raw())",
                "42:42:20:42:30:30:5:50", "same-class _get_b/_set_b own the member; the store yields the assigned value");
        });

        test("accessor_bare_access_in_body", [check_both]() {
            check_both(
                "class C { int b = 1; int _get_b() { return b + 100; } void _set_b(int v) { b = v * 10; }\n"
                "  int bare() { return b; } int viaThis() { return this.b; } void bareStore(int v) { b = v; } }\n"
                "var c = C();\n"
                "string r = to_string(c.b) + \":\" + to_string(c.bare()) + \":\" + to_string(c.viaThis());\n"
                "c.bareStore(5);\n"
                "r = r + \":\" + to_string(c.b) + \":\" + to_string(c.bare());\n"
                "c.b = 7;\n"
                "r + \":\" + to_string(c.bare()) + \":\" + to_string(c.viaThis())",
                "101:1:101:105:5:70:170", "bare b is the backing field (no recursion), this.b is the accessor");
        });

        test("accessor_compound_and_incdec", [check_both]() {
            check_both(
                "class C { int b = 1; int _get_b() { return b + 100; } void _set_b(int v) { b = v * 10; } int raw() { return b; } }\n"
                "var c = C();\n"
                "c.b += 1;\n"
                "string r = to_string(c.b) + \":\" + to_string(c.raw());\n"
                "var old = c.b++;\n"
                "r = r + \":\" + to_string(old) + \":\" + to_string(c.b) + \":\" + to_string(c.raw());\n"
                "c.b -= 10;\n"
                "r = r + \":\" + to_string(c.b) + \":\" + to_string(c.raw());\n"
                "var pre = --c.b;\n"
                "r + \":\" + to_string(pre) + \":\" + to_string(c.b) + \":\" + to_string(c.raw())",
                "1120:1020:1120:11310:11210:113100:113000:113099:1131090:1130990",
                "compound reads through _get_b and writes through _set_b; postfix yields the old getter value");
        });

        test("accessor_child_over_parent_field", [check_both]() {
            // no `override` needed over a base's SYNTHESIZED accessor; `override` still accepted
            check_both(
                "class P { int b = 1; }\n"
                "class C : P { int _get_b() { return b + 100; } void _set_b(int v) { b = v * 10; } int raw() { return b; } }\n"
                "var c = C();\n"
                "string r = \"\";\n"
                "for (int i = 0; i < 3; ++i) { r = r + to_string(c.b) + \",\"; }\n"
                "for (int i = 0; i < 3; ++i) { c.b = i + 2; r = r + to_string(c.b) + \",\"; }\n"
                "int hot() { int s = 0; for (int i = 0; i < 5; ++i) { s += c.b; } return s; }\n"
                "r = r + to_string(hot()) + \",\" + to_string(hot()) + \",\";\n"
                "void hots(int v) { for (int i = 0; i < 3; ++i) { c.b = v; } }\n"
                "hots(7); r = r + to_string(c.raw()) + \",\"; hots(9); r + to_string(c.raw())",
                "101,101,101,120,130,140,700,700,70,90", "child accessor over a parent field: every read/store dispatches (no armed slot)");
            check_both(
                "class P { int b = 1; } class C : P { override int _get_b() { return b + 100; } } var c = C(); to_string(c.b)",
                "101", "override spelling accepted");
        });

        test("accessor_parent_over_child_field", [check_both]() {
            check_both(
                "class P { int log = 0; int _get_b() { return 42; } void _set_b(int v) { log = log + v; } }\n"
                "class C : P { int b = 1; int raw() { return b; } }\n"
                "var c = C();\n"
                "string r = to_string(c.b);\n"
                "c.b = 2;\n"
                "r + \":\" + to_string(c.b) + \":\" + to_string(c.raw()) + \":\" + to_string(c.log)",
                "42:42:1:2", "the parent's USER accessor wins over the child's synthesized one");
        });

        test("accessor_proofs_on_off_identical", [this, check_proof_run, thrice_then]() {
            const std::string unit =
                "class Q { int b = 1; int plain = 2; int _get_b() { return b + 100; } void _set_b(int v) { b = v * 10; } int raw() { return b; } }\n"
                "class P { int b = 1; } class C : P { int _get_b() { return b + 100; } void _set_b(int v) { b = v * 10; } int raw() { return b; } }\n"
                "class PP { int log = 0; int _get_b() { return 42; } void _set_b(int v) { log = log + v; } } class CC : PP { int b = 1; }\n"
                "string probe(Q q, C c, CC cc) { q.b = 2; c.b = 3; cc.b = 4; q.plain = q.plain + 1; q.b += 1;\n"
                "  return to_string(q.b) + \":\" + to_string(q.raw()) + \":\" + to_string(c.b) + \":\" + to_string(c.raw()) + \":\" + to_string(cc.b) + \":\" + to_string(cc.log) + \":\" + to_string(q.plain); }\n"
                "Q q = Q(); C c = C(); CC cc = CC();\n"
                "probe(q, c, cc)";
            // by-value class parameters copy the instance, so every probe sees fresh receivers
            check_proof_run(thrice_then(unit, "probe(q, c, cc)"), "1310:1210:130:30:42:4:3", "typed receivers with user accessors: all four lanes agree");
            // an accessor-bearing field never carries a field proof; a sibling plain field still does
            auto members_after_two = [](const std::string& src) {
                auto e = jai::engine::make();
                e->execute(src);
                e->execute(src);
                return e->last_proof_stats().members;
            };
            check_eq((uint32_t)0, members_after_two("class Q { int b = 1; int _get_b() { return 7; } } int probe(Q q) { return q.b; } Q q = Q(); probe(q)"), "user getter: member site refused");
            check_eq((uint32_t)1, members_after_two("class Q { int b = 1; int plain = 2; int _get_b() { return 7; } } int probe(Q q) { return q.b + q.plain; } Q q = Q(); probe(q)"), "only the plain sibling proves");
            check_eq((uint32_t)1, members_after_two("class Q { int b = 1; int plain = 2; void _set_b(int v) { } } int probe(Q q) { q.b = 3; return q.b + q.plain; } Q q = Q(); probe(q)"), "user setter: read and write sites refused, the sibling proves");
        });

        test("accessor_hot_reload_adds_and_removes", [this]() {
            for (bool use_vm : {false, true}) {
                const std::string tag = use_vm ? "vm: " : "interp: ";
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(
                    "class R { int f = 1; int log = 0; int raw() { return f; } }\n"
                    "var r = R();\n"
                    "int hot() { int s = 0; for (int i = 0; i < 5; ++i) { s += r.f; } return s; }\n"
                    "void hots(int v) { for (int i = 0; i < 5; ++i) { r.f = v; } }");
                check_eq((int64_t)5, e->execute("hot()").as_int(), tag + "plain field");
                e->execute("hots(2);");
                check_eq((int64_t)10, e->execute("hot()").as_int(), tag + "armed read site");
                e->execute("class R { int f = 1; int log = 0; int raw() { return f; } int _get_f() { return f * 10; } void _set_f(int v) { log = log + 1; } }");
                check_eq((int64_t)100, e->execute("hot()").as_int(), tag + "the reload's getter serves the hot read site");
                e->execute("hots(3);");
                check_eq((int64_t)5, e->execute("r.log").as_int(), tag + "the reload's setter fires at the hot store site");
                check_eq((int64_t)2, e->execute("r.raw()").as_int(), tag + "the setter only logged");
                check_eq((int64_t)20, e->execute("r.f").as_int(), tag + "plain member read through the getter");
                e->execute("class R { int f = 1; int log = 0; int raw() { return f; } }");
                check_eq((int64_t)10, e->execute("hot()").as_int(), tag + "accessor removed: the raw field again");
                e->execute("hots(4);");
                check_eq((int64_t)20, e->execute("hot()").as_int(), tag + "raw store again");
                check_eq((int64_t)5, e->execute("r.log").as_int(), tag + "log untouched by raw stores");
                // a parent gaining an accessor reaches existing child instances
                e->execute(
                    "class P { int b = 1; } class C : P { int raw() { return b; } } var c = C();\n"
                    "int hotc() { int s = 0; for (int i = 0; i < 5; ++i) { s += c.b; } return s; }");
                check_eq((int64_t)5, e->execute("hotc()").as_int(), tag + "child reads the parent field");
                check_eq((int64_t)5, e->execute("hotc()").as_int(), tag + "armed");
                e->execute("class P { int b = 1; int _get_b() { return b + 100; } }");
                check_eq((int64_t)505, e->execute("hotc()").as_int(), tag + "the parent's new getter serves the child's hot site");
                check_eq((int64_t)1, e->execute("c.raw()").as_int(), tag + "bare read in the child is the field");
            }
        });

        test("accessor_field_keeps_ic_off", [check_both]() {
            // a getter with a side effect: an armed member slot IC would sum the raw field (0)
            check_both(
                "class C { int b = 0; int n = 0; int _get_b() { n = n + 1; return n; } }\n"
                "var c = C();\n"
                "int sum() { int s = 0; for (int i = 0; i < 5; ++i) { s += c.b; } return s; }\n"
                "string r = to_string(sum()) + \":\" + to_string(sum());\n"
                "class D { int b = 0; int cnt = 0; void _set_b(int v) { cnt = cnt + 1; } int raw() { return b; } }\n"
                "var d = D();\n"
                "void fill() { for (int i = 0; i < 5; ++i) { d.b = i; } }\n"
                "fill(); fill();\n"
                "r + \":\" + to_string(d.cnt) + \":\" + to_string(d.b) + \":\" + to_string(d.raw())",
                "15:40:10:0:0", "every hot read/store dispatches to the accessor");
        });

        test("accessor_setter_error_text_parity", [this, both]() {
            auto [i_out, v_out] = both(
                "class C { int b = 1; void _set_b(int v) { if (v < 0) { throw \"negative\"; } b = v * 10; } int raw() { return b; } }\n"
                "var c = C();\n"
                "var m1 = \"\"; try { c.b = -1; m1 = \"stored\"; } catch (err1) { m1 = err1; }\n"
                "var m2 = \"\"; try { c.b = \"str\"; m2 = \"stored\"; } catch (err2) { m2 = err2; }\n"
                "var m3 = \"\"; try { c.b = 1.5; m3 = \"stored\"; } catch (err3) { m3 = err3; }\n"
                "m1 + \"|\" + m2 + \"|\" + m3 + \"|\" + to_string(c.raw()) + \"|\" + to_string(c.b)");
            check_eq(i_out, v_out, "setter error texts agree across backends");
            check_eq(std::string("negative|No overload of '_set_b' takes (string)|stored|10|10"), i_out, "script throw, overload rejection, float->int conversion");
        });

        test("accessor_ref_binding_is_non_lvalue", [check_both]() {
            check_both(
                "class C { int b = 1; int plain = 2; int _get_b() { return b; } }\n"
                "var c = C();\n"
                "void f(int& r) { r = 9; }\n"
                "var m = \"\"; try { f(c.b); m = \"bound\"; } catch (err) { m = err; }\n"
                "f(c.plain);\n"
                "m + \":\" + to_string(c.plain) + \":\" + to_string(c.b)",
                "Cannot pass non-lvalue to reference parameter:9:1", "an accessor-bearing field is a computed property to a reference");
        });
        // ===== ONE CONVERSION TABLE FOR EVERY TYPED SLOT (detail::convert_for_typed_slot) =====
        // declaration/initializer, `=`, by-value parameter binding, typed return and field store
        // all run the assignment table (docs/static_checking.md rule table); each site owns its
        // rejection text. Every row is byte-identical across backends.

        test("typed_slot_string_accepts_anything_everywhere", [check_both]() {
            check_both(R"JAI(
                string s = 5;
                string fp(string t) { return "param: " + t; }
                string g() { return 5; }
                class H { string s = ""; }
                auto h = H(); h.s = 5;
                s + "|" + fp(5) + "|" + g() + "|" + h.s
            )JAI", "5|param: 5|5|5", "the item: string slots take an int on every path");
            check_both(R"JAI(
                string f(string t) { return t; }
                var n = null;
                f(2.5) + "|" + f(true) + "|" + f('a') + "|" + f(n) + "|" + f(null) + "|" + f([1]) + "|" + f({"k": 1})
            )JAI", "2.500000|true|a|null|null|[array]|[map]", "string parameter: every primitive and container row");
            check_both(R"JAI(
                class X { string to_string() { return "X!"; } }
                class D {}
                string f(string t) { return t; }
                string g() { return X(); }
                string g2() { return 2.5; }
                class H { string s = ""; }
                auto h = H(); h.s = X();
                string a = X(); string b; b = X();
                f(X()) + "|" + f(D()) + "|" + g() + "|" + g2() + "|" + h.s + "|" + a + "|" + b
            )JAI", "X!|[object]|X!|2.500000|X!|X!|X!", "objects render through their script to_string() on every path");
        });

        test("typed_slot_bool_is_truthiness_everywhere", [check_both]() {
            check_both(R"JAI(
                class D {} class B { bool to_bool() { return false; } } class C { int a = 0; }
                bool b(bool x) { return x; }
                string j(bool v) { return to_string(v) + ","; }
                j(b(5)) + j(b(0)) + j(b(2.5)) + j(b("")) + j(b("a")) + j(b(null)) + j(b([])) + j(b([1])) + j(b(D())) + j(b(B())) + j(b(shared_ptr<C>()))
            )JAI", "true,false,true,false,true,false,false,true,true,false,true,", "bool parameter: is_truthy rows incl. to_bool()");
            check_both(R"JAI(
                class D {} class B { bool to_bool() { return false; } }
                bool r1() { return 5; } bool r2() { return ""; } bool r3() { return D(); } bool r4() { return B(); } bool r5() { return [1]; }
                to_string(r1()) + "," + to_string(r2()) + "," + to_string(r3()) + "," + to_string(r4()) + "," + to_string(r5())
            )JAI", "true,false,true,false,true", "bool return: is_truthy rows");
            check_both(R"JAI(
                class D {} class B { bool to_bool() { return false; } }
                class H { bool f = false; }
                auto h = H();
                h.f = [1]; string acc = to_string(h.f);
                h.f = D(); acc = acc + "," + to_string(h.f);
                h.f = B(); acc = acc + "," + to_string(h.f);
                h.f = {"k": 1}; acc = acc + "," + to_string(h.f);
                h.f = []; acc = acc + "," + to_string(h.f);
                acc
            )JAI", "true,true,false,true,false", "bool field: containers and objects convert like a bool local");
            check_both(R"JAI(
                class B { bool to_bool() { return false; } }
                bool p(bool x) { return x; }
                var v = B();
                string r = "";
                if (v) { r = "T"; } else { r = "F"; }
                bool d = v;
                r + to_string(d) + to_string(p(v))
            )JAI", "Ffalsefalse", "bool slots agree with if (x)");
        });

        test("typed_slot_numeric_from_bool_everywhere", [check_both]() {
            check_both(R"JAI(
                int i(int x) { return x; } float fl(float x) { return x; } int r() { return true; } float rf() { return false; }
                to_string(i(true)) + "," + to_string(fl(true)) + "," + to_string(r()) + "," + to_string(rf()) + "," + to_string(i(false))
            )JAI", "1,1.000000,1,0.000000,0", "bool into int/float parameters and returns");
        });

        test("typed_slot_object_to_x_methods_everywhere", [check_both, check_quad]() {
            const char* var_sourced = R"JAI(
                class X { int to_int() { return 7; } float to_float() { return 1.5; } char to_char() { return 'x'; } }
                class H { int i = 0; float f = 0.0; char c = 'a'; }
                var xs = X();
                int a = xs; float b = xs; char c = xs; int d; d = xs;
                auto h = H(); h.i = xs; h.f = xs; h.c = xs;
                int r() { return xs; } int p(int v) { return v; }
                to_string(a) + "," + to_string(b) + "," + to_string(c) + "," + to_string(d) + "," + to_string(h.i) + "," + to_string(h.f) + "," + to_string(h.c) + "," + to_string(r()) + "," + to_string(p(xs))
            )JAI";
            check_quad(var_sourced, "7,1.500000,x,7,7,1.500000,x,7,7", "to_int/to_float/to_char on decl, assign, field, return, param");
            check_both(R"JAI(
                class X { int to_int() { return 7; } float to_float() { return 1.5; } char to_char() { return 'x'; } }
                int a = X(); float b = X(); char c = X();
                to_string(a) + "," + to_string(b) + "," + to_string(c)
            )JAI", "7,1.500000,x", "the checker admits to_X() sources under strict");
            check_both(R"JAI(
                class X { int to_int() { return 7; } }
                class Y { auto to_int() { return X(); } }
                var y = Y();
                string r = ""; try { int v = y; r = "ok"; } catch (e) { r = e; }
                int p(int v) { return v; }
                string r2 = ""; try { p(y); r2 = "ok"; } catch (e) { r2 = e; }
                r + "|" + r2
            )JAI", "Cannot initialize 'v' (declared int) with Y|Cannot pass Y to parameter 'v' of 'p' (declared int)", "an object result of to_X() never converts again");
            check_both(R"JAI(
                class Q { int to_int() { return 1; } }
                var q = Q();
                string r = ""; try { char c = q; r = "ok"; } catch (e) { r = e; }
                r
            )JAI", "Cannot initialize 'c' (declared char) with Q", "no to_char(): char slot rejects");
        });

        test("typed_slot_handle_into_value_slot", [this, check_both, both]() {
            check_both(R"JAI(
                class C { int a = 0; C() {} C(int v) { a = v; } }
                C v = new C(2); C w; w = new C(3); C e = shared_ptr<C>();
                var h = new C(4); C shared = h; shared.a = 9;
                to_string(v.a) + "," + to_string(w.a) + "," + to_string(e.a) + "," + to_string(h.a)
            )JAI", "2,3,0,9", "a shared_ptr<C> handle binds to a C slot and stays a handle (parameter parity)");
            auto [i_out, v_out] = both(R"JAI(
                class C { int a = 0; C() {} C(int v) { a = v; } }
                string pt(C c) { return type_of(c) + ":" + to_string(c == null); }
                C d1 = new C(2); C d2 = shared_ptr<C>();
                string decl = type_of(d1) + ":" + to_string(d1 == null) + "|" + type_of(d2) + ":" + to_string(d2 == null);
                string param = pt(new C(2)) + "|" + pt(shared_ptr<C>());
                to_string(decl == param) + " " + decl
            )JAI");
            check_eq(i_out, v_out, "handle-into-value-slot shape parity");
            check_true(i_out.rfind("true ", 0) == 0, "decl and param agree on the bound shape (got: " + i_out + ")");
        });

        test("typed_slot_null_into_handle_params_and_returns", [check_both]() {
            check_both(R"JAI(
                class C { int a = 0; }
                string p(shared_ptr<C> x) { return to_string(x == null) + ":" + type_of(x); }
                shared_ptr<C> g() { return null; }
                string pw(weak_ptr<C> x) { return to_string(x == null) + ":" + type_of(x); }
                weak_ptr<C> gw() { return null; }
                shared_ptr<C> q = null; weak_ptr<C> qw; qw = null;
                p(null) + "|" + to_string(g() == null) + ":" + type_of(g()) + "|" + pw(null) + "|" + to_string(gw() == null) + ":" + type_of(gw()) + "|" + type_of(q) + "|" + type_of(qw)
            )JAI", "true:object|true:object|true:weak_ptr|true:weak_ptr|object|weak_ptr", "null binds to shared_ptr/weak_ptr parameters and returns as the typed null `=` stores");
        });

        test("typed_slot_no_chained_conversion_anywhere", [check_both]() {
            check_both(R"JAI(
                class TA { int v = 1; } class TB { int v = 0; TB(TA a) { v = a.v * 2; } } class TC { int v = 0; TC(TB b) { v = b.v * 3; } }
                class H { TC t; } class X { int to_int() { return 7; } } class C { int a = 0; C() {} C(int v) { a = v; } }
                var ta = TA(); var xs = X();
                string r1 = ""; try { TC c = ta; r1 = "ok"; } catch (e) { r1 = e; }
                string r2 = ""; try { TC c2; c2 = ta; r2 = "ok"; } catch (e) { r2 = e; }
                string r3 = ""; try { auto h = H(); h.t = ta; r3 = "ok"; } catch (e) { r3 = e; }
                int pc(TC c) { return c.v; }
                string r4 = ""; try { pc(ta); r4 = "ok"; } catch (e) { r4 = e; }
                TC rc() { return ta; }
                string r5 = ""; try { rc(); r5 = "ok"; } catch (e) { r5 = e; }
                string r6 = ""; try { C c = xs; r6 = "ok"; } catch (e) { r6 = e; }
                r1 + "|" + r2 + "|" + r3 + "|" + r4 + "|" + r5 + "|" + r6
            )JAI", "Cannot initialize 'c' (declared TC) with TA|Cannot assign TA to 'c2' (declared TC)|Cannot assign TA to field 't' of H (declared TC)|Cannot convert TA to TC (no suitable single-argument constructor)|Cannot convert TA to TC (no suitable single-argument constructor)|Cannot initialize 'c' (declared C) with X",
                "TA->TB->TC and X->to_int->C are never implicit");
            check_both(R"JAI(
                class TA { int v = 1; } class TB { int v = 0; TB(TA a) { v = a.v * 2; } }
                class HB { TB t; }
                var ta = TA();
                TB b = ta; TB b2; b2 = ta; auto hb = HB(); hb.t = ta;
                int pb(TB b) { return b.v; } TB rb() { return ta; }
                to_string(b.v) + to_string(b2.v) + to_string(hb.t.v) + to_string(pb(ta)) + to_string(rb().v)
            )JAI", "22222", "exactly one user conversion works on every path");
        });

        test("typed_slot_field_ctor_conversion", [this, check_both]() {
            check_both(R"JAI(
                class C { int a = 0; C() {} C(int v) { a = v; } } class D {} class H { C c; } class G { D d; }
                auto h = H(); h.c = 5; string r = to_string(h.c.a); h.c = 2.5; r = r + "," + to_string(h.c.a);
                var t = true;
                try { h.c = t; r = r + ",ok"; } catch (e) { r = r + "," + e; }
                auto g = G(); var c1 = C(1); try { g.d = c1; r = r + ",ok"; } catch (e) { r = r + "," + e; }
                r
            )JAI", "5,2,Cannot assign bool to field 'c' of H (declared C),Cannot assign C to field 'd' of G (declared D)", "field stores construct through a true 1-param ctor; bool needs int<->float or same base");
            // host set_field runs the table without its script rows; hot reload likewise falls back
            // to the initializer default when the live value converts only through script
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute("class C { int a = 0; C() {} C(int v) { a = v; } } class H { C c; string s = \"\"; } auto h = H();");
                auto instance = e->execute("h").as<std::shared_ptr<jai::class_instance>>();
                check_not_null(instance.get());
                bool threw = false;
                try { instance->set_field(e->symbolize("c"), jai::script_value((int64_t)5, e.get())); }
                catch (const std::exception& ex) { threw = std::string(ex.what()).find("Cannot assign int to field 'c' of H (declared C)") != std::string::npos; }
                check(threw, "set_field: ctor conversion is a script row, refused from C++");
                instance->set_field(e->symbolize("s"), jai::script_value((int64_t)5, e.get()));
                check_eq(std::string("5"), e->execute("h.s").as<std::string>(), "set_field: to_string row still converts");
                e->execute("class R { int f = 0; } R r = R(); r.f = 5; class X { int to_int() { return 7; } } class R2 { X f = X(); } R2 r2 = R2();");
                e->execute("class R { C f; }");
                check_eq(std::string("true"), e->execute("r.f == null").to_string(), "reload int->C: ctor-only conversion adopts the default");
                e->execute("class R2 { int f = 3; }");
                check_eq(std::string("3"), e->execute("r2.f").to_string(), "reload X->int: to_int-only conversion adopts the default");
            }
        });

        test("typed_slot_container_boundary_on_params_and_returns", [this, check_both, both]() {
            check_both(R"JAI(
                void pa(array<int> a) {}
                string r1 = ""; try { pa(["x"]); r1 = "ok"; } catch (e) { r1 = e; }
                array<int> ga() { return ["x"]; }
                string r2 = ""; try { ga(); r2 = "ok"; } catch (e) { r2 = e; }
                string pb(array<int> a) { return type_of(a[1]) + to_string(a[1]); }
                string r3 = pb([1, 2.5]);
                array<int> g = [1, 2];
                string pt(array<int> a) { return to_string(a.size()) + type_of(a[0]); }
                r1 + "|" + r2 + "|" + r3 + "|" + pt(g)
            )JAI", "Cannot store string in array<int>|Cannot store string in array<int>|int2|2int", "array<int> parameters and returns run the element boundary");
            auto [i_out, v_out] = both(R"JAI(
                void pc(array<int> a) { a.push("zz"); }
                string r4 = ""; try { pc([1]); r4 = "ok"; } catch (e) { r4 = e; }
                string r5 = ""; try { array<int> x = [1]; x.push("zz"); r5 = "ok"; } catch (e) { r5 = e; }
                void pm(map<string,int> m) {}
                string r6 = ""; try { pm({"k": "v"}); r6 = "ok"; } catch (e) { r6 = e; }
                string r7 = ""; try { map<string,int> m = {"k": "v"}; r7 = "ok"; } catch (e) { r7 = e; }
                to_string(r4 == r5) + ":" + r4 + "|" + to_string(r6 == r7) + ":" + r6
            )JAI");
            check_eq(i_out, v_out, "container parameter texts agree across backends");
            check_true(i_out.rfind("true:Cannot push", 0) == 0 && i_out.find("|true:Cannot store") != std::string::npos,
                "a bound array<int>/map<string,int> parameter is the declared representation (got: " + i_out + ")");
        });

        test("typed_slot_local_container_assign_checks_elements", [check_both]() {
            check_both(R"JAI(
                string f() { array<int> v = [1]; var s = ["x"]; try { v = s; } catch (e) { return e; } return type_of(v[0]); }
                f()
            )JAI", "Cannot store string in array<int>", "a slot-local `=` into array<int> runs the boundary like the global path");
            check_both(R"JAI(
                string f() { map<string,int> m = {"a": 1}; var s = {"k": "v"}; string r = ""; try { m = s; r = "ok"; } catch (e) { r = e; } return r + ":" + to_string(m.size()); }
                f()
            )JAI", "Cannot store string in map<string,int>:1", "a slot-local `=` into map<string,int> runs the boundary");
        });

        test("typed_slot_rejection_texts", [check_both]() {
            check_both(R"JAI(
                void i(int x) {} var n = null; string r1 = ""; try { i(n); } catch (e) { r1 = e; }
                class A {} class B {} void fa(A a) {} var b = B(); string r2 = ""; try { fa(b); } catch (e) { r2 = e; }
                class C { int a = 0; } void sp(shared_ptr<C> p) {} var five = 5; string r3 = ""; try { sp(five); } catch (e) { r3 = e; }
                var s = "s"; int r() { return s; } string r4 = ""; try { r(); } catch (e) { r4 = e; }
                void wp(weak_ptr<C> p) {} string r5 = ""; try { wp(five); } catch (e) { r5 = e; }
                void pa(array<int> a) {} string r6 = ""; try { pa(five); } catch (e) { r6 = e; }
                var arr = [1]; string r7 = ""; try { i(arr); } catch (e) { r7 = e; }
                r1 + "|" + r2 + "|" + r3 + "|" + r4 + "|" + r5 + "|" + r6 + "|" + r7
            )JAI", "Cannot pass null to parameter 'x' of 'i' (declared int)|Cannot convert B to A (no suitable single-argument constructor)|Cannot pass int to parameter 'p' of 'sp' (declared shared_ptr<C>)|Cannot return string from 'r' (declared -> int)|Cannot pass int to parameter 'p' of 'wp' (declared weak_ptr<C>)|Cannot pass int to parameter 'a' of 'pa' (declared array<int>)|Cannot pass array to parameter 'x' of 'i' (declared int)",
                "source spellings are the payload's type, targets their canonical name");
        });

        test("typed_slot_throwing_conversion_propagates", [check_both]() {
            check_both(R"JAI(
                class T { int v = 0; T(int x) { throw "boom"; } }
                var five = 5;
                string r1 = ""; try { T t = five; } catch (e) { r1 = e; }
                string r2 = ""; try { T t2; t2 = five; } catch (e) { r2 = e; }
                class H { T t; } string r3 = ""; try { auto h = H(); h.t = five; } catch (e) { r3 = e; }
                void p(T t) {} string r4 = ""; try { p(five); } catch (e) { r4 = e; }
                T g() { return five; } string r5 = ""; try { g(); } catch (e) { r5 = e; }
                r1 + "|" + r2 + "|" + r3 + "|" + r4 + "|" + r5
            )JAI", "boom|boom|boom|boom|boom", "a converting constructor's own error surfaces on every path");
            check_both(R"JAI(
                class X { int to_int() { throw "bang"; } }
                var xs = X();
                string r1 = ""; try { int a = xs; } catch (e) { r1 = e; }
                int p(int v) { return v; } string r2 = ""; try { p(xs); } catch (e) { r2 = e; }
                r1 + "|" + r2
            )JAI", "bang|bang", "a throwing to_X() surfaces on every path");
        });

        test("typed_slot_proof_lanes", [check_proof_run]() {
            check_proof_run({"string f(string t) { return t + \"!\"; } string r = f(5); r = r + f(2.5); r"}, "5!2.500000!", "string parameter bound from an int under every lane");
            check_proof_run({"int sum(array<int> a) { int s = 0; for (auto x : a) { s += x; } return s; } to_string(sum([1, 2.5, 3]))"}, "6", "array<int> parameter bound from a hetero literal under every lane");
            check_proof_run({"bool b(bool x) { return x; } to_string(b(5)) + to_string(b(0))"}, "truefalse", "bool parameter bound from an int under every lane");
        });

    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::strong_types_tests)
