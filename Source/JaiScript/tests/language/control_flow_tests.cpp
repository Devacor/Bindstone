#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class control_flow_tests : public suite {
public:
    control_flow_tests() : suite("Control Flow") {}
    
    void forge_tests() override {
        // If statement tests
        test("if_statement_true", [this]() {
            auto engine = engine::make();
            script_value result = engine->execute(R"(
                var x = 5;
                if (x > 0) {
                    x = 10;
                }
                x;
            )");
            check_eq(result.as<int>(), 10);
        });
        
        test("if_statement_false", [this]() {
            auto engine = engine::make();
            script_value result = engine->execute(R"(
                var x = 5;
                if (x < 0) {
                    x = 10;
                }
                x;
            )");
            check_eq(result.as<int>(), 5);
        });
        
        test("if_else_branches", [this]() {
            auto engine = engine::make();
            
            // Test true branch
            script_value result = engine->execute(R"(
                var x = 5;
                if (x > 0) {
                    x = 10;
                } else {
                    x = -10;
                }
                x;
            )");
            check_eq(result.as<int>(), 10);
            
            // Test false branch
            result = engine->execute(R"(
                var x = -5;
                if (x > 0) {
                    x = 10;
                } else {
                    x = -10;
                }
                x;
            )");
            check_eq(result.as<int>(), -10);
        });
        
        test("nested_if_statements", [this]() {
            auto engine = engine::make();
            script_value result = engine->execute(R"(
                var x = 5;
                var y = 10;
                if (x > 0) {
                    if (y > 5) {
                        x = x + y;
                    }
                }
                x;
            )");
            check_eq(result.as<int>(), 15);
        });
        
        // While loop tests
        test("while_loop_counter", [this]() {
            auto engine = engine::make();
            script_value result = engine->execute(R"(
                var counter = 0;
                while (counter < 5) {
                    counter = counter + 1;
                }
                counter;
            )");
            check_eq(result.as<int>(), 5);
        });
        
        test("while_loop_sum", [this]() {
            auto engine = engine::make();
            script_value result = engine->execute(R"(
                var sum = 0;
                var i = 1;
                while (i <= 4) {
                    sum = sum + i;
                    i = i + 1;
                }
                sum;
            )");
            check_eq(result.as<int>(), 10); // 1 + 2 + 3 + 4
        });
        
        test("while_loop_with_break", [this]() {
            auto engine = engine::make();
            script_value result = engine->execute(R"(
                var i = 0;
                while (true) {
                    i = i + 1;
                    if (i >= 5) {
                        break;
                    }
                }
                i;
            )");
            check_eq(result.as<int>(), 5);
        });
        
        test("while_loop_with_continue", [this]() {
            auto engine = engine::make();
            script_value result = engine->execute(R"(
                var sum = 0;
                var i = 0;
                while (i < 10) {
                    i = i + 1;
                    if (i % 2 == 0) {
                        continue;
                    }
                    sum = sum + i;
                }
                sum;
            )");
            check_eq(result.as<int>(), 25); // 1 + 3 + 5 + 7 + 9
        });
        
        // For loop tests
        test("for_loop_basic", [this]() {
            auto engine = engine::make();
            script_value result = engine->execute(R"(
                var sum = 0;
                for (var i = 1; i <= 5; i = i + 1) {
                    sum = sum + i;
                }
                sum;
            )");
            check_eq(result.as<int>(), 15); // 1 + 2 + 3 + 4 + 5
        });
        
        test("for_loop_with_increment_operator", [this]() {
            auto engine = engine::make();
            script_value result = engine->execute(R"(
                var sum = 0;
                for (int i = 0; i < 5; ++i) {
                    sum += i;
                }
                sum;
            )");
            check_eq(result.as<int>(), 10); // 0 + 1 + 2 + 3 + 4
        });
        
        test("for_loop_with_break", [this]() {
            auto engine = engine::make();
            script_value result = engine->execute(R"(
                var sum = 0;
                for (int i = 1; i <= 10; ++i) {
                    if (i > 5) {
                        break;
                    }
                    sum += i;
                }
                sum;
            )");
            check_eq(result.as<int>(), 15); // 1 + 2 + 3 + 4 + 5
        });
        
        test("for_loop_with_continue", [this]() {
            auto engine = engine::make();
            script_value result = engine->execute(R"(
                var sum = 0;
                for (int i = 1; i <= 10; ++i) {
                    if (i % 2 == 0) {
                        continue;
                    }
                    sum += i;
                }
                sum;
            )");
            check_eq(result.as<int>(), 25); // 1 + 3 + 5 + 7 + 9
        });
        
        test("nested_loops", [this]() {
            auto engine = engine::make();
            script_value result = engine->execute(R"(
                var sum = 0;
                for (int i = 1; i <= 3; ++i) {
                    for (int j = 1; j <= 3; ++j) {
                        sum += i * j;
                    }
                }
                sum;
            )");
            check_eq(result.as<int>(), 36); // Sum of multiplication table
        });
        
        test("complex_control_flow", [this]() {
            auto engine = engine::make();
            script_value result = engine->execute(R"(
                var result = 0;
                for (int i = 1; i <= 10; ++i) {
                    if (i % 2 == 0) {
                        continue;
                    }
                    var j = 0;
                    while (j < i) {
                        result += 1;
                        j++;
                        if (j > 3) {
                            break;
                        }
                    }
                }
                result;
            )");
            check_eq(result.as<int>(), 16);
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
using control_flow_tests = jai::foundry::tests::control_flow_tests;
FOUNDRY_REGISTER(control_flow_tests)