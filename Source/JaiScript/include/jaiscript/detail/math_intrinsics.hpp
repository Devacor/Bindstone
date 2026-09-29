#pragma once

#ifndef __JAISCRIPT_DETAIL_MATH_INTRINSICS_HPP__
#define __JAISCRIPT_DETAIL_MATH_INTRINSICS_HPP__

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/checked_result.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/numeric_conversion.hpp>
#include <jaiscript/detail/math_values.hpp>
#include <cmath>
#include <cstdint>
#include <random>
#include <string_view>

namespace jai::detail {

    // ---- math:: language intrinsics (Dev commission 2026-07-09) ---------------------------
    // `math::` is a RESERVED namespace owned by the language: both backends recognize
    // `math::<fn>(args)` call shapes and execute through eval_math_intrinsic below -
    // no registration, no environment lookup, no native-boundary crossing. Pure-numeric
    // by ruling: nobody overrides int+int-class operations, and math:: is ours; user
    // operator overloads (vec + vec, ...) are untouched - their internals decompose to
    // these numerics. ONE enum + ONE name map + ONE kernel = the single source of truth
    // (parity by construction; the lookup ladder ruling: switch/array, no strings past
    // the first resolution, which callers cache on the AST node).
    enum class math_fn : uint8_t {
        floor_, ceil_, itrunc, ifloor,
        sqrt_, pow_, abs_, min_, max_, clamp_,
        sin_, cos_, tan_, atan2_,
        distance, distance_squared,
        radians_to_degrees, degrees_to_radians,
        random_, random_range, random_seed,
        // Bindstone-style interpolation family (formula-identical to the stdlib
        // registrations they supersede; lerp and mix are the same formula)
        lerp_, mix_, unmix_,
        mix_in_, mix_out_, mix_in_out_, mix_out_in_,
        unmix_in_, unmix_out_, unmix_in_out_, unmix_out_in_,
        // Explicit mod-2^64 integer arithmetic (policy-independent: identical in checked
        // and wrap overflow builds) - the sanctioned spelling for hash folds and RNG steps
        // that would otherwise hand-roll limb products around the checked-overflow error.
        wrap_add_, wrap_sub_, wrap_mul_,
        // The native math types' constructors (math::vec3(1, 2, 3); detail/math_values.hpp construct)
        vec2_, vec3_, ivec2_, ivec3_, vec4_,
        round_, remap_,
        count,
        none = count
    };

    inline math_fn math_intrinsic_from_name(std::string_view name) noexcept {
        switch (name.empty() ? '\0' : name.front()) {
            case 'a':
                if (name == "abs") return math_fn::abs_;
                if (name == "atan2") return math_fn::atan2_;
                return math_fn::none;
            case 'c':
                if (name == "cos") return math_fn::cos_;
                if (name == "ceil") return math_fn::ceil_;
                if (name == "clamp") return math_fn::clamp_;
                return math_fn::none;
            case 'd':
                if (name == "distance") return math_fn::distance;
                if (name == "distance_squared") return math_fn::distance_squared;
                if (name == "degrees_to_radians") return math_fn::degrees_to_radians;
                return math_fn::none;
            case 'f':
                return name == "floor" ? math_fn::floor_ : math_fn::none;
            case 'i':
                if (name == "itrunc") return math_fn::itrunc;
                if (name == "ifloor") return math_fn::ifloor;
                if (name == "ivec2") return math_fn::ivec2_;
                if (name == "ivec3") return math_fn::ivec3_;
                return math_fn::none;
            case 'v':
                if (name == "vec2") return math_fn::vec2_;
                if (name == "vec3") return math_fn::vec3_;
                if (name == "vec4") return math_fn::vec4_;
                return math_fn::none;
            case 'l':
                return name == "lerp" ? math_fn::lerp_ : math_fn::none;
            case 'm':
                if (name == "min") return math_fn::min_;
                if (name == "max") return math_fn::max_;
                if (name == "mix") return math_fn::mix_;
                if (name == "mix_in") return math_fn::mix_in_;
                if (name == "mix_out") return math_fn::mix_out_;
                if (name == "mix_in_out") return math_fn::mix_in_out_;
                if (name == "mix_out_in") return math_fn::mix_out_in_;
                return math_fn::none;
            case 'u':
                if (name == "unmix") return math_fn::unmix_;
                if (name == "unmix_in") return math_fn::unmix_in_;
                if (name == "unmix_out") return math_fn::unmix_out_;
                if (name == "unmix_in_out") return math_fn::unmix_in_out_;
                if (name == "unmix_out_in") return math_fn::unmix_out_in_;
                return math_fn::none;
            case 'p':
                return name == "pow" ? math_fn::pow_ : math_fn::none;
            case 'r':
                if (name == "random") return math_fn::random_;   // arity picks range form
                if (name == "random_seed") return math_fn::random_seed;
                if (name == "radians_to_degrees") return math_fn::radians_to_degrees;
                if (name == "round") return math_fn::round_;
                if (name == "remap") return math_fn::remap_;
                return math_fn::none;
            case 's':
                if (name == "sin") return math_fn::sin_;
                if (name == "sqrt") return math_fn::sqrt_;
                return math_fn::none;
            case 't':
                return name == "tan" ? math_fn::tan_ : math_fn::none;
            case 'w':
                if (name == "wrap_add") return math_fn::wrap_add_;
                if (name == "wrap_sub") return math_fn::wrap_sub_;
                if (name == "wrap_mul") return math_fn::wrap_mul_;
                return math_fn::none;
            default:
                return math_fn::none;
        }
    }

    // Engine-owned deterministic RNG (zero statics). Modeled on MV::Random
    // (generalUtility.h) with two deliberate divergences: (1) the raw engine is
    // std::mt19937_64 - its sequence IS exactly specified by the standard, so seeds
    // reproduce cross-platform - but the int/float MAPPINGS are ours, because
    // std::uniform_*_distribution algorithms are implementation-defined and would
    // break STATE_HASH/differential-fuzz determinism across toolchains; (2) the
    // default seed is FIXED (unseeded engines are deterministic, matching the test
    // regime) where MV defaults to random_device - games seed via math::random_seed.
    struct math_rng_state {
        std::mt19937_64 generator{0x9E3779B97F4A7C15ull};

        uint64_t next() { return generator(); }
        void seed(uint64_t s) { generator.seed(s); }
        // Uniform [0.0, 1.0] INCLUSIVE of both ends (Dev spec: ranges are inclusive)
        double next_unit_inclusive() {
            return static_cast<double>(next() >> 11) * (1.0 / 9007199254740991.0);   // / (2^53 - 1)
        }
    };

    // seed(): mt19937_64 seeding uses the standard-specified init_seq, so the mix
    // (fixed sequence from seed) reproduces cross-platform - update the RNG rationale
    // comment above if the engine ever changes.

    // Shared call-shape recognition (both backends): `math::<fn>` static access.
    // Name-based and engine-independent, so the resolution caches on the AST node
    // (255 = unresolved, 254 = not an intrinsic, else the math_fn id).
    inline math_fn math_intrinsic_for_call(const member_expr* callee) {
        if (callee->math_intrinsic_cache == 255) {
            math_fn fn = math_fn::none;
            if (callee->is_static && callee->object &&
                callee->object->get_type() == node_type::identifier_expr &&
                static_cast<const identifier_expr*>(callee->object.get())->name == "math") {
                fn = math_intrinsic_from_name(callee->member);
            }
            callee->math_intrinsic_cache = fn == math_fn::none ? 254 : static_cast<uint8_t>(fn);
        }
        return callee->math_intrinsic_cache == 254 ? math_fn::none
                                                   : static_cast<math_fn>(callee->math_intrinsic_cache);
    }

    // A stdlib math builtin the VM may evaluate here while its name binds the registered function (engine::register_math_alias):
    // the one-argument float family, whose stdlib (script_float) -> script_float bodies are these expressions verbatim, so an
    // int or float argument yields the same bits either way (abs is the stdlib's float abs: an int argument converts first, unlike
    // math::abs)
    inline bool math_alias_kernel(math_fn fn) noexcept {
        switch (fn) {
        case math_fn::floor_: case math_fn::ceil_: case math_fn::sqrt_: case math_fn::sin_: case math_fn::cos_: case math_fn::tan_:
        case math_fn::radians_to_degrees: case math_fn::degrees_to_radians: case math_fn::abs_:
            return true;
        default: return false;
        }
    }
    inline script_float eval_math_alias(math_fn fn, script_float x) noexcept {
        switch (fn) {
        case math_fn::abs_: return std::fabs(x);
        case math_fn::floor_: return std::floor(x);
        case math_fn::ceil_: return std::ceil(x);
        case math_fn::sqrt_: return std::sqrt(x);
        case math_fn::sin_: return std::sin(x);
        case math_fn::cos_: return std::cos(x);
        case math_fn::tan_: return std::tan(x);
        case math_fn::radians_to_degrees: return x * (180.0 / 3.14159265358979323846);
        default: return x * (3.14159265358979323846 / 180.0);
        }
    }

    // The eased mix/unmix formulas (verbatim from the stdlib registrations), one definition for the scalar rows and the
    // vector rows
    inline script_float eased_value(math_fn fn, script_float start, script_float end, script_float percent, script_float strength) noexcept {
        switch (fn) {
            case math_fn::mix_in_:
                return std::pow(percent, strength) * (end - start) + start;
            case math_fn::mix_out_:
                return (1.0 - std::pow(1.0 - percent, strength)) * (end - start) + start;
            case math_fn::mix_in_out_: {
                const script_float half_range = (end - start) / 2.0 + start;
                return percent < 0.5
                    ? std::pow(percent * 2.0, strength) * (half_range - start) + start
                    : (1.0 - std::pow(1.0 - (percent - 0.5) * 2.0, strength)) * (end - half_range) + half_range;
            }
            case math_fn::mix_out_in_: {
                const script_float half_range = (end - start) / 2.0 + start;
                return percent < 0.5
                    ? (1.0 - std::pow(1.0 - percent * 2.0, strength)) * (half_range - start) + start
                    : std::pow((percent - 0.5) * 2.0, strength) * (end - half_range) + half_range;
            }
            case math_fn::unmix_in_:
                return (end == start) ? end : std::pow((percent - start) / (end - start), 1.0 / strength);
            case math_fn::unmix_out_:
                return 1.0 - std::pow(1.0 - ((percent - start) / (end - start)), 1.0 / strength);
            case math_fn::unmix_in_out_: {
                const script_float half_range = (end - start) / 2.0 + start;
                return percent < half_range
                    ? std::pow((percent - start) / (half_range - start), 1.0 / strength) / 2.0
                    : (1.0 - std::pow(1.0 - ((percent - half_range) / (end - half_range)), 1.0 / strength)) / 2.0 + 0.5;
            }
            default: {   // unmix_out_in_
                const script_float half_range = (end - start) / 2.0 + start;
                return percent < half_range
                    ? (1.0 - std::pow(1.0 - ((percent - start) / (half_range - start)), 1.0 / strength)) / 2.0
                    : std::pow((percent - half_range) / (end - half_range), 1.0 / strength) / 2.0 + 0.5;
            }
        }
    }
    inline script_float remap_value(script_float v, script_float in_lo, script_float in_hi, script_float out_lo, script_float out_hi) noexcept {
        return in_hi == in_lo ? out_lo : out_lo + (v - in_lo) * (out_hi - out_lo) / (in_hi - in_lo);
    }
    inline const char* dims_mismatch_text(math_fn fn) noexcept {
        switch (fn) {
            case math_fn::min_: return "math::min expects vectors of the same dimensions";
            case math_fn::max_: return "math::max expects vectors of the same dimensions";
            case math_fn::clamp_: return "math::clamp expects vectors of the same dimensions";
            case math_fn::lerp_: return "math::lerp expects vectors of the same dimensions";
            case math_fn::mix_: return "math::mix expects vectors of the same dimensions";
            case math_fn::unmix_: return "math::unmix expects vectors of the same dimensions";
            case math_fn::remap_: return "math::remap expects vectors of the same dimensions";
            case math_fn::mix_in_: return "math::mix_in expects vectors of the same dimensions";
            case math_fn::mix_out_: return "math::mix_out expects vectors of the same dimensions";
            case math_fn::mix_in_out_: return "math::mix_in_out expects vectors of the same dimensions";
            case math_fn::mix_out_in_: return "math::mix_out_in expects vectors of the same dimensions";
            case math_fn::unmix_in_: return "math::unmix_in expects vectors of the same dimensions";
            case math_fn::unmix_out_: return "math::unmix_out expects vectors of the same dimensions";
            case math_fn::unmix_in_out_: return "math::unmix_in_out expects vectors of the same dimensions";
            default: return "math::unmix_out_in expects vectors of the same dimensions";
        }
    }

    // The vector rows of the component-wise family (floor, ceil, round, abs, min, max, clamp, lerp, mix, unmix, remap and
    // the eased mixes): reached only when a scalar row found an operand that is not a number. Every vector operand has the
    // same dimensions; a scalar operand applies to every component (a vector t gives each component its own). Each component
    // runs the scalar formula in binary64 on the widened components and lands as binary32, so a result is a vec of the
    // operands' dimensions, and an ivec operand never truncates silently. abs, min, max and clamp keep ivec exact when every
    // operand is integral (a scalar outside int32 follows the integer policy). Anything else is the scalar row's text.
    JAI_MATH_COLD inline checked_result<script_value> eval_vec_math(math_fn fn, const script_value* const* args, size_t argc,
                                                                    engine* eng, const char* type_text) {
        struct operand {
            bool vec = false;
            bool integral = false;
            size_t dims = 0;
            script_float f[math::k_max_dims] = {0, 0, 0, 0};
            script_int i[math::k_max_dims] = {0, 0, 0, 0};
        };
        auto type_error = [&] { return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), type_text); };
        constexpr size_t k_max_args = 5;
        if (argc > k_max_args) { return type_error(); }
        operand ops[k_max_args];
        size_t dims = 0;
        for (size_t a = 0; a < argc; ++a) {
            script_value decoded(std::monostate{}, eng);
            const script_value* v = args[a];
            if (v->raw_storage_index() == script_value::TYPEID_CPP_BOUND) { decoded = v->bound_decoded_temp(); v = &decoded; }
            const size_t raw = v->raw_storage_index();
            operand& o = ops[a];
            if (math::is_kind(raw)) {
                const script_value_type k = v->unchecked_vec_kind();
                o.vec = true;
                o.integral = math::integral(k);
                o.dims = math::dims(k);
                if (dims != 0 && o.dims != dims) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), dims_mismatch_text(fn));
                }
                dims = o.dims;
                for (size_t c = 0; c < o.dims; ++c) {
                    if (o.integral) { o.i[c] = math::i32_at(*v, c); o.f[c] = static_cast<script_float>(o.i[c]); }
                    else { o.f[c] = static_cast<script_float>(math::f32_at(*v, c)); }
                }
            } else if (raw == script_value::TYPEID_INT) {
                o.integral = true;
                for (size_t c = 0; c < math::k_max_dims; ++c) { o.i[c] = v->unchecked_as_int(); o.f[c] = static_cast<script_float>(o.i[c]); }
            } else if (raw == script_value::TYPEID_FLOAT) {
                for (size_t c = 0; c < math::k_max_dims; ++c) { o.f[c] = v->unchecked_as_float(); }
            } else {
                return type_error();
            }
        }
        if (dims == 0) { return type_error(); }

        const bool exact_family = fn == math_fn::abs_ || fn == math_fn::min_ || fn == math_fn::max_ || fn == math_fn::clamp_;
        bool all_integral = true;
        for (size_t a = 0; a < argc; ++a) { all_integral = all_integral && ops[a].integral; }
        float f[math::k_max_dims] = {0, 0, 0, 0};
        int32_t out[math::k_max_dims] = {0, 0, 0, 0};
        if (exact_family && all_integral) {
            for (size_t c = 0; c < dims; ++c) {
                script_int r = 0;
                switch (fn) {
                    case math_fn::abs_: r = ops[0].i[c] < 0 ? -ops[0].i[c] : ops[0].i[c]; break;
                    case math_fn::min_: r = ops[0].i[c] <= ops[1].i[c] ? ops[0].i[c] : ops[1].i[c]; break;
                    case math_fn::max_: r = ops[0].i[c] >= ops[1].i[c] ? ops[0].i[c] : ops[1].i[c]; break;
                    default: r = ops[0].i[c] < ops[1].i[c] ? ops[1].i[c] : (ops[0].i[c] > ops[2].i[c] ? ops[2].i[c] : ops[0].i[c]); break;
                }
                auto narrowed = math::to_i32(r);
                if (!narrowed) { return narrowed.error_value(); }
                out[c] = narrowed.value();
            }
            return math::make(math::family_kind(true, dims), f, out, eng);
        }
        for (size_t c = 0; c < dims; ++c) {
            const script_float a = ops[0].f[c];
            script_float r = 0.0;
            switch (fn) {
                case math_fn::floor_: r = std::floor(a); break;
                case math_fn::ceil_: r = std::ceil(a); break;
                case math_fn::round_: r = std::round(a); break;
                case math_fn::abs_: r = std::fabs(a); break;
                case math_fn::min_: r = a <= ops[1].f[c] ? a : ops[1].f[c]; break;
                case math_fn::max_: r = a >= ops[1].f[c] ? a : ops[1].f[c]; break;
                case math_fn::clamp_: r = a < ops[1].f[c] ? ops[1].f[c] : (a > ops[2].f[c] ? ops[2].f[c] : a); break;
                case math_fn::lerp_: case math_fn::mix_: r = (ops[2].f[c] * (ops[1].f[c] - a)) + a; break;
                case math_fn::unmix_: r = (ops[1].f[c] == a) ? 0.0 : (ops[2].f[c] - a) / (ops[1].f[c] - a); break;
                case math_fn::remap_: r = remap_value(a, ops[1].f[c], ops[2].f[c], ops[3].f[c], ops[4].f[c]); break;
                default: r = eased_value(fn, a, ops[1].f[c], ops[2].f[c], ops[3].f[c]); break;
            }
            f[c] = math::to_f32(r);
        }
        return math::make(math::family_kind(false, dims), f, out, eng);
    }

    // math::vec2(...) ... math::ivec3(...): out of line, so the constructor's temporaries stay off the scalar kernel's frame
    JAI_MATH_COLD inline checked_result<script_value> eval_vec_constructor(math_fn fn, const script_value* const* args, size_t argc, engine* eng) {
        const script_value_type kind = fn == math_fn::vec2_ ? script_value_type::jai_vec2_type
                                     : fn == math_fn::vec3_ ? script_value_type::jai_vec3_type
                                     : fn == math_fn::vec4_ ? script_value_type::jai_vec4_type
                                     : fn == math_fn::ivec2_ ? script_value_type::jai_ivec2_type : script_value_type::jai_ivec3_type;
        if (argc > math::k_max_dims) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), math::ctor_arity_text(kind));
        }
        // S8: a bound argument decodes once (a host variable reads its current value)
        script_value decoded[math::k_max_dims] = {script_value(std::monostate{}, eng), script_value(std::monostate{}, eng),
                                                 script_value(std::monostate{}, eng), script_value(std::monostate{}, eng)};
        const script_value* view[math::k_max_dims] = {nullptr, nullptr, nullptr, nullptr};
        for (size_t k = 0; k < argc; ++k) {
            if (args[k]->raw_storage_index() == script_value::TYPEID_CPP_BOUND) {
                decoded[k] = args[k]->bound_decoded_temp();
                view[k] = &decoded[k];
            } else {
                view[k] = args[k];
            }
        }
        return math::construct(kind, view, argc, eng);
    }

    // The ONE evaluation kernel, used verbatim by both backends. Args arrive as an
    // array of DEREF'D pointers (zero-copy from the caller's stack). Type discipline
    // per ruling: ONE variant read per arg, switch on it; cpp_bound decodes through
    // the S8 prologue shape. Every case arity-checks BEFORE touching args.
    inline checked_result<script_value> eval_math_intrinsic(math_fn fn, const script_value* const* args, size_t argc,
                                                            engine* eng, math_rng_state& rng) {
        // Normalize each operand once: yields (is_number, as_double, is_int, as_int)
        struct num {
            bool ok = false;
            bool is_int = false;
            script_int i = 0;
            script_float f = 0.0;
            script_float d() const noexcept { return is_int ? static_cast<script_float>(i) : f; }
        };
        auto read = [](const script_value& v) noexcept -> num {
            num out;
            switch (v.raw_storage_index()) {
                case script_value::TYPEID_INT: out.ok = true; out.is_int = true; out.i = v.unchecked_as_int(); break;
                case script_value::TYPEID_FLOAT: out.ok = true; out.f = v.unchecked_as_float(); break;
                case script_value::TYPEID_CPP_BOUND: {
                    const script_value decoded = v.bound_decoded_temp();   // S8 prologue
                    if (decoded.raw_storage_index() == script_value::TYPEID_INT) {
                        out.ok = true; out.is_int = true; out.i = decoded.unchecked_as_int();
                    } else if (decoded.raw_storage_index() == script_value::TYPEID_FLOAT) {
                        out.ok = true; out.f = decoded.unchecked_as_float();
                    }
                    break;
                }
                default: break;
            }
            return out;
        };
        auto bad_arity = [&](const char* text) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), text);
        };
        auto bad_type = [&](const char* text) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), text);
        };

        switch (fn) {
            case math_fn::floor_: {
                if (argc != 1) return bad_arity("math::floor expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return eval_vec_math(fn, args, argc, eng, "math::floor expects a numeric argument");
                return checked_result<script_value>(script_value(std::floor(a.d()), eng));
            }
            case math_fn::ceil_: {
                if (argc != 1) return bad_arity("math::ceil expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return eval_vec_math(fn, args, argc, eng, "math::ceil expects a numeric argument");
                return checked_result<script_value>(script_value(std::ceil(a.d()), eng));
            }
            case math_fn::itrunc: {
                if (argc != 1) return bad_arity("math::itrunc expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return bad_type("math::itrunc expects a numeric argument");
				if (a.is_int) { return script_value(a.i, eng); }
				auto result = checked_float_to_integer(a.f);
				if (!result) { return result.error_value(); }
				return script_value(result.value(), eng);
            }
            case math_fn::ifloor: {
                if (argc != 1) return bad_arity("math::ifloor expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return bad_type("math::ifloor expects a numeric argument");
				if (a.is_int) { return script_value(a.i, eng); }
				auto result = checked_float_to_integer(std::floor(a.f));
				if (!result) { return result.error_value(); }
				return script_value(result.value(), eng);
            }
            case math_fn::sqrt_: {
                if (argc != 1) return bad_arity("math::sqrt expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return bad_type("math::sqrt expects a numeric argument");
                return checked_result<script_value>(script_value(std::sqrt(a.d()), eng));
            }
            case math_fn::pow_: {
                if (argc != 2) return bad_arity("math::pow expects 2 arguments");
                const num a = read(*args[0]), b = read(*args[1]);
                if (!a.ok || !b.ok) return bad_type("math::pow expects numeric arguments");
                return checked_result<script_value>(script_value(std::pow(a.d(), b.d()), eng));
            }
            case math_fn::abs_: {
                if (argc != 1) return bad_arity("math::abs expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return eval_vec_math(fn, args, argc, eng, "math::abs expects a numeric argument");
                if (a.is_int) {
                    // |INT64_MIN| overflows; surface the overflow policy's catchable error
                    if (a.i == INT64_MIN) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::invalid_numeric_operand),
                            "Integer overflow in math::abs");
                    }
                    return checked_result<script_value>(script_value(a.i < 0 ? -a.i : a.i, eng));
                }
                return checked_result<script_value>(script_value(std::fabs(a.f), eng));
            }
            case math_fn::min_:
            case math_fn::max_: {
                if (argc != 2) return bad_arity(fn == math_fn::min_ ? "math::min expects 2 arguments" : "math::max expects 2 arguments");
                const num a = read(*args[0]), b = read(*args[1]);
                if (!a.ok || !b.ok) return eval_vec_math(fn, args, argc, eng, fn == math_fn::min_ ? "math::min expects numeric arguments" : "math::max expects numeric arguments");
				// Integer pairs stay exact above 2^53; mixed pairs keep double promotion.
				const bool pick_a = a.is_int && b.is_int
					? (fn == math_fn::min_ ? a.i <= b.i : a.i >= b.i)
					: (fn == math_fn::min_ ? a.d() <= b.d() : a.d() >= b.d());
                const num& p = pick_a ? a : b;
                if (p.is_int) return checked_result<script_value>(script_value(p.i, eng));
                return checked_result<script_value>(script_value(p.f, eng));
            }
            case math_fn::clamp_: {
                if (argc != 3) return bad_arity("math::clamp expects 3 arguments (value, lo, hi)");
                const num v = read(*args[0]), lo = read(*args[1]), hi = read(*args[2]);
                if (!v.ok || !lo.ok || !hi.ok) return eval_vec_math(fn, args, argc, eng, "math::clamp expects numeric arguments");
				const bool below = v.is_int && lo.is_int ? v.i < lo.i : v.d() < lo.d();
				const num& p = below ? lo : ((v.is_int && hi.is_int ? v.i > hi.i : v.d() > hi.d()) ? hi : v);
                if (p.is_int) return checked_result<script_value>(script_value(p.i, eng));
                return checked_result<script_value>(script_value(p.f, eng));
            }
            case math_fn::sin_: {
                if (argc != 1) return bad_arity("math::sin expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return bad_type("math::sin expects a numeric argument");
                return checked_result<script_value>(script_value(std::sin(a.d()), eng));
            }
            case math_fn::cos_: {
                if (argc != 1) return bad_arity("math::cos expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return bad_type("math::cos expects a numeric argument");
                return checked_result<script_value>(script_value(std::cos(a.d()), eng));
            }
            case math_fn::tan_: {
                if (argc != 1) return bad_arity("math::tan expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return bad_type("math::tan expects a numeric argument");
                return checked_result<script_value>(script_value(std::tan(a.d()), eng));
            }
            case math_fn::atan2_: {
                if (argc != 2) return bad_arity("math::atan2 expects 2 arguments (y, x)");
                const num y = read(*args[0]), x = read(*args[1]);
                if (!y.ok || !x.ok) return bad_type("math::atan2 expects numeric arguments");
                return checked_result<script_value>(script_value(std::atan2(y.d(), x.d()), eng));
            }
            case math_fn::distance:
            case math_fn::distance_squared: {
                if (argc != 4) {
                    return bad_arity(fn == math_fn::distance ? "math::distance expects 4 arguments (x1, y1, x2, y2)"
                                                             : "math::distance_squared expects 4 arguments (x1, y1, x2, y2)");
                }
                const num x1 = read(*args[0]), y1 = read(*args[1]), x2 = read(*args[2]), y2 = read(*args[3]);
                if (!x1.ok || !y1.ok || !x2.ok || !y2.ok) {
                    return bad_type(fn == math_fn::distance ? "math::distance expects numeric arguments"
                                                            : "math::distance_squared expects numeric arguments");
                }
                const script_float dx = x2.d() - x1.d();
                const script_float dy = y2.d() - y1.d();
                const script_float d2 = dx * dx + dy * dy;
                return checked_result<script_value>(script_value(fn == math_fn::distance ? std::sqrt(d2) : d2, eng));
            }
            case math_fn::radians_to_degrees: {
                if (argc != 1) return bad_arity("math::radians_to_degrees expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return bad_type("math::radians_to_degrees expects a numeric argument");
                return checked_result<script_value>(script_value(a.d() * (180.0 / 3.14159265358979323846), eng));
            }
            case math_fn::degrees_to_radians: {
                if (argc != 1) return bad_arity("math::degrees_to_radians expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return bad_type("math::degrees_to_radians expects a numeric argument");
                return checked_result<script_value>(script_value(a.d() * (3.14159265358979323846 / 180.0), eng));
            }
            case math_fn::random_:
            case math_fn::random_range: {
                if (argc == 0) {
                    // math::random(): uniform [0.0, 1.0] inclusive
                    return checked_result<script_value>(script_value(rng.next_unit_inclusive(), eng));
                }
                if (argc != 2) return bad_arity("math::random expects 0 or 2 arguments (min, max)");
                const num lo = read(*args[0]), hi = read(*args[1]);
                if (!lo.ok || !hi.ok) return bad_type("math::random expects numeric arguments");
                if (lo.is_int && hi.is_int) {
                    // Inclusive int range; empty/inverted range errors cleanly
                    if (hi.i < lo.i) return bad_type("math::random range is inverted (max < min)");
                    const uint64_t span = static_cast<uint64_t>(hi.i) - static_cast<uint64_t>(lo.i) + 1ull;
                    // span == 0 means the FULL int64 range (hi - lo wrapped); modulo by 0 guard
                    const uint64_t r = span == 0 ? rng.next() : rng.next() % span;
                    return checked_result<script_value>(script_value(static_cast<script_int>(static_cast<uint64_t>(lo.i) + r), eng));
                }
                if (hi.d() < lo.d()) return bad_type("math::random range is inverted (max < min)");
                return checked_result<script_value>(script_value(lo.d() + rng.next_unit_inclusive() * (hi.d() - lo.d()), eng));
            }
            case math_fn::random_seed: {
                if (argc != 1) return bad_arity("math::random_seed expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return bad_type("math::random_seed expects a numeric argument");
				if (a.is_int) {
					rng.seed(static_cast<uint64_t>(a.i));
				} else {
					auto result = checked_float_to_integer<uint64_t>(a.f);
					if (!result) { return result.error_value(); }
					rng.seed(result.value());
				}
                return checked_result<script_value>(script_value(std::monostate{}, eng));
            }
            case math_fn::wrap_add_:
            case math_fn::wrap_sub_:
            case math_fn::wrap_mul_: {
                // Two's-complement wraparound computed in unsigned (UB-free), integer-only:
                // silently truncating a float would defeat the point of explicit wrap math.
                if (argc != 2) {
                    return bad_arity(fn == math_fn::wrap_add_ ? "math::wrap_add expects 2 arguments"
                                   : fn == math_fn::wrap_sub_ ? "math::wrap_sub expects 2 arguments"
                                                              : "math::wrap_mul expects 2 arguments");
                }
                const num a = read(*args[0]), b = read(*args[1]);
                if (!a.ok || !b.ok || !a.is_int || !b.is_int) {
                    return bad_type(fn == math_fn::wrap_add_ ? "math::wrap_add expects integer arguments"
                                  : fn == math_fn::wrap_sub_ ? "math::wrap_sub expects integer arguments"
                                                             : "math::wrap_mul expects integer arguments");
                }
                const uint64_t ua = static_cast<uint64_t>(a.i), ub = static_cast<uint64_t>(b.i);
                const uint64_t r = fn == math_fn::wrap_add_ ? ua + ub
                                 : fn == math_fn::wrap_sub_ ? ua - ub
                                                            : ua * ub;
                return checked_result<script_value>(script_value(static_cast<script_int>(r), eng));
            }

            // ---- interpolation family (formulas verbatim from the stdlib registrations) ----
            case math_fn::lerp_:
            case math_fn::mix_: {
                if (argc != 3) return bad_arity(fn == math_fn::lerp_ ? "math::lerp expects 3 arguments (start, end, percent)"
                                                                     : "math::mix expects 3 arguments (start, end, percent)");
                const num s = read(*args[0]), e = read(*args[1]), t = read(*args[2]);
                if (!s.ok || !e.ok || !t.ok) return eval_vec_math(fn, args, argc, eng, fn == math_fn::lerp_ ? "math::lerp expects numeric arguments"
                                                                                                             : "math::mix expects numeric arguments");
                return checked_result<script_value>(script_value((t.d() * (e.d() - s.d())) + s.d(), eng));
            }
            case math_fn::unmix_: {
                if (argc != 3) return bad_arity("math::unmix expects 3 arguments (start, end, value)");
                const num s = read(*args[0]), e = read(*args[1]), v = read(*args[2]);
                if (!s.ok || !e.ok || !v.ok) return eval_vec_math(fn, args, argc, eng, "math::unmix expects numeric arguments");
                return checked_result<script_value>(script_value(
                    (e.d() == s.d()) ? 0.0 : (v.d() - s.d()) / (e.d() - s.d()), eng));
            }
            case math_fn::mix_in_:
            case math_fn::mix_out_:
            case math_fn::mix_in_out_:
            case math_fn::mix_out_in_:
            case math_fn::unmix_in_:
            case math_fn::unmix_out_:
            case math_fn::unmix_in_out_:
            case math_fn::unmix_out_in_: {
                if (argc != 4) return bad_arity("math:: eased mix/unmix expects 4 arguments (start, end, percent_or_value, strength)");
                const num s_ = read(*args[0]), e_ = read(*args[1]), p_ = read(*args[2]), k_ = read(*args[3]);
                if (!s_.ok || !e_.ok || !p_.ok || !k_.ok) return eval_vec_math(fn, args, argc, eng, "math:: eased mix/unmix expects numeric arguments");
                const script_float r = eased_value(fn, s_.d(), e_.d(), p_.d(), k_.d());
                return checked_result<script_value>(script_value(r, eng));
            }

            case math_fn::vec2_:
            case math_fn::vec3_:
            case math_fn::vec4_:
            case math_fn::ivec2_:
            case math_fn::ivec3_:
                return eval_vec_constructor(fn, args, argc, eng);

            case math_fn::round_: {
                if (argc != 1) return bad_arity("math::round expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return eval_vec_math(fn, args, argc, eng, "math::round expects a numeric argument");
                return checked_result<script_value>(script_value(std::round(a.d()), eng));
            }
            case math_fn::remap_: {
                if (argc != 5) return bad_arity("math::remap expects 5 arguments (value, in_lo, in_hi, out_lo, out_hi)");
                const num v = read(*args[0]), il = read(*args[1]), ih = read(*args[2]), ol = read(*args[3]), oh = read(*args[4]);
                if (!v.ok || !il.ok || !ih.ok || !ol.ok || !oh.ok) return eval_vec_math(fn, args, argc, eng, "math::remap expects numeric arguments");
                return checked_result<script_value>(script_value(remap_value(v.d(), il.d(), ih.d(), ol.d(), oh.d()), eng));
            }

            default:
                return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation),
                    "Unknown math intrinsic");
        }
    }

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_MATH_INTRINSICS_HPP__
