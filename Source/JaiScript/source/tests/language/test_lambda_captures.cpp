#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <format>

namespace jai::foundry::tests {

class lambda_capture_tests : public suite {
public:
    lambda_capture_tests() : suite("Lambda Captures") {}
    
    void forge_tests() override {
        test("no_capture_lambda", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("[](auto x) -> auto { return x + 1; }(5)");
            check_eq(result.as<int>(), 6);
        });
        
        test("empty_default_capture", [this]() {
            auto eng = make_engine();
            try {
                auto result = eng->execute("[=](auto x) -> auto { return x + 1; }(5)");
                check_eq(result.as<int>(), 6);
            } catch (const std::exception& e) {
                throw test_failure(std::format("Failed with [=]: {}", e.what()));
            }
        });

        test("capture_all_by_value", [this]() {
            auto eng = make_engine();
            
            std::string script = R"(
                auto x = 10;
                auto y = 20;
                auto z = 30;
                
                auto lambda = [=](auto bonus) -> auto {
                    return x + y + z + bonus;
                };
                
                x = 100;  // Change variables after lambda creation
                y = 200;
                z = 300;
                
                lambda(5);  // Should use original values: 10 + 20 + 30 + 5 = 65
            )";
            
            auto result = eng->execute(script);
            check_eq(result.as<int>(), 65);
        });

        test("capture_all_by_reference", [this]() {
            auto eng = make_engine();
            
            std::string script = R"(
                auto a = 1;
                auto b = 2;
                
                auto lambda = [&](auto multiplier) -> auto {
                    a = a * multiplier;
                    b = b * multiplier;
                    return a + b;
                };
                
                auto result1 = lambda(10);  // a=10, b=20, result=30
                auto result2 = lambda(2);   // a=20, b=40, result=60
                
                result1 * 100 + result2;   // Should be 3060
            )";
            
            auto result = eng->execute(script);
            check_eq(result.as<int>(), 3060);
        });

        test("mixed_capture_value_default_with_ref", [this]() {
            auto eng = make_engine();
            
            std::string script = R"(
                auto x = 10;
                auto y = 20;
                auto counter = 0;
                
                auto lambda = [=, &counter](auto bonus) -> auto {
                    counter = counter + 1;
                    return x + y + bonus + counter;
                };
                
                x = 100;  // x should still be 10 in lambda
                y = 200;  // y should still be 20 in lambda
                
                auto result1 = lambda(1);  // 10 + 20 + 1 + 1 = 32
                auto result2 = lambda(2);  // 10 + 20 + 2 + 2 = 34
                
                result1 * 100 + result2;   // Should be 3234
            )";
            
            auto result = eng->execute(script);
            check_eq(result.as<int>(), 3234);
        });

        test("explicit_captures", [this]() {
            auto eng = make_engine();
            
            std::string script = R"(
                auto x = 10;
                auto y = 20;
                
                auto lambda = [x, y](auto bonus) -> auto {
                    return x + y + bonus;
                };
                
                x = 100;  // Should not affect lambda
                y = 200;  // Should not affect lambda
                
                lambda(5);  // Should be 10 + 20 + 5 = 35
            )";
            
            auto result = eng->execute(script);
            check_eq(result.as<int>(), 35);
        });

        test("reference_capture_modifies_original", [this]() {
            auto eng = make_engine();
            
            std::string script = R"(
                auto counter = 0;
                auto increment = [&counter]() -> auto {
                    counter = counter + 1;
                    return counter;
                };
                
                auto val1 = increment();  // Should be 1
                auto val2 = increment();  // Should be 2
                counter;                  // Check final counter value
            )";
            
            auto result = eng->execute(script);
            check_eq(result.as<int>(), 2);
        });

        // Debug test - simple case
        test("debug_simple_default_capture", [this]() {
            auto eng = make_engine();
            
            // This is the simplest possible test of [=]
            auto result = eng->execute(R"(
                auto f = [=]() -> auto { return 42; };
                f()
            )");
            check_eq(result.as<int>(), 42);
        });

        // Debug test - parameter only
        test("debug_parameter_with_default_capture", [this]() {
            auto eng = make_engine();
            
            // Test [=] with only parameter usage
            try {
                auto result = eng->execute(R"(
                    auto f = [=](auto x) -> auto { return x; };
                    f(99)
                )");
                check_eq(result.as<int>(), 99);
            } catch (const std::exception& e) {
                throw test_failure(std::format("Parameter lookup failed: {}", e.what()));
            }
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
using lambda_capture_tests = jai::foundry::tests::lambda_capture_tests;
FOUNDRY_REGISTER(lambda_capture_tests)