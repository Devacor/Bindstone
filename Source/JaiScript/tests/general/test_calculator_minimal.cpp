#include <jaiscript/core/engine.hpp>
#include <iostream>

int main() {
    try {
        auto engine = jai::engine::make();

        // Add test functions like in the real test
        engine->add_function("log_action", [](const std::string& msg) {
            std::cout << "LOG: " << msg << std::endl;
            return jai::script_value(std::monostate{}, std::weak_ptr<jai::engine>{});
        });

        engine->add_function("check_value", [](const std::string& desc, bool result) {
            std::cout << "CHECK: " << desc << " = " << (result ? "PASS" : "FAIL") << std::endl;
            return jai::script_value(std::monostate{}, std::weak_ptr<jai::engine>{});
        });

        std::cout << "=== TEST 1: Exact script from test ===" << std::endl;
        auto result1 = engine->execute(R"(
            auto global_calc = null;  // Declare global variable first

            class Calculator {
                auto result = 0.0;
                auto memory = 0.0;

                void add(x) {
                    result = result + x;
                    log_action("add called");
                }

                void multiply(x) {
                    result = result * x;
                    log_action("multiply called");
                }

                void store() {
                    memory = result;
                    log_action("store called");
                }

                void recall() {
                    result = memory;
                    log_action("recall called");
                }
            }

            global_calc = Calculator();
            global_calc.add(5);
            global_calc.multiply(3);
            global_calc.store();

            check_value("result is 15.0", global_calc.result == 15.0);
            check_value("memory is 15.0", global_calc.memory == 15.0);

            return global_calc.result;
        )");

        std::cout << "First execute successful!" << std::endl;
        std::cout << "Returned value: " << result1.to_string() << std::endl;

        std::cout << "\n=== TEST 2: Hot reload with identical class ===" << std::endl;
        auto result2 = engine->execute(R"(
            class Calculator {
                auto result = 0.0;
                auto memory = 0.0;

                void add(x) {
                    result = result + x;
                    log_action("add called");
                }

                void multiply(x) {
                    result = result * x;
                    log_action("multiply called");
                }

                void store() {
                    memory = result;
                    log_action("store called");
                }

                void recall() {
                    result = memory;
                    log_action("recall called");
                }
            }
        )");

        std::cout << "\n=== TEST 3: Use global_calc after hot reload ===" << std::endl;
        auto result3 = engine->execute(R"(
            global_calc.add(10);
            check_value("result is 25.0", global_calc.result == 25.0);
            check_value("memory still 15.0", global_calc.memory == 15.0);
            return global_calc.result;
        )");

        std::cout << "After hot reload, result: " << result3.to_string() << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
