#pragma once

#ifndef __JAISCRIPT_DETAIL_MATH_INTRINSICS_HPP__
#define __JAISCRIPT_DETAIL_MATH_INTRINSICS_HPP__

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/checked_result.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/detail/ast.hpp>
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
                if (!a.ok) return bad_type("math::floor expects a numeric argument");
                return checked_result<script_value>(script_value(std::floor(a.d()), eng));
            }
            case math_fn::ceil_: {
                if (argc != 1) return bad_arity("math::ceil expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return bad_type("math::ceil expects a numeric argument");
                return checked_result<script_value>(script_value(std::ceil(a.d()), eng));
            }
            case math_fn::itrunc: {
                if (argc != 1) return bad_arity("math::itrunc expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return bad_type("math::itrunc expects a numeric argument");
                return checked_result<script_value>(script_value(a.is_int ? a.i : static_cast<script_int>(a.f), eng));
            }
            case math_fn::ifloor: {
                if (argc != 1) return bad_arity("math::ifloor expects 1 argument");
                const num a = read(*args[0]);
                if (!a.ok) return bad_type("math::ifloor expects a numeric argument");
                return checked_result<script_value>(script_value(a.is_int ? a.i : static_cast<script_int>(std::floor(a.f)), eng));
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
                if (!a.ok) return bad_type("math::abs expects a numeric argument");
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
                if (!a.ok || !b.ok) return bad_type(fn == math_fn::min_ ? "math::min expects numeric arguments" : "math::max expects numeric arguments");
                const bool pick_a = fn == math_fn::min_ ? (a.d() <= b.d()) : (a.d() >= b.d());
                const num& p = pick_a ? a : b;
                if (p.is_int) return checked_result<script_value>(script_value(p.i, eng));
                return checked_result<script_value>(script_value(p.f, eng));
            }
            case math_fn::clamp_: {
                if (argc != 3) return bad_arity("math::clamp expects 3 arguments (value, lo, hi)");
                const num v = read(*args[0]), lo = read(*args[1]), hi = read(*args[2]);
                if (!v.ok || !lo.ok || !hi.ok) return bad_type("math::clamp expects numeric arguments");
                const num& p = v.d() < lo.d() ? lo : (v.d() > hi.d() ? hi : v);
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
                rng.seed(a.is_int ? static_cast<uint64_t>(a.i) : static_cast<uint64_t>(a.f));
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
                if (!s.ok || !e.ok || !t.ok) return bad_type(fn == math_fn::lerp_ ? "math::lerp expects numeric arguments"
                                                                                  : "math::mix expects numeric arguments");
                return checked_result<script_value>(script_value((t.d() * (e.d() - s.d())) + s.d(), eng));
            }
            case math_fn::unmix_: {
                if (argc != 3) return bad_arity("math::unmix expects 3 arguments (start, end, value)");
                const num s = read(*args[0]), e = read(*args[1]), v = read(*args[2]);
                if (!s.ok || !e.ok || !v.ok) return bad_type("math::unmix expects numeric arguments");
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
                if (!s_.ok || !e_.ok || !p_.ok || !k_.ok) return bad_type("math:: eased mix/unmix expects numeric arguments");
                const script_float start = s_.d(), end = e_.d(), percent = p_.d(), strength = k_.d();
                script_float r = 0.0;
                switch (fn) {
                    case math_fn::mix_in_:
                        r = std::pow(percent, strength) * (end - start) + start;
                        break;
                    case math_fn::mix_out_:
                        r = (1.0 - std::pow(1.0 - percent, strength)) * (end - start) + start;
                        break;
                    case math_fn::mix_in_out_: {
                        const script_float half_range = (end - start) / 2.0 + start;
                        r = percent < 0.5
                            ? std::pow(percent * 2.0, strength) * (half_range - start) + start
                            : (1.0 - std::pow(1.0 - (percent - 0.5) * 2.0, strength)) * (end - half_range) + half_range;
                        break;
                    }
                    case math_fn::mix_out_in_: {
                        const script_float half_range = (end - start) / 2.0 + start;
                        r = percent < 0.5
                            ? (1.0 - std::pow(1.0 - percent * 2.0, strength)) * (half_range - start) + start
                            : std::pow((percent - 0.5) * 2.0, strength) * (end - half_range) + half_range;
                        break;
                    }
                    case math_fn::unmix_in_:
                        r = (end == start) ? end : std::pow((percent - start) / (end - start), 1.0 / strength);
                        break;
                    case math_fn::unmix_out_:
                        r = 1.0 - std::pow(1.0 - ((percent - start) / (end - start)), 1.0 / strength);
                        break;
                    case math_fn::unmix_in_out_: {
                        const script_float half_range = (end - start) / 2.0 + start;
                        r = percent < half_range
                            ? std::pow((percent - start) / (half_range - start), 1.0 / strength) / 2.0
                            : (1.0 - std::pow(1.0 - ((percent - half_range) / (end - half_range)), 1.0 / strength)) / 2.0 + 0.5;
                        break;
                    }
                    default: {   // unmix_out_in_
                        const script_float half_range = (end - start) / 2.0 + start;
                        r = percent < half_range
                            ? (1.0 - std::pow(1.0 - ((percent - start) / (half_range - start)), 1.0 / strength)) / 2.0
                            : std::pow((percent - half_range) / (end - half_range), 1.0 / strength) / 2.0 + 0.5;
                        break;
                    }
                }
                return checked_result<script_value>(script_value(r, eng));
            }

            default:
                return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation),
                    "Unknown math intrinsic");
        }
    }

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_MATH_INTRINSICS_HPP__
