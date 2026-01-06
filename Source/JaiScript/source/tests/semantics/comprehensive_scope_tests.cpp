#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>
#include <vector>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Global log to track construction/destruction order
static std::vector<std::string> operation_log;

// C++ class bound via dynamic_binder
class CppBoundObject {
public:
    static int instance_count;  // Total created (for unique IDs)
    static int alive_count;     // Currently alive
    int id;
    std::string name;

    CppBoundObject(const std::string& n) : id(++instance_count), name(n) {
        alive_count++;
        operation_log.push_back("CppBoundObject(\"" + name + "\") ctor, id=" + std::to_string(id) + ", alive=" + std::to_string(alive_count));
    }

    CppBoundObject(const CppBoundObject& other) : id(other.id), name(other.name) {
        alive_count++;  // Copies also increase alive count
        operation_log.push_back("CppBoundObject COPY from id=" + std::to_string(other.id) + ", alive=" + std::to_string(alive_count));
    }

    ~CppBoundObject() {
        alive_count--;
        operation_log.push_back("~CppBoundObject(\"" + name + "\") dtor, id=" + std::to_string(id) + ", alive=" + std::to_string(alive_count));
    }

    void instance_method() {
        operation_log.push_back("CppBoundObject::instance_method() called on \"" + name + "\"");
    }

    static void static_method() {
        operation_log.push_back("CppBoundObject::static_method() called");
    }

    std::string get_name() const { return name; }
    int get_id() const { return id; }

    static void reset() {
        instance_count = 0;
        alive_count = 0;
        operation_log.clear();
    }
};

int CppBoundObject::instance_count = 0;
int CppBoundObject::alive_count = 0;

void register_cpp_bound_object(engine& eng) {
    dynamic_binder<CppBoundObject>(eng, "CppBoundObject")
        .constructor<std::string>()
        .method("instance_method", &CppBoundObject::instance_method)
        .method("get_name", &CppBoundObject::get_name)
        .method("get_id", &CppBoundObject::get_id)
        .property("name", &CppBoundObject::name)
        .property("id", &CppBoundObject::id)
        .static_method("static_method", &CppBoundObject::static_method)
        .static_property("instance_count", &CppBoundObject::instance_count)
        .static_property("alive_count", &CppBoundObject::alive_count)
        .build();
}

class comprehensive_scope_tests : public suite {
public:
    comprehensive_scope_tests() : suite("Comprehensive Scope Tests") {}

    void forge_tests() override {
        test("block_scope_destruction", [this]() {
            auto eng = engine::make();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                // Test 1: Simple block scope
                {
                    auto obj1 = CppBoundObject("block_scope");
                }
                // obj1 should be destroyed here

                // Test 2: Nested blocks
                {
                    auto outer = CppBoundObject("outer_block");
                    {
                        auto inner = CppBoundObject("inner_block");
                    }
                    // inner should be destroyed here
                }
                // outer should be destroyed here

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "All C++ objects should be destroyed");

            // Verify construction/destruction order
            std::cout << "\n=== Operation Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }

            // Should have: ctor block_scope, dtor block_scope, ctor outer, ctor inner, dtor inner, dtor outer
            check(operation_log.size() >= 6, "Should have at least 6 operations");
        });

        test("if_statement_scope", [this]() {
            auto eng = engine::make();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                auto condition = true;

                if (condition) {
                    auto if_obj = CppBoundObject("if_true_branch");
                }
                // if_obj should be destroyed here

                if (!condition) {
                    auto else_obj = CppBoundObject("if_false_branch");
                } else {
                    auto else_true = CppBoundObject("else_branch");
                }
                // else_true should be destroyed here

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "All if-scope objects destroyed");

            std::cout << "\n=== If Statement Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }
        });

        test("for_loop_scope", [this]() {
            auto eng = engine::make();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                // Test for loop scope with initialization variable
                for (auto i = 0; i < 3; i = i + 1) {
                    auto loop_obj = CppBoundObject("loop_iteration");
                }
                // loop_obj should be destroyed after each iteration
                // i should be destroyed when for loop ends

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "All loop objects destroyed");

            std::cout << "\n=== For Loop Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }

            // Should see 3 constructions and 3 destructions
            int ctor_count = 0;
            int dtor_count = 0;
            for (const auto& op : operation_log) {
                if (op.find("ctor") != std::string::npos) ctor_count++;
                if (op.find("dtor") != std::string::npos) dtor_count++;
            }
            check_eq(ctor_count, 3, "Should construct 3 objects in loop");
            check_eq(dtor_count, 3, "Should destruct 3 objects in loop");
        });

        test("function_scope", [this]() {
            auto eng = engine::make();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                void test_function() {
                    auto func_local = CppBoundObject("function_local");
                }
                // func_local should be destroyed when function returns

                test_function();
                test_function();  // Call twice to verify destruction each time

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "All function locals destroyed");

            std::cout << "\n=== Function Scope Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }

            // Should see 2 constructions and 2 destructions
            int ctor_count = 0;
            int dtor_count = 0;
            for (const auto& op : operation_log) {
                if (op.find("ctor") != std::string::npos) ctor_count++;
                if (op.find("dtor") != std::string::npos) dtor_count++;
            }
            check_eq(ctor_count, 2, "Should construct object twice");
            check_eq(dtor_count, 2, "Should destruct object twice");
        });

        test("lambda_scope", [this]() {
            auto eng = engine::make();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                auto make_lambda = []() {
                    auto lambda_local = CppBoundObject("lambda_local");
                    return lambda_local.get_id();
                };

                auto id1 = make_lambda();
                auto id2 = make_lambda();

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "All lambda locals destroyed");

            std::cout << "\n=== Lambda Scope Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }
        });

        test("script_class_with_destructor", [this]() {
            auto eng = engine::make();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                class ScriptClass {
                    auto cpp_member;

                    ScriptClass(auto name) {
                        cpp_member = CppBoundObject(name);
                    }

                    ~ScriptClass() {
                        // Destructor should be called automatically
                        // cpp_member will be destroyed when ScriptClass is destroyed
                    }

                    void instance_method() {
                        cpp_member.instance_method();
                    }

                    static void static_method() {
                        CppBoundObject::static_method();
                    }
                }

                // Test script class scope
                {
                    auto script_obj = ScriptClass("script_class_member");
                    script_obj.instance_method();
                }
                // script_obj and its cpp_member should both be destroyed here

                ScriptClass::static_method();

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "Script class member destroyed");

            std::cout << "\n=== Script Class Destructor Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }
        });

        test("mixed_scopes_comprehensive", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);  // Register stdlib to get print()
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                // Global scope object (will live until script ends)
                auto global_obj = CppBoundObject("global");

                class ScriptClassWithDestructor {
                    auto member;

                    ScriptClassWithDestructor(auto name) {
                        member = CppBoundObject(name + "_member");
                    }

                    ~ScriptClassWithDestructor() {
                        // member will be destroyed automatically
                    }

                    void call_lambda() {
                        print("call_lambda: before creating lambda");
                        auto lambda = [this]() {
                            print("lambda: inside lambda body");
                            auto lambda_obj = CppBoundObject("lambda_capture");
                            member.instance_method();
                        };
                        print("call_lambda: after creating lambda, before calling");
                        lambda();
                        print("call_lambda: after calling lambda");
                        // lambda_obj destroyed here
                    }
                }

                void test_function() {
                    auto func_obj = CppBoundObject("function");

                    for (auto i = 0; i < 2; i = i + 1) {
                        auto loop_obj = CppBoundObject("loop");

                        if (i == 0) {
                            auto if_obj = CppBoundObject("if_branch");
                        }
                        // if_obj destroyed here if created
                        // loop_obj destroyed here
                    }
                    // i destroyed here
                    // func_obj destroyed here
                }

                {
                    auto block_obj = CppBoundObject("block");
                    auto script_class = ScriptClassWithDestructor("script_class");
                    script_class.call_lambda();
                    test_function();
                    // script_class destroyed here (and its member)
                    // block_obj destroyed here
                }

                // global_obj still alive

                true
            )");

            check_eq(result.as<bool>(), true);

            // global_obj is still alive, so alive_count should be 1
            check_eq(CppBoundObject::alive_count, 1, "Only global object should remain");

            std::cout << "\n=== Comprehensive Mixed Scopes Log ===" << std::endl;
            for (size_t i = 0; i < operation_log.size(); ++i) {
                std::cout << i << ": " << operation_log[i] << std::endl;
            }

            // Verify we have destructor calls for everything except global
            int ctor_count = 0;
            int dtor_count = 0;
            for (const auto& op : operation_log) {
                // Count both "ctor" and "COPY" as constructions
                if (op.find("ctor") != std::string::npos || op.find("COPY") != std::string::npos) ctor_count++;
                if (op.find("dtor") != std::string::npos) dtor_count++;
            }

            std::cout << "\nCtor count: " << ctor_count << ", Dtor count: " << dtor_count << std::endl;

            // Should have ctor_count = dtor_count + 1 (the global object)
            check_eq(ctor_count, dtor_count + 1, "All non-global objects should be destroyed");
        });

        test("early_return_scope_cleanup", [this]() {
            auto eng = engine::make();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                int test_early_return(auto should_return) {
                    auto before_if = CppBoundObject("before_if");

                    if (should_return) {
                        auto in_if = CppBoundObject("in_if");
                        return 42;
                        // in_if should be destroyed here
                    }
                    // before_if should be destroyed here if we don't return early

                    return 0;
                }

                auto result1 = test_early_return(true);   // early return
                auto result2 = test_early_return(false);  // normal return

                result1 == 42 && result2 == 0
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "All objects destroyed despite early return");

            std::cout << "\n=== Early Return Scope Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }
        });

        test("exception_scope_cleanup", [this]() {
            auto eng = engine::make();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                void throwing_function() {
                    auto before_throw = CppBoundObject("before_throw");
                    // In the future when exceptions are supported, test:
                    // throw "error";
                    // before_throw should be destroyed here
                }

                // For now, just test normal flow
                throwing_function();

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "Objects destroyed in exception scenarios");
        });
    }

    void pre_test() override {
        CppBoundObject::reset();
    }

    void post_test() override {
        // Verify no leaks after each test
        if (CppBoundObject::alive_count > 1) {  // Allow 1 for global_obj in comprehensive test
            std::cerr << "WARNING: Potential memory leak, alive_count = "
                     << CppBoundObject::alive_count << std::endl;
        }
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::comprehensive_scope_tests)
