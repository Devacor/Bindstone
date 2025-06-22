#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(ControlFlowTests)

JAI_TEST(simple_if_true) {
    Engine engine;
    Value result = engine.execute(R"(
        x = 5;
        if (x > 0) {
            x = 10;
        }
        return x;
    )");
    expect_eq(result.as<int>(), 10);
}

JAI_TEST(simple_if_false) {
    Engine engine;
    Value result = engine.execute(R"(
        x = 5;
        if (x < 0) {
            x = 10;
        }
        return x;
    )");
    expect_eq(result.as<int>(), 5);
}

JAI_TEST(if_else_true_branch) {
    Engine engine;
    Value result = engine.execute(R"(
        x = 5;
        if (x > 0) {
            x = 10;
        } else {
            x = -10;
        }
        return x;
    )");
    expect_eq(result.as<int>(), 10);
}

JAI_TEST(if_else_false_branch) {
    Engine engine;
    Value result = engine.execute(R"(
        x = -5;
        if (x > 0) {
            x = 10;
        } else {
            x = -10;
        }
        return x;
    )");
    expect_eq(result.as<int>(), -10);
}

JAI_TEST(simple_while_loop) {
    Engine engine;
    Value result = engine.execute(R"(
        counter = 0;
        while (counter < 5) {
            counter = counter + 1;
        }
        return counter;
    )");
    expect_eq(result.as<int>(), 5);
}

JAI_TEST(while_loop_with_condition) {
    Engine engine;
    Value result = engine.execute(R"(
        sum = 0;
        i = 1;
        while (i <= 4) {
            sum = sum + i;
            i = i + 1;
        }
        return sum;
    )");
    expect_eq(result.as<int>(), 10); // 1 + 2 + 3 + 4 = 10
}

JAI_TEST(basic_for_loop) {
    Engine engine;
    Value result = engine.execute(R"(
        sum = 0;
        for (auto i = 1; i <= 5; i = i + 1) {
            sum = sum + i;
        }
        return sum;
    )");
    expect_eq(result.as<int>(), 15); // 1 + 2 + 3 + 4 + 5 = 15
}

JAI_TEST(for_loop_with_initialization) {
    Engine engine;
    Value result = engine.execute(R"(
        total = 0;
        for (auto count = 0; count < 3; count = count + 1) {
            total = total + count * 2;
        }
        return total;
    )");
    expect_eq(result.as<int>(), 6); // 0*2 + 1*2 + 2*2 = 6
}

JAI_TEST(nested_if_statements) {
    Engine engine;
    Value result = engine.execute(R"(
        x = 5;
        y = 10;
        result = 0;
        if (x > 0) {
            if (y > 5) {
                result = x + y;
            } else {
                result = x - y;
            }
        }
        return result;
    )");
    expect_eq(result.as<int>(), 15);
}

JAI_TEST(while_loop_with_if) {
    Engine engine;
    Value result = engine.execute(R"(
        sum = 0;
        i = 1;
        while (i <= 10) {
            if (i % 2 == 0) {
                sum = sum + i;
            }
            i = i + 1;
        }
        return sum;
    )");
    expect_eq(result.as<int>(), 30); // 2 + 4 + 6 + 8 + 10 = 30
}

JAI_TEST(string_truthiness) {
    Engine engine;
    Value result = engine.execute(R"(
        message = "hello";
        result = 0;
        if (message) {
            result = 1;
        }
        return result;
    )");
    expect_eq(result.as<int>(), 1);
}

JAI_TEST(empty_string_falsiness) {
    Engine engine;
    Value result = engine.execute(R"(
        message = "";
        result = 0;
        if (message) {
            result = 1;
        } else {
            result = 2;
        }
        return result;
    )");
    expect_eq(result.as<int>(), 2);
}

JAI_TEST(zero_falsiness) {
    Engine engine;
    Value result = engine.execute(R"(
        value = 0;
        result = 0;
        if (value) {
            result = 1;
        } else {
            result = 2;
        }
        return result;
    )");
    expect_eq(result.as<int>(), 2);
}

JAI_TEST(break_statement) {
    Engine engine;
    Value result = engine.execute(R"(
        sum = 0;
        i = 1;
        while (true) {
            if (i > 5) {
                break;
            }
            sum = sum + i;
            i = i + 1;
        }
        return sum;
    )");
    expect_eq(result.as<int>(), 15); // 1 + 2 + 3 + 4 + 5 = 15
}

JAI_TEST(continue_statement) {
    Engine engine;
    Value result = engine.execute(R"(
        sum = 0;
        for (auto i = 1; i <= 5; i = i + 1) {
            if (i == 3) {
                continue;
            }
            sum = sum + i;
        }
        return sum;
    )");
    expect_eq(result.as<int>(), 12); // 1 + 2 + 4 + 5 = 12 (skips 3)
}

JAI_TEST(complex_control_flow) {
    Engine engine;
    Value result = engine.execute(R"(
        total = 0;
        for (auto i = 1; i <= 10; i = i + 1) {
            if (i % 2 == 0) {
                if (i > 6) {
                    break;
                }
                total = total + i;
            } else {
                if (i == 5) {
                    continue;
                }
                total = total + (i * 2);
            }
        }
        return total;
    )");
    // i=1: odd, not 5, total += 1*2 = 2
    // i=2: even, <=6, total += 2 = 4  
    // i=3: odd, not 5, total += 3*2 = 10
    // i=4: even, <=6, total += 4 = 14
    // i=5: odd, is 5, continue
    // i=6: even, <=6, total += 6 = 20
    // i=7: odd, not 5, total += 7*2 = 34
    // i=8: even, >6, break
    expect_eq(result.as<int>(), 34);
}

JAI_TEST(range_based_for_loop) {
    Engine engine;
    Value result = engine.execute(R"(
        sum = 0;
        for (auto x : {1, 2, 3, 4, 5}) {
            sum = sum + x;
        }
        return sum;
    )");
    expect_eq(result.as<int>(), 15); // 1 + 2 + 3 + 4 + 5 = 15
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()