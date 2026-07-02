#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <cmath>

using namespace jai::foundry;

namespace jai::foundry::tests {

class math_stdlib_tests : public suite {
public:
    math_stdlib_tests() : suite("Math Stdlib Tests") {}

    void forge_tests() override {
        // ============================================================
        // Basic operations
        // ============================================================

        test("abs_positive", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("abs(5.5);");
            check_eq(result.as<double>(), 5.5);
        });

        test("abs_negative", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("abs(-5.5);");
            check_eq(result.as<double>(), 5.5);
        });

        test("sign_positive", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("sign(42.0);");
            check_eq(result.as<script_int>(), 1);
        });

        test("sign_negative", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("sign(-42.0);");
            check_eq(result.as<script_int>(), -1);
        });

        test("sign_zero", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("sign(0.0);");
            check_eq(result.as<script_int>(), 0);
        });

        test("min_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("min(3.0, 7.0);");
            check_eq(result.as<double>(), 3.0);
        });

        test("max_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("max(3.0, 7.0);");
            check_eq(result.as<double>(), 7.0);
        });

        test("clamp_in_range", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("clamp(5.0, 0.0, 10.0);");
            check_eq(result.as<double>(), 5.0);
        });

        test("clamp_below", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("clamp(-5.0, 0.0, 10.0);");
            check_eq(result.as<double>(), 0.0);
        });

        test("clamp_above", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("clamp(15.0, 0.0, 10.0);");
            check_eq(result.as<double>(), 10.0);
        });

        // ============================================================
        // Rounding
        // ============================================================

        test("floor_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("floor(3.7);");
            check_eq(result.as<double>(), 3.0);
        });

        test("ceil_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("ceil(3.2);");
            check_eq(result.as<double>(), 4.0);
        });

        test("round_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("round(3.5);");
            check_eq(result.as<double>(), 4.0);
        });

        test("trunc_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("trunc(-3.7);");
            check_eq(result.as<double>(), -3.0);
        });

        // ============================================================
        // Power and roots
        // ============================================================

        test("sqrt_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("sqrt(16.0);");
            check_eq(result.as<double>(), 4.0);
        });

        test("pow_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("pow(2.0, 3.0);");
            check_eq(result.as<double>(), 8.0);
        });

        test("exp_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("exp(1.0);");
            check(std::abs(result.as<double>() - 2.71828182845904523536) < 0.0001);
        });

        test("log_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("log(E);");
            check(std::abs(result.as<double>() - 1.0) < 0.0001);
        });

        // ============================================================
        // Trigonometry
        // ============================================================

        test("sin_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("sin(0.0);");
            check_eq(result.as<double>(), 0.0);
        });

        test("cos_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("cos(0.0);");
            check_eq(result.as<double>(), 1.0);
        });

        test("sin_pi_half", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("sin(PI / 2.0);");
            check(std::abs(result.as<double>() - 1.0) < 0.0001);
        });

        // ============================================================
        // Mix/Unmix interpolation (Bindstone-style)
        // ============================================================

        test("mix_basic", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            // mix(0, 100, 0.5) should return 50
            script_value result = engine->execute("mix(0.0, 100.0, 0.5);");
            check_eq(result.as<double>(), 50.0);
        });

        test("mix_at_start", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("mix(10.0, 20.0, 0.0);");
            check_eq(result.as<double>(), 10.0);
        });

        test("mix_at_end", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("mix(10.0, 20.0, 1.0);");
            check_eq(result.as<double>(), 20.0);
        });

        test("unmix_basic", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            // unmix(0, 100, 50) should return 0.5
            script_value result = engine->execute("unmix(0.0, 100.0, 50.0);");
            check_eq(result.as<double>(), 0.5);
        });

        test("unmix_at_start", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("unmix(10.0, 20.0, 10.0);");
            check_eq(result.as<double>(), 0.0);
        });

        test("unmix_at_end", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("unmix(10.0, 20.0, 20.0);");
            check_eq(result.as<double>(), 1.0);
        });

        test("mix_in_ease", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            // mix_in with strength 2 at t=0.5: pow(0.5, 2) = 0.25, result = 25
            script_value result = engine->execute("mix_in(0.0, 100.0, 0.5, 2.0);");
            check_eq(result.as<double>(), 25.0);
        });

        test("mix_out_ease", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            // mix_out with strength 2 at t=0.5: 1 - pow(0.5, 2) = 0.75, result = 75
            script_value result = engine->execute("mix_out(0.0, 100.0, 0.5, 2.0);");
            check_eq(result.as<double>(), 75.0);
        });

        // ============================================================
        // Utility functions
        // ============================================================

        test("saturate_clamps_to_unit", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            check_eq(engine->execute("saturate(0.5);").as<double>(), 0.5);
            check_eq(engine->execute("saturate(-0.5);").as<double>(), 0.0);
            check_eq(engine->execute("saturate(1.5);").as<double>(), 1.0);
        });

        test("wrap_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            // wrap(0, 360, 450) should return 90
            script_value result = engine->execute("wrap(0.0, 360.0, 450.0);");
            check_eq(result.as<double>(), 90.0);
        });

        test("remap_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            // remap(5, 0, 10, 0, 100) should return 50
            script_value result = engine->execute("remap(5.0, 0.0, 10.0, 0.0, 100.0);");
            check_eq(result.as<double>(), 50.0);
        });

        test("hypot_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            // hypot(3, 4) = 5 (3-4-5 triangle)
            script_value result = engine->execute("hypot(3.0, 4.0);");
            check_eq(result.as<double>(), 5.0);
        });

        test("lerp_function", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("lerp(0.0, 100.0, 0.25);");
            check_eq(result.as<double>(), 25.0);
        });

        // ============================================================
        // Constants
        // ============================================================

        test("pi_constant", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("PI;");
            check(std::abs(result.as<double>() - 3.14159265358979323846) < 0.0001);
        });

        test("e_constant", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("E;");
            check(std::abs(result.as<double>() - 2.71828182845904523536) < 0.0001);
        });

        test("tau_constant", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("TAU;");
            check(std::abs(result.as<double>() - 6.28318530717958647692) < 0.0001);
        });

        // ============================================================
        // Random (just test they don't crash - values are random)
        // ============================================================

        test("random_in_unit_range", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("random();");
            double val = result.as<double>();
            check(val >= 0.0 && val < 1.0);
        });

        test("random_int_in_range", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("random_int(1, 10);");
            script_int val = result.as<script_int>();
            check(val >= 1 && val <= 10);
        });

        test("random_range_in_bounds", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            script_value result = engine->execute("random_range(5.0, 10.0);");
            double val = result.as<double>();
            check(val >= 5.0 && val < 10.0);
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::math_stdlib_tests)
