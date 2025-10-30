#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {
namespace this_lambda_minimal_ns {

class this_lambda_minimal_tests : public suite {
public:
    this_lambda_minimal_tests() : suite("Minimal This Lambda Tests") {}

    void forge_tests() override {
        test("minimal_this_capture_in_method", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            try {
                std::cout << "\n=== Test: minimal_this_capture_in_method ===" << std::endl;

                auto result = eng->execute(R"(
                    class TestClass {
                        auto value;

                        TestClass(auto v) {
                            print("Constructor called with: " + v);
                            value = v;
                        }

                        void call_lambda() {
                            print("call_lambda: start");
                            auto lambda = [this]() {
                                print("Inside lambda, value = " + value);
                                return value;
                            };
                            print("call_lambda: lambda created, about to call");
                            auto result = lambda();
                            print("call_lambda: lambda returned " + result);
                            return result;
                        }
                    }

                    print("Creating TestClass");
                    auto obj = TestClass(42);
                    print("Calling call_lambda");
                    auto result = obj.call_lambda();
                    print("Result: " + result);
                    result
                )");

                std::cout << "Final result: " << result.to_string() << std::endl;
                check_eq(result.as<int>(), 42);
            } catch (const std::exception& e) {
                std::cout << "Exception: " << e.what() << std::endl;
                throw test_failure("Exception: " + std::string(e.what()));
            }
        });

        test("void_method_with_lambda", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            try {
                std::cout << "\n=== Test: void_method_with_lambda ===" << std::endl;

                auto result = eng->execute(R"(
                    class TestClass {
                        auto value;

                        TestClass(auto v) {
                            print("Constructor: " + v);
                            value = v;
                        }

                        void method_void() {
                            print("method_void: start");
                            auto lambda = [this]() {
                                print("lambda: value = " + value);
                            };
                            print("method_void: calling lambda");
                            lambda();
                            print("method_void: done");
                        }
                    }

                    print("Creating object");
                    auto obj = TestClass(99);
                    print("Calling method_void");
                    obj.method_void();
                    print("All done");
                    true
                )");

                check_eq(result.as<bool>(), true);
            } catch (const std::exception& e) {
                std::cout << "Exception: " << e.what() << std::endl;
                throw test_failure("Exception: " + std::string(e.what()));
            }
        });
    }
};

} // namespace this_lambda_minimal_ns
} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::this_lambda_minimal_ns::this_lambda_minimal_tests)
