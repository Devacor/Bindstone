#pragma once

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/value.hpp>
#include <cmath>
#include <limits>
#include <random>

namespace jai {
namespace stdlib {

    inline void register_math_functions(engine& eng_ref) {
        engine* eng = &eng_ref;

        // ============================================================
        // Basic operations
        // ============================================================

        eng_ref.add_function("abs", [](script_float x) -> script_float {
            return std::abs(x);
        });

        eng_ref.add_function("sign", [](script_float x) -> script_int {
            if (x > 0) return 1;
            if (x < 0) return -1;
            return 0;
        });

        eng_ref.add_function("min", [](script_float a, script_float b) -> script_float {
            return std::fmin(a, b);
        });

        eng_ref.add_function("max", [](script_float a, script_float b) -> script_float {
            return std::fmax(a, b);
        });

        eng_ref.add_function("clamp", [](script_float x, script_float lo, script_float hi) -> script_float {
            return std::fmax(lo, std::fmin(x, hi));
        });

        // ============================================================
        // Rounding
        // ============================================================

        eng_ref.add_function("floor", [](script_float x) -> script_float {
            return std::floor(x);
        });

        eng_ref.add_function("ceil", [](script_float x) -> script_float {
            return std::ceil(x);
        });

        eng_ref.add_function("round", [](script_float x) -> script_float {
            return std::round(x);
        });

        eng_ref.add_function("trunc", [](script_float x) -> script_float {
            return std::trunc(x);
        });

        // ============================================================
        // Power and roots
        // ============================================================

        eng_ref.add_function("sqrt", [](script_float x) -> script_float {
            return std::sqrt(x);
        });

        eng_ref.add_function("cbrt", [](script_float x) -> script_float {
            return std::cbrt(x);
        });

        eng_ref.add_function("pow", [](script_float base, script_float exp) -> script_float {
            return std::pow(base, exp);
        });

        eng_ref.add_function("exp", [](script_float x) -> script_float {
            return std::exp(x);
        });

        eng_ref.add_function("exp2", [](script_float x) -> script_float {
            return std::exp2(x);
        });

        eng_ref.add_function("log", [](script_float x) -> script_float {
            return std::log(x);
        });

        eng_ref.add_function("log2", [](script_float x) -> script_float {
            return std::log2(x);
        });

        eng_ref.add_function("log10", [](script_float x) -> script_float {
            return std::log10(x);
        });

        // ============================================================
        // Trigonometry (radians)
        // ============================================================

        eng_ref.add_function("sin", [](script_float x) -> script_float {
            return std::sin(x);
        });

        eng_ref.add_function("cos", [](script_float x) -> script_float {
            return std::cos(x);
        });

        eng_ref.add_function("tan", [](script_float x) -> script_float {
            return std::tan(x);
        });

        eng_ref.add_function("asin", [](script_float x) -> script_float {
            return std::asin(x);
        });

        eng_ref.add_function("acos", [](script_float x) -> script_float {
            return std::acos(x);
        });

        eng_ref.add_function("atan", [](script_float x) -> script_float {
            return std::atan(x);
        });

        eng_ref.add_function("atan2", [](script_float y, script_float x) -> script_float {
            return std::atan2(y, x);
        });

        // ============================================================
        // Hyperbolic
        // ============================================================

        eng_ref.add_function("sinh", [](script_float x) -> script_float {
            return std::sinh(x);
        });

        eng_ref.add_function("cosh", [](script_float x) -> script_float {
            return std::cosh(x);
        });

        eng_ref.add_function("tanh", [](script_float x) -> script_float {
            return std::tanh(x);
        });

        // ============================================================
        // Angle conversion
        // ============================================================

        eng_ref.add_function("degrees", [](script_float radians) -> script_float {
            return radians * (180.0 / 3.14159265358979323846);
        });

        eng_ref.add_function("radians", [](script_float degrees) -> script_float {
            return degrees * (3.14159265358979323846 / 180.0);
        });

        // ============================================================
        // Utility
        // ============================================================

        eng_ref.add_function("fmod", [](script_float x, script_float y) -> script_float {
            return std::fmod(x, y);
        });

        // hypot - sqrt(x^2 + y^2) without overflow
        eng_ref.add_function("hypot", [](script_float x, script_float y) -> script_float {
            return std::hypot(x, y);
        });

        // lerp - linear interpolation (alias: mix)
        eng_ref.add_function("lerp", [](script_float a, script_float b, script_float t) -> script_float {
            return a + t * (b - a);
        });

        // ============================================================
        // Interpolation (Bindstone-style mix/unmix)
        // ============================================================

        eng_ref.add_function("mix", [](script_float start, script_float end, script_float percent) -> script_float {
            return (percent * (end - start)) + start;
        });

        eng_ref.add_function("unmix", [](script_float start, script_float end, script_float value) -> script_float {
            return (end == start) ? 0.0 : (value - start) / (end - start);
        });

        eng_ref.add_function("mix_in", [](script_float start, script_float end, script_float percent, script_float strength) -> script_float {
            return std::pow(percent, strength) * (end - start) + start;
        });

        eng_ref.add_function("mix_out", [](script_float start, script_float end, script_float percent, script_float strength) -> script_float {
            return (1.0 - std::pow(1.0 - percent, strength)) * (end - start) + start;
        });

        eng_ref.add_function("mix_in_out", [](script_float start, script_float end, script_float percent, script_float strength) -> script_float {
            auto half_range = (end - start) / 2.0 + start;
            if (percent < 0.5) {
                return std::pow(percent * 2.0, strength) * (half_range - start) + start;
            }
            return (1.0 - std::pow(1.0 - (percent - 0.5) * 2.0, strength)) * (end - half_range) + half_range;
        });

        eng_ref.add_function("mix_out_in", [](script_float start, script_float end, script_float percent, script_float strength) -> script_float {
            auto half_range = (end - start) / 2.0 + start;
            if (percent < 0.5) {
                return (1.0 - std::pow(1.0 - percent * 2.0, strength)) * (half_range - start) + start;
            }
            return std::pow((percent - 0.5) * 2.0, strength) * (end - half_range) + half_range;
        });

        eng_ref.add_function("unmix_in", [](script_float start, script_float end, script_float value, script_float strength) -> script_float {
            return (end == start) ? end : std::pow((value - start) / (end - start), 1.0 / strength);
        });

        eng_ref.add_function("unmix_out", [](script_float start, script_float end, script_float value, script_float strength) -> script_float {
            return 1.0 - std::pow(1.0 - ((value - start) / (end - start)), 1.0 / strength);
        });

        eng_ref.add_function("unmix_in_out", [](script_float start, script_float end, script_float value, script_float strength) -> script_float {
            auto half_range = (end - start) / 2.0 + start;
            if (value < half_range) {
                return std::pow((value - start) / (half_range - start), 1.0 / strength) / 2.0;
            }
            return (1.0 - std::pow(1.0 - ((value - half_range) / (end - half_range)), 1.0 / strength)) / 2.0 + 0.5;
        });

        eng_ref.add_function("unmix_out_in", [](script_float start, script_float end, script_float value, script_float strength) -> script_float {
            auto half_range = (end - start) / 2.0 + start;
            if (value < half_range) {
                return (1.0 - std::pow(1.0 - ((value - start) / (half_range - start)), 1.0 / strength)) / 2.0;
            }
            return std::pow((value - half_range) / (end - half_range), 1.0 / strength) / 2.0 + 0.5;
        });

        // saturate - clamp to [0, 1]
        eng_ref.add_function("saturate", [](script_float x) -> script_float {
            return std::fmax(0.0, std::fmin(x, 1.0));
        });

        // wrap - wraps value into range [lower, upper)
        eng_ref.add_function("wrap", [](script_float lower, script_float upper, script_float val) -> script_float {
            if (lower > upper) std::swap(lower, upper);
            val -= lower;
            script_float range = upper - lower;
            if (range == 0.0) return upper;
            return val - (range * std::floor(val / range)) + lower;
        });

        // remap - remaps value from one range to another
        // remap(value, in_min, in_max, out_min, out_max)
        eng_ref.add_function("remap", [](script_float value, script_float in_min, script_float in_max,
                                        script_float out_min, script_float out_max) -> script_float {
            if (in_max == in_min) return out_min;
            script_float t = (value - in_min) / (in_max - in_min);
            return out_min + t * (out_max - out_min);
        });

        eng_ref.add_function("is_nan", [](script_float x) -> bool {
            return std::isnan(x);
        });

        eng_ref.add_function("is_inf", [](script_float x) -> bool {
            return std::isinf(x);
        });

        // ============================================================
        // Constants
        // ============================================================

        eng_ref.add_global("PI", script_value(3.14159265358979323846, eng));
        eng_ref.add_global("E", script_value(2.71828182845904523536, eng));
        eng_ref.add_global("TAU", script_value(6.28318530717958647692, eng));  // 2*PI
        eng_ref.add_global("INF", script_value(std::numeric_limits<script_float>::infinity(), eng));
        eng_ref.add_global("NAN_VALUE", script_value(std::numeric_limits<script_float>::quiet_NaN(), eng));

        // ============================================================
        // Random numbers (simple interface)
        // ============================================================

        // Create a shared random engine for this script engine
        auto rng = std::make_shared<std::mt19937>(std::random_device{}());

        // random() - returns random float in [0, 1)
        eng_ref.add_function("random", [rng]() -> script_float {
            std::uniform_real_distribution<script_float> dist(0.0, 1.0);
            return dist(*rng);
        });

        // random_int(min, max) - returns random int in [min, max]
        eng_ref.add_function("random_int", [rng](script_int min_val, script_int max_val) -> script_int {
            std::uniform_int_distribution<script_int> dist(min_val, max_val);
            return dist(*rng);
        });

        // random_range(min, max) - returns random float in [min, max)
        eng_ref.add_function("random_range", [rng](script_float min_val, script_float max_val) -> script_float {
            std::uniform_real_distribution<script_float> dist(min_val, max_val);
            return dist(*rng);
        });
    }

} // namespace stdlib
} // namespace jai
