// Regression tests for bugs found during the 2026-05 deep review.
// Each test documents the finding number from that review and reproduces the
// edge case that previously failed. Pure-script reproducers live here; the
// C++-bound numeric reproducers (cpp_bound float/int reads) live in
// source/tests/semantics/cpp_bound_numeric_tests.cpp.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class review_regression_tests : public suite {
public:
    review_regression_tests() : suite("Review Regressions") {}

    void forge_tests() override {
        // ---- #5 lexer: template (backtick) string at end-of-source ----
        test("tmpl_string_at_eof", [this]() {
            auto e = engine::make();
            check_eq(std::string("hello world"), e->execute("`hello world`").as_string());
        });
        test("tmpl_string_interp_at_eof", [this]() {
            auto e = engine::make();
            check_eq(std::string("val=7"), e->execute("auto y = 7; `val=${y}`").as_string());
        });

        // ---- #6 lexer: malformed octal must error, valid octal still works ----
        test("octal_valid", [this]() {
            auto e = engine::make();
            check_eq((int64_t)8, e->execute("010").as_int());   // C-style octal
        });
        test("octal_malformed_throws", [this]() {
            auto e = engine::make();
            check_throws([&]() { e->execute("auto x = 08; x"); });
        });

        // ---- #7 parser: deep prefix-operator chain must not crash ----
        test("deep_unary_chain_modest", [this]() {
            auto e = engine::make();
            std::string src(100, '!');  // even count of '!' applied to true => true; well under the guard
            src += "true";
            check(e->execute(src).as_bool());
        });
        test("deep_unary_chain_guard_no_crash", [this]() {
            auto e = engine::make();
            std::string src(400, '!');  // > MAX_PARSE_DEPTH
            src += "true";
            // The depth guard makes the parser recover gracefully (no stack
            // overflow). execute() returns null; the engine stays usable.
            e->execute(src);
            check_eq((int64_t)3, e->execute("1 + 2").as_int());
        });

        // ---- #19 range-for inside a function ----
        test("range_for_in_function", [this]() {
            auto e = engine::make();
            auto r = e->execute(R"(
                auto sum(auto arr) -> auto {
                    auto total = 0;
                    for (auto x : arr) { total = total + x; }
                    return total;
                }
                sum([1, 2, 3]);
            )");
            check_eq((int64_t)6, r.as_int());
        });
        test("range_for_ref_in_function", [this]() {
            auto e = engine::make();
            auto r = e->execute(R"(
                auto doubleAll(auto arr) -> auto {
                    for (auto& x : arr) { x = x * 2; }
                    return arr[0] + arr[1] + arr[2];
                }
                doubleAll([1, 2, 3]);
            )");
            check_eq((int64_t)12, r.as_int());
        });

        // ---- #8 set_local gap after a skipped block (agent-applied; lock it in) ----
        test("local_after_untaken_block", [this]() {
            auto e = engine::make();
            auto r = e->execute(R"(
                auto f() -> auto {
                    auto a = 1;
                    if (false) { auto b = 99; }
                    auto c = a + 2;
                    return c;
                }
                f();
            )");
            check_eq((int64_t)3, r.as_int());
        });

        // ---- #15 throw inside a while loop must not hang ----
        test("throw_in_while_no_hang", [this]() {
            auto e = engine::make();
            auto r = e->execute(R"(
                auto count = 0;
                try {
                    var i = 0;
                    while (i < 5) { count = count + 1; throw "x"; }
                } catch (e) { }
                count
            )");
            check_eq((int64_t)1, r.as_int());
        });
        test("throw_in_for_no_hang", [this]() {
            auto e = engine::make();
            auto r = e->execute(R"(
                auto count = 0;
                try {
                    for (var i = 0; i < 5; i = i + 1) { count = count + 1; throw "x"; }
                } catch (e) { }
                count
            )");
            check_eq((int64_t)1, r.as_int());
        });

        // ---- #16 shrinking an array during value range-for must not OOB ----
        test("range_for_shrink_no_oob", [this]() {
            auto e = engine::make();
            auto r = e->execute(R"(
                auto a = [1, 2, 3, 4, 5];
                auto seen = 0;
                auto first = true;
                for (auto x : a) { seen = seen + 1; if (first) { a.clear(); first = false; } }
                seen
            )");
            check_eq((int64_t)1, r.as_int());
        });

        // ---- #17 continue inside a switch case stops the case body & continues loop ----
        test("switch_continue_in_loop", [this]() {
            auto e = engine::make();
            auto r = e->execute(R"(
                auto total = 0;
                for (var i = 0; i < 4; i = i + 1) {
                    switch (i) {
                        case 1:
                            continue;
                            total = total + 1000;   // must NOT execute
                        default:
                            total = total + 1;
                    }
                }
                total
            )");
            check_eq((int64_t)3, r.as_int());
        });
        test("switch_break_does_not_break_loop", [this]() {
            auto e = engine::make();
            auto r = e->execute(R"(
                auto total = 0;
                for (var i = 0; i < 3; i = i + 1) {
                    switch (i) {
                        case 0: total = total + 10; break;
                        default: total = total + 1;
                    }
                }
                total
            )");
            check_eq((int64_t)12, r.as_int());
        });

        // ---- #32 reading a missing map key must NOT insert it ----
        test("map_read_missing_no_insert", [this]() {
            auto e = engine::make();
            auto r = e->execute(R"(
                var m = {"a": 1, "b": 2};
                var x = m["zzz"];
                var y = m["qqq"];
                m.size()
            )");
            check_eq((int64_t)2, r.as_int());
        });
        test("map_assign_still_inserts", [this]() {
            auto e = engine::make();
            auto r = e->execute(R"(
                var m = {"a": 1};
                m["new"] = 5;
                m["a"] + m["new"] + m.size()
            )");
            check_eq((int64_t)8, r.as_int());  // 1 + 5 + 2
        });

        // ---- #28 cpp-bound 4-byte float must be read as a float, not 8 bytes ----
        test("cpp_bound_float_read", [this]() {
            auto e = engine::make();
            float hp = 12.5f;
            e->add_global_ref("hp", hp);
            check_near(12.5, e->execute("hp").as_float(), 0.0001);
        });
        // ---- #29 cpp-bound int64 -> float must keep all 64 bits ----
        test("cpp_bound_int64_to_float", [this]() {
            auto e = engine::make();
            int64_t big = 5000000000LL;  // > INT32_MAX
            e->add_global_ref("big", big);
            check_near(5000000000.0, e->execute("big * 1.0").as_float(), 1.0);
        });

        // ---- #26/#27 complex-typed & float map keys keep std::map intact ----
        test("array_map_keys_stable", [this]() {
            auto e = engine::make();
            auto r = e->execute(R"(
                var m = {};
                m[[1, 2]] = "a";
                m[[3, 4]] = "b";
                m.size()
            )");
            check_eq((int64_t)2, r.as_int());
        });

        // ---- #9/#10/#11 integer overflow policy (compile-time; tests adapt to it) ----
        // Variables are used so the parser's constant-folder doesn't pre-evaluate these.
        // Behavior depends on the build: throw_on_overflow() true => raises; false => wraps.
        test("int_overflow_add", [this]() {
            auto e = engine::make();
            const char* src = "auto x = 9223372036854775807; x + 1";  // INT64_MAX + 1
            if (e->throw_on_overflow()) {
                check_throws([&]() { e->execute(src); });
            } else {
                check_eq((int64_t)INT64_MIN, e->execute(src).as_int());  // wraps to INT64_MIN
            }
        });
        test("int_overflow_mul", [this]() {
            auto e = engine::make();
            const char* src = "auto x = 9223372036854775807; x * 2";  // INT64_MAX * 2
            if (e->throw_on_overflow()) {
                check_throws([&]() { e->execute(src); });
            } else {
                check_eq((int64_t)-2, e->execute(src).as_int());  // 2*(2^63-1) wraps to -2
            }
        });
        test("int_min_div_neg1", [this]() {
            auto e = engine::make();
            const char* src = "auto x = -9223372036854775807 - 1; auto d = -1; x / d";  // INT64_MIN / -1
            if (e->throw_on_overflow()) {
                check_throws([&]() { e->execute(src); });
            } else {
                check_eq((int64_t)INT64_MIN, e->execute(src).as_int());  // wraps to INT64_MIN
            }
        });
        test("int_no_false_overflow", [this]() {  // policy-independent: 2e9*2 = 4e9 fits int64
            auto e = engine::make();
            check_eq((int64_t)4000000000LL, e->execute("auto x = 2000000000; x * 2").as_int());
        });
        test("int_mod_neg1_is_zero", [this]() {   // policy-independent: a % -1 == 0, no trap
            auto e = engine::make();
            check_eq((int64_t)0, e->execute("auto x = -9223372036854775807 - 1; auto d = -1; x % d").as_int());
        });

        // ---- #35 JSON float serialization round-trips at full precision ----
        test("json_float_roundtrip", [this]() {
            auto e = engine::make();
            jai::stdlib::register_all(*e);
            check(e->execute("var x = 0.1 + 0.2; from_json(to_json(x)) == x").as_bool());
        });
        // ---- #34 deeply nested JSON raises a catchable error (no stack overflow) ----
        test("json_deep_nesting_throws", [this]() {
            auto e = engine::make();
            jai::stdlib::register_all(*e);
            std::string s(300, '[');   // 300 > JAI_MAX_JSON_PARSE_DEPTH (128)
            s.append(300, ']');
            e->add_global("s", s);
            check_throws([&]() { e->execute("from_json(s)"); });
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
using review_regression_tests = jai::foundry::tests::review_regression_tests;
FOUNDRY_REGISTER(review_regression_tests)
