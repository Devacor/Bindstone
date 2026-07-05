#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai::foundry;

namespace jai::foundry::tests {

class exception_handling_tests : public suite {
public:
    exception_handling_tests() : suite("Exception Handling") {}
    
    void forge_tests() override {
        test("basic_throw_catch", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                try {
                    throw "Test error message";
                } catch (e) {
                    return e;
                }
            )";
            
            script_value result = engine->execute(script);
            check_eq(result.is_string(), true);
            check_eq(result.as_string(), "Test error message");
        });
        
        test("throw_without_catch_bubbles_to_cpp", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                throw "Unhandled exception";
            )";
            
            bool caught_exception = false;
            try {
                engine->execute(script);
            } catch (const script_exception& e) {
                caught_exception = true;
                check_eq(std::string(e.what()), "Unhandled exception");
            }
            
            check_eq(caught_exception, true);
        });
        
        test("catch_without_variable", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                try {
                    throw "Error message";
                } catch {
                    return "Caught without variable";
                }
            )";
            
            script_value result = engine->execute(script);
            check_eq(result.is_string(), true);
            check_eq(result.as_string(), "Caught without variable");
        });
        
        test("nested_try_catch", [this]() {
            auto engine = make_engine();

            std::string script = R"(
                try {
                    try {
                        throw "Inner error";
                    } catch (inner) {
                        throw "Outer error: " + inner;
                    }
                } catch (outer) {
                    return outer;
                }
            )";

            script_value result = engine->execute(script);
            std::cout << "  Result type: " << static_cast<int>(result.type()) << "\n";
            if (result.is_string()) {
                std::cout << "  Result value: '" << result.as_string() << "'\n";
            } else {
                std::cout << "  Result is not a string\n";
            }
            check_eq(result.is_string(), true);
            check_eq(result.as_string(), "Outer error: Inner error");
        });
        
        test("cpp_runtime_error_interop", [this]() {
            auto engine = make_engine();
            
            // Add a C++ function that throws std::runtime_error
            engine->add_function("risky_function", []() {
                throw std::runtime_error("C++ function error");
            });
            
            std::string script = R"(
                try {
                    risky_function();
                } catch (e) {
                    return e;
                }
            )";
            
            script_value result = engine->execute(script);
            if (!result.is_string() || result.as_string() != "C++ function error") {
                std::cout << "\ncpp_runtime_error_interop FAILURE:" << std::endl;
                std::cout << "  Expected: 'C++ function error'" << std::endl;
                std::cout << "  Actual: '" << (result.is_string() ? result.as_string() : "<not a string>") << "'" << std::endl;
                std::cout << "  Is int: " << result.is_int() << ", Is string: " << result.is_string() << std::endl;
            }
            check_eq(result.is_string(), true);
            check_eq(result.as_string(), "C++ function error");
        });
        
        test("cpp_generic_exception_interop", [this]() {
            auto engine = make_engine();
            
            // Add a C++ function that throws generic std::exception
            engine->add_function("generic_exception", []() {
                throw std::logic_error("Generic C++ error");
            });
            
            std::string script = R"(
                try {
                    generic_exception();
                } catch (e) {
                    return e;
                }
            )";
            
            script_value result = engine->execute(script);
            if (!result.is_string() || result.as_string() != "Generic C++ error") {
                std::cout << "\ncpp_generic_exception_interop FAILURE:" << std::endl;
                std::cout << "  Expected: 'Generic C++ error'" << std::endl;
                std::cout << "  Actual: '" << (result.is_string() ? result.as_string() : "<not a string>") << "'" << std::endl;
                std::cout << "  Is int: " << result.is_int() << ", Is string: " << result.is_string() << std::endl;
            }
            check_eq(result.is_string(), true);
            check_eq(result.as_string(), "Generic C++ error");
        });
        
        test("throw_rethrow", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                try {
                    throw "Original error";
                } catch (e) {
                    try {
                        throw;  // Re-throw
                    } catch (re) {
                        return re;
                    }
                }
            )";
            
            script_value result = engine->execute(script);
            check_eq(result.is_string(), true);
            check_eq(result.as_string(), "Original error");
        });
        
        test("throw_in_expressions", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                auto result = "Start";
                try {
                    result = result + " " + throw "Error in expression";
                } catch (e) {
                    result = result + " caught: " + e;
                }
                return result;
            )";
            
            script_value result = engine->execute(script);
            check_eq(result.is_string(), true);
            check_eq(result.as_string(), "Start caught: Error in expression");
        });
        
        test("exception_with_numeric_values", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                try {
                    throw 42;
                } catch (e) {
                    return e;
                }
            )";
            
            script_value result = engine->execute(script);
            check_eq(result.is_int(), true);
            check_eq(result.as<int>(), 42);
        });
        
        test("exception_scope_isolation", [this]() {
            auto engine = make_engine();
            
            std::string script = R"(
                auto outer_var = "outer";
                try {
                    auto inner_var = "inner";
                    throw "test";
                } catch (e) {
                    // inner_var should not be accessible here
                    return outer_var + " " + e;
                }
            )";
            
            script_value result = engine->execute(script);
            check_eq(result.is_string(), true);
            check_eq(result.as_string(), "outer test");
        });

        test("throw_captures_nested_stack_trace", [this]() {
            auto engine = make_engine();
            const char* src =
                "auto inner() { throw \"boom\"; }\n"   // line 1
                "auto middle() { return inner(); }\n"  // line 2
                "auto outer() { return middle(); }\n"  // line 3
                "outer();\n";                          // line 4
            try { engine->execute(src); } catch (...) {}

            auto trace = engine->last_stack_trace();
            check_ge(trace.size(), (size_t)4);
            check_eq(std::string("inner"), trace[0].function);
            check_eq(std::string("middle"), trace[1].function);
            check_eq(std::string("outer"), trace[2].function);
            check_eq(std::string("<script>"), trace[3].function);
            check_eq((size_t)1, trace[0].line);
            check_eq((size_t)2, trace[1].line);
            check_eq((size_t)3, trace[2].line);
            check_eq((size_t)4, trace[3].line);
        });

        test("top_level_throw_captures_script_frame", [this]() {
            auto engine = make_engine();
            try { engine->execute("throw \"x\";"); } catch (...) {}
            auto trace = engine->last_stack_trace();
            check_false(trace.empty());
            check_eq(std::string("<script>"), trace.back().function);
        });

        test("format_stack_trace_lists_frames", [this]() {
            auto engine = make_engine();
            try { engine->execute("auto f() { throw \"x\"; }\nf();\n"); } catch (...) {}
            auto s = engine->format_stack_trace();
            check_true(s.find("at f") != std::string::npos);
        });

        test("runtime_error_captures_nested_stack_trace", [this]() {
            auto engine = make_engine();
            const char* src =
                "auto inner() { return missingVar; }\n"  // line 1: undefined variable
                "auto middle() { return inner(); }\n"     // line 2
                "auto outer() { return middle(); }\n"     // line 3
                "outer();\n";                             // line 4
            try { engine->execute(src); } catch (...) {}
            auto trace = engine->last_stack_trace();
            check_ge(trace.size(), (size_t)4);
            check_eq(std::string("inner"), trace[0].function);
            check_eq(std::string("middle"), trace[1].function);
            check_eq(std::string("outer"), trace[2].function);
            check_eq((size_t)1, trace[0].line);
        });

        test("method_throw_captures_stack_trace", [this]() {
            auto engine = make_engine();
            const char* src =
                "class Foo {\n"
                "    auto boom() { throw \"x\"; }\n"   // line 2
                "}\n"
                "auto f = Foo();\n"
                "f.boom();\n";                          // line 5
            try { engine->execute(src); } catch (...) {}
            auto trace = engine->last_stack_trace();
            check_false(trace.empty());
            check_eq((size_t)2, trace[0].line);
            check_eq(std::string("boom"), trace[0].function);
        });

        test("lambda_throw_captures_stack_trace", [this]() {
            auto engine = make_engine();
            const char* src =
                "auto fn = []() -> auto { throw \"x\"; };\n"  // line 1
                "fn();\n";                                     // line 2
            try { engine->execute(src); } catch (...) {}
            auto trace = engine->last_stack_trace();
            check_false(trace.empty());
            check_eq((size_t)1, trace[0].line);
        });

        // A throw during argument evaluation must propagate the thrown value; it must not
        // leak into the outer call's parameter binding and get replaced by a binding error.
        test("throw_during_argument_evaluation_caught", [this]() {
            auto engine = make_engine();
            script_value result = engine->execute(R"(
                auto h() { throw "argfail"; }
                auto g(int a, int b) { return "g"; }
                auto caller() {
                    try { return g(1, h()); } catch (e) { return "c:" + e; }
                }
                return caller() + "|" + caller();
            )");
            check_eq(std::string("c:argfail|c:argfail"), result.as_string());
        });

        test("throw_during_argument_evaluation_native_callee", [this]() {
            auto engine = make_engine();
            engine->add_function("gn", [](int a, int b) { return std::string("gn"); });
            script_value result = engine->execute(R"(
                auto h() { throw "argfail"; }
                try { return "r:" + gn(1, h()); } catch (e) { return "c:" + e; }
            )");
            check_eq(std::string("c:argfail"), result.as_string());
        });

        test("throw_during_argument_evaluation_method_callee", [this]() {
            auto engine = make_engine();
            script_value result = engine->execute(R"(
                auto h() { throw "argfail"; }
                class M { auto m(int a, int b) { return "m"; } }
                var o = M();
                try { return "r:" + o.m(1, h()); } catch (e) { return "c:" + e; }
            )");
            check_eq(std::string("c:argfail"), result.as_string());
        });

        test("throw_during_argument_evaluation_indirect_callee", [this]() {
            auto engine = make_engine();
            script_value result = engine->execute(R"(
                auto h() { throw "argfail"; }
                auto g(int a, int b) { return "g"; }
                var hv = h;
                try { return "r:" + g(1, hv()); } catch (e) { return "c:" + e; }
            )");
            check_eq(std::string("c:argfail"), result.as_string());
        });

        test("throw_during_argument_evaluation_uncaught_text", [this]() {
            auto engine = make_engine();
            std::string got;
            try {
                engine->execute(
                    "auto h() { throw \"argfail\"; }\n"
                    "auto g(int a, int b) { return \"g\"; }\n"
                    "g(1, h());\n");
            } catch (const std::exception& e) {
                got = e.what();
            }
            check_eq(std::string("argfail"), got);
        });

        test("throw_during_callee_evaluation", [this]() {
            auto engine = make_engine();
            script_value result = engine->execute(R"(
                auto h() { throw "calleefail"; }
                try { h()(); } catch (e) { return "c:" + e; }
            )");
            check_eq(std::string("c:calleefail"), result.as_string());
        });

        // Regression: caught typed-return conversion error left the callee frame/env and
        // hasReturnValue_ live - catch body truncated, post-try statements skipped
        test("caught_return_conversion_error_full_recovery", [this]() {
            auto engine = make_engine();
            script_value result = engine->execute(R"(
                function bad() -> int { return "nope"; }
                var log = "";
                try { bad(); log += "no"; } catch (e) { log += "a"; log += "b"; }
                log += "c";
                log;
            )");
            check_eq(std::string("abc"), result.as_string());
        });

        test("caught_return_conversion_error_enclosing_typed_return", [this]() {
            auto engine = make_engine();
            script_value result = engine->execute(R"(
                function bad() -> int { return "nope"; }
                function outer() -> int {
                    var seen = 0;
                    try { bad(); } catch (e) { seen = 1; }
                    return 7 + seen;
                }
                outer();
            )");
            check_eq((int64_t)8, result.as_int());
        });

        test("caught_arg_conversion_error_full_recovery", [this]() {
            auto engine = make_engine();
            script_value result = engine->execute(R"(
                function takes(int x) -> int { return x; }
                var log = "";
                try { takes([1, 2]); log += "no"; } catch (e) { log += "a"; }
                log += "b";
                log;
            )");
            check_eq(std::string("ab"), result.as_string());
            check_eq((int64_t)3, engine->execute("takes(3);").as_int());
        });

        test("throw_during_string_method_argument", [this]() {
            auto engine = make_engine();
            script_value result = engine->execute(R"(
                auto h() { throw "strfail"; }
                var s = "abc";
                try { var n = s.find(h()); return "r:found"; } catch (e) { return "c:" + e; }
            )");
            check_eq(std::string("c:strfail"), result.as_string());
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::exception_handling_tests)