#pragma once

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/value.hpp>
#include <cmath>
#include <limits>
#include <random>

namespace jai {
namespace stdlib {

    // Register math functions with an engine
    inline void register_math_functions(engine& engine) {
        auto engine_weak = engine.weak_from_this();

        // ============================================================
        // Basic operations
        // ============================================================

        // abs - absolute value (works for both int and float)
        engine.add_function("abs", [](script_float x) -> script_float {
            return std::abs(x);
        });

        // sign - returns -1, 0, or 1
        engine.add_function("sign", [](script_float x) -> script_int {
            if (x > 0) return 1;
            if (x < 0) return -1;
            return 0;
        });

        // min/max - works with floats
        engine.add_function("min", [](script_float a, script_float b) -> script_float {
            return std::fmin(a, b);
        });

        engine.add_function("max", [](script_float a, script_float b) -> script_float {
            return std::fmax(a, b);
        });

        // clamp - clamps value between min and max
        engine.add_function("clamp", [](script_float x, script_float lo, script_float hi) -> script_float {
            return std::fmax(lo, std::fmin(x, hi));
        });

        // ============================================================
        // Rounding
        // ============================================================

        // floor - round down
        engine.add_function("floor", [](script_float x) -> script_float {
            return std::floor(x);
        });

        // ceil - round up
        engine.add_function("ceil", [](script_float x) -> script_float {
            return std::ceil(x);
        });

        // round - round to nearest
        engine.add_function("round", [](script_float x) -> script_float {
            return std::round(x);
        });

        // trunc - truncate toward zero
        engine.add_function("trunc", [](script_float x) -> script_float {
            return std::trunc(x);
        });

        // ============================================================
        // Power and roots
        // ============================================================

        // sqrt - square root
        engine.add_function("sqrt", [](script_float x) -> script_float {
            return std::sqrt(x);
        });

        // cbrt - cube root
        engine.add_function("cbrt", [](script_float x) -> script_float {
            return std::cbrt(x);
        });

        // pow - power
        engine.add_function("pow", [](script_float base, script_float exp) -> script_float {
            return std::pow(base, exp);
        });

        // exp - e^x
        engine.add_function("exp", [](script_float x) -> script_float {
            return std::exp(x);
        });

        // exp2 - 2^x
        engine.add_function("exp2", [](script_float x) -> script_float {
            return std::exp2(x);
        });

        // log - natural logarithm
        engine.add_function("log", [](script_float x) -> script_float {
            return std::log(x);
        });

        // log2 - base-2 logarithm
        engine.add_function("log2", [](script_float x) -> script_float {
            return std::log2(x);
        });

        // log10 - base-10 logarithm
        engine.add_function("log10", [](script_float x) -> script_float {
            return std::log10(x);
        });

        // ============================================================
        // Trigonometry (radians)
        // ============================================================

        // sin - sine
        engine.add_function("sin", [](script_float x) -> script_float {
            return std::sin(x);
        });

        // cos - cosine
        engine.add_function("cos", [](script_float x) -> script_float {
            return std::cos(x);
        });

        // tan - tangent
        engine.add_function("tan", [](script_float x) -> script_float {
            return std::tan(x);
        });

        // asin - arc sine
        engine.add_function("asin", [](script_float x) -> script_float {
            return std::asin(x);
        });

        // acos - arc cosine
        engine.add_function("acos", [](script_float x) -> script_float {
            return std::acos(x);
        });

        // atan - arc tangent
        engine.add_function("atan", [](script_float x) -> script_float {
            return std::atan(x);
        });

        // atan2 - arc tangent of y/x
        engine.add_function("atan2", [](script_float y, script_float x) -> script_float {
            return std::atan2(y, x);
        });

        // ============================================================
        // Hyperbolic
        // ============================================================

        // sinh - hyperbolic sine
        engine.add_function("sinh", [](script_float x) -> script_float {
            return std::sinh(x);
        });

        // cosh - hyperbolic cosine
        engine.add_function("cosh", [](script_float x) -> script_float {
            return std::cosh(x);
        });

        // tanh - hyperbolic tangent
        engine.add_function("tanh", [](script_float x) -> script_float {
            return std::tanh(x);
        });

        // ============================================================
        // Angle conversion
        // ============================================================

        // degrees - radians to degrees
        engine.add_function("degrees", [](script_float radians) -> script_float {
            return radians * (180.0 / 3.14159265358979323846);
        });

        // radians - degrees to radians
        engine.add_function("radians", [](script_float degrees) -> script_float {
            return degrees * (3.14159265358979323846 / 180.0);
        });

        // ============================================================
        // Utility
        // ============================================================

        // fmod - floating-point remainder
        engine.add_function("fmod", [](script_float x, script_float y) -> script_float {
            return std::fmod(x, y);
        });

        // hypot - sqrt(x^2 + y^2) without overflow
        engine.add_function("hypot", [](script_float x, script_float y) -> script_float {
            return std::hypot(x, y);
        });

        // lerp - linear interpolation (alias: mix)
        engine.add_function("lerp", [](script_float a, script_float b, script_float t) -> script_float {
            return a + t * (b - a);
        });

        // ============================================================
        // Interpolation (Bindstone-style mix/unmix)
        // ============================================================

        // mix - linear interpolation: mix(start, end, percent)
        // Returns: percent * (end - start) + start
        engine.add_function("mix", [](script_float start, script_float end, script_float percent) -> script_float {
            return (percent * (end - start)) + start;
        });

        // unmix - inverse lerp: given a value, returns the percent
        // Returns: (value - start) / (end - start)
        engine.add_function("unmix", [](script_float start, script_float end, script_float value) -> script_float {
            return (end == start) ? end : (value - start) / (end - start);
        });

        // mix_in - ease-in interpolation (accelerating)
        // Uses pow(percent, strength) for easing
        engine.add_function("mix_in", [](script_float start, script_float end, script_float percent, script_float strength) -> script_float {
            return std::pow(percent, strength) * (end - start) + start;
        });

        // mix_out - ease-out interpolation (decelerating)
        // Uses 1 - pow(1 - percent, strength) for easing
        engine.add_function("mix_out", [](script_float start, script_float end, script_float percent, script_float strength) -> script_float {
            return (1.0 - std::pow(1.0 - percent, strength)) * (end - start) + start;
        });

        // mix_in_out - ease-in-out interpolation (accelerate then decelerate)
        engine.add_function("mix_in_out", [](script_float start, script_float end, script_float percent, script_float strength) -> script_float {
            auto half_range = (end - start) / 2.0 + start;
            if (percent < 0.5) {
                return std::pow(percent * 2.0, strength) * (half_range - start) + start;
            }
            return (1.0 - std::pow(1.0 - (percent - 0.5) * 2.0, strength)) * (end - half_range) + half_range;
        });

        // mix_out_in - ease-out-in interpolation (decelerate then accelerate)
        engine.add_function("mix_out_in", [](script_float start, script_float end, script_float percent, script_float strength) -> script_float {
            auto half_range = (end - start) / 2.0 + start;
            if (percent < 0.5) {
                return (1.0 - std::pow(1.0 - percent * 2.0, strength)) * (half_range - start) + start;
            }
            return std::pow((percent - 0.5) * 2.0, strength) * (end - half_range) + half_range;
        });

        // unmix_in - inverse of mix_in
        engine.add_function("unmix_in", [](script_float start, script_float end, script_float value, script_float strength) -> script_float {
            return (end == start) ? end : std::pow((value - start) / (end - start), 1.0 / strength);
        });

        // unmix_out - inverse of mix_out
        engine.add_function("unmix_out", [](script_float start, script_float end, script_float value, script_float strength) -> script_float {
            return 1.0 - std::pow(1.0 - ((value - start) / (end - start)), 1.0 / strength);
        });

        // unmix_in_out - inverse of mix_in_out
        engine.add_function("unmix_in_out", [](script_float start, script_float end, script_float value, script_float strength) -> script_float {
            auto half_range = (end - start) / 2.0 + start;
            if (value < half_range) {
                return std::pow((value - start) / (half_range - start), 1.0 / strength) / 2.0;
            }
            return (1.0 - std::pow(1.0 - ((value - half_range) / (end - half_range)), 1.0 / strength)) / 2.0 + 0.5;
        });

        // unmix_out_in - inverse of mix_out_in
        engine.add_function("unmix_out_in", [](script_float start, script_float end, script_float value, script_float strength) -> script_float {
            auto half_range = (end - start) / 2.0 + start;
            if (value < half_range) {
                return (1.0 - std::pow(1.0 - ((value - start) / (half_range - start)), 1.0 / strength)) / 2.0;
            }
            return std::pow((value - half_range) / (end - half_range), 1.0 / strength) / 2.0 + 0.5;
        });

        // saturate - clamp to [0, 1]
        engine.add_function("saturate", [](script_float x) -> script_float {
            return std::fmax(0.0, std::fmin(x, 1.0));
        });

        // wrap - wraps value into range [lower, upper)
        engine.add_function("wrap", [](script_float lower, script_float upper, script_float val) -> script_float {
            if (lower > upper) std::swap(lower, upper);
            val -= lower;
            script_float range = upper - lower;
            if (range == 0.0) return upper;
            return val - (range * std::floor(val / range)) + lower;
        });

        // remap - remaps value from one range to another
        // remap(value, in_min, in_max, out_min, out_max)
        engine.add_function("remap", [](script_float value, script_float in_min, script_float in_max,
                                        script_float out_min, script_float out_max) -> script_float {
            script_float t = (value - in_min) / (in_max - in_min);
            return out_min + t * (out_max - out_min);
        });

        // is_nan - check if NaN
        engine.add_function("is_nan", [](script_float x) -> bool {
            return std::isnan(x);
        });

        // is_inf - check if infinite
        engine.add_function("is_inf", [](script_float x) -> bool {
            return std::isinf(x);
        });

        // ============================================================
        // Constants
        // ============================================================

        engine.add_global("PI", script_value(3.14159265358979323846, engine_weak));
        engine.add_global("E", script_value(2.71828182845904523536, engine_weak));
        engine.add_global("TAU", script_value(6.28318530717958647692, engine_weak));  // 2*PI
        engine.add_global("INF", script_value(std::numeric_limits<script_float>::infinity(), engine_weak));
        engine.add_global("NAN_VALUE", script_value(std::numeric_limits<script_float>::quiet_NaN(), engine_weak));

        // ============================================================
        // Random numbers (simple interface)
        // ============================================================

        // Create a shared random engine for this script engine
        auto rng = std::make_shared<std::mt19937>(std::random_device{}());

        // random() - returns random float in [0, 1)
        engine.add_function("random", [rng]() -> script_float {
            std::uniform_real_distribution<script_float> dist(0.0, 1.0);
            return dist(*rng);
        });

        // random_int(min, max) - returns random int in [min, max]
        engine.add_function("random_int", [rng](script_int min_val, script_int max_val) -> script_int {
            std::uniform_int_distribution<script_int> dist(min_val, max_val);
            return dist(*rng);
        });

        // random_range(min, max) - returns random float in [min, max)
        engine.add_function("random_range", [rng](script_float min_val, script_float max_val) -> script_float {
            std::uniform_real_distribution<script_float> dist(min_val, max_val);
            return dist(*rng);
        });
    }

} // namespace stdlib
} // namespace jai
