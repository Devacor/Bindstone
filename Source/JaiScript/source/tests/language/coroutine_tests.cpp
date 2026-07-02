#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class coroutine_tests : public suite {
public:
    coroutine_tests() : suite("Coroutine Tests") {}

    void forge_tests() override {

        test("basic_yield_with_value", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int counter() {
                    yield 1;
                    yield 2;
                    yield 3;
                    return 4;
                }
                auto c = counter();
                auto a = c.resume();
                auto b = c.resume();
                auto d = c.resume();
                auto e = c.resume();
                a + b + d + e
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(10));
        });

        test("coroutine_done_check", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int gen() {
                    yield 42;
                    return 0;
                }
                auto c = gen();
                auto before = c.done();
                c.resume();
                auto mid = c.done();
                c.resume();
                auto after = c.done();
                auto result = 0;
                if (!before && !mid && after) {
                    result = 1;
                }
                result
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(1));
        });

        test("yield_inside_for_loop", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int range_gen() {
                    for (int i = 0; i < 5; ++i) {
                        yield i;
                    }
                    return -1;
                }
                auto c = range_gen();
                auto sum = 0;
                while (!c.done()) {
                    sum = sum + c.resume();
                }
                sum
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(9));
        });

        test("yield_inside_while_loop", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int while_gen() {
                    auto i = 0;
                    while (i < 3) {
                        yield i;
                        i = i + 1;
                    }
                    return 99;
                }
                auto c = while_gen();
                auto r0 = c.resume();
                auto r1 = c.resume();
                auto r2 = c.resume();
                auto r3 = c.resume();
                r0 * 1000 + r1 * 100 + r2 * 10 + r3
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(219));
        });

        test("yield_inside_if_else", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int branch_gen(int x) {
                    if (x > 0) {
                        yield 1;
                    } else {
                        yield -1;
                    }
                    return 0;
                }
                auto c1 = branch_gen(5);
                auto c2 = branch_gen(-3);
                auto r1 = c1.resume();
                auto r2 = c2.resume();
                r1 + r2
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(0));
        });

        test("multiple_yields_in_sequence", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int seq() {
                    yield 10;
                    yield 20;
                    yield 30;
                    yield 40;
                    yield 50;
                    return 0;
                }
                auto c = seq();
                auto sum = 0;
                sum = sum + c.resume();
                sum = sum + c.resume();
                sum = sum + c.resume();
                sum = sum + c.resume();
                sum = sum + c.resume();
                sum = sum + c.resume();
                sum
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(150));
        });

        test("multiple_concurrent_coroutines", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int gen_a() {
                    yield 1;
                    yield 2;
                    return 3;
                }
                coroutine int gen_b() {
                    yield 10;
                    yield 20;
                    return 30;
                }
                auto a = gen_a();
                auto b = gen_b();
                auto r1 = a.resume();
                auto r2 = b.resume();
                auto r3 = a.resume();
                auto r4 = b.resume();
                auto r5 = a.resume();
                auto r6 = b.resume();
                r1 + r2 + r3 + r4 + r5 + r6
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(66));
        });

        test("coroutine_with_parameters", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int multiply_gen(int start, int factor) {
                    int val = start;
                    yield val;
                    val = val * factor;
                    yield val;
                    val = val * factor;
                    return val;
                }
                auto c = multiply_gen(2, 3);
                auto r1 = c.resume();
                auto r2 = c.resume();
                auto r3 = c.resume();
                r1 * 100 + r2 * 10 + r3
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(278));
        });

        test("yield_string_values", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine string greet() {
                    yield "hello";
                    yield "world";
                    return "!";
                }
                auto c = greet();
                auto s = "";
                s = s + c.resume();
                s = s + " ";
                s = s + c.resume();
                s = s + c.resume();
                s
            )");
            check_eq(result.template as<std::string>(), std::string("hello world!"));
        });

        test("nested_loops_with_yield", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int matrix_gen() {
                    for (int i = 0; i < 2; ++i) {
                        for (int j = 0; j < 3; ++j) {
                            yield i * 10 + j;
                        }
                    }
                    return -1;
                }
                auto c = matrix_gen();
                auto sum = 0;
                while (!c.done()) {
                    sum = sum + c.resume();
                }
                sum
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(35));
        });

        test("coroutine_void_yield", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine void stepper() {
                    yield;
                    yield;
                    return;
                }
                auto c = stepper();
                c.resume();
                c.resume();
                c.resume();
                auto result = 0;
                if (c.done()) {
                    result = 1;
                }
                result
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(1));
        });

        test("yield_no_duplicate_side_effects", [this]() {
            auto eng = jai::foundry::make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(R"(
                auto count = 0;
                coroutine void counter() {
                    for (auto i = 0; i < 5; i++) {
                        count = count + 1;
                        yield;
                    }
                }
                auto c = counter();
                c.resume();
                c.resume();
                c.resume();
            )");
            auto count = eng->get_variable("count").as<int64_t>();
            check_eq(count, int64_t(3));
        });

        test("range_for_over_generator", [this]() {
            auto eng = jai::foundry::make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(R"(
                coroutine int fibonacci() {
                    int a = 0;
                    int b = 1;
                    while (true) {
                        yield a;
                        int temp = a + b;
                        a = b;
                        b = temp;
                    }
                }
                auto sum = 0;
                auto count = 0;
                for (auto x : fibonacci()) {
                    sum = sum + x;
                    count = count + 1;
                    if (count == 7) { break; }
                }
            )");
            check_eq(eng->get_variable("sum").as<int64_t>(), int64_t(20));
            check_eq(eng->get_variable("count").as<int64_t>(), int64_t(7));
        });

        test("range_for_over_finite_generator", [this]() {
            auto eng = jai::foundry::make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(R"(
                coroutine int range(int n) {
                    for (int i = 0; i < n; i++) {
                        yield i;
                    }
                }
                auto total = 0;
                for (auto x : range(5)) {
                    total = total + x;
                }
            )");
            check_eq(eng->get_variable("total").as<int64_t>(), int64_t(10));
        });

        test("for_loop_yield_step_by_step", [this]() {
            auto eng = jai::foundry::make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(R"(
                coroutine int gen() {
                    for (int i = 0; i < 3; ++i) {
                        yield i;
                    }
                    return 99;
                }
                auto c = gen();
                auto r0 = c.resume();
                auto r1 = c.resume();
                auto r2 = c.resume();
                auto r3 = c.resume();
            )");
            check_eq(eng->get_variable("r0").as<int64_t>(), int64_t(0));
            check_eq(eng->get_variable("r1").as<int64_t>(), int64_t(1));
            check_eq(eng->get_variable("r2").as<int64_t>(), int64_t(2));
            check_eq(eng->get_variable("r3").as<int64_t>(), int64_t(99));
        });

    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::coroutine_tests)
