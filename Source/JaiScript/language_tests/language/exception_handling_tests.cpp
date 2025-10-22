#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai::foundry;

namespace jai::foundry::tests {

class exception_handling_tests : public suite {
public:
    exception_handling_tests() : suite("Exception Handling") {}
    
    void forge_tests() override {
        test("basic_throw_catch", [this]() {
            auto engine = engine::make();
            
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
            auto engine = engine::make();
            
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
            auto engine = engine::make();
            
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
            auto engine = engine::make();
            
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
            check_eq(result.is_string(), true);
            check_eq(result.as_string(), "Outer error: Inner error");
        });
        
        test("cpp_runtime_error_interop", [this]() {
            auto engine = engine::make();
            
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
            auto engine = engine::make();
            
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
            auto engine = engine::make();
            
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
            auto engine = engine::make();
            
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
            auto engine = engine::make();
            
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
            auto engine = engine::make();
            
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
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::exception_handling_tests)