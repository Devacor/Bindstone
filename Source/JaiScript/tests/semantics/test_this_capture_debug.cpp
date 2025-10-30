#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {
namespace this_capture_debug_ns {

class this_capture_debug_tests : public suite {
public:
    this_capture_debug_tests() : suite("this capture debug tests") {}

    void forge_tests() override {
        test("simple this capture in method", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            try {
                auto result = eng->execute(R"(
                    class TestClass {
                        auto value;

                        TestClass(auto v) {
                            value = v;
                            print("TestClass created with value: " + value);
                        }

                        auto test_lambda() {
                            print("Before lambda creation");
                            auto lambda = [this]() {
                                print("Inside lambda, value = " + value);
                                return value * 2;
                            };
                            print("After lambda creation, before call");
                            auto result = lambda();
                            print("Lambda returned: " + result);
                            return result;
                        }
                    }

                    auto obj = TestClass(42);
                    auto result = obj.test_lambda();
                    print("Final result: " + result);
                    result
                )");

                check_eq(result.as<int>(), 84);
            } catch (const std::exception& e) {
                throw test_failure("Exception: " + std::string(e.what()));
            }
        });
    }
};

} // namespace this_capture_debug_ns
} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::this_capture_debug_ns::this_capture_debug_tests)
