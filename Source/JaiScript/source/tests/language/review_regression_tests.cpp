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
            auto e = make_engine();
            check_eq(std::string("hello world"), e->execute("`hello world`").as_string());
        });
        test("tmpl_string_interp_at_eof", [this]() {
            auto e = make_engine();
            check_eq(std::string("val=7"), e->execute("auto y = 7; `val=${y}`").as_string());
        });

        // ---- #6 lexer: malformed octal must error, valid octal still works ----
        test("octal_valid", [this]() {
            auto e = make_engine();
            check_eq((int64_t)8, e->execute("010").as_int());   // C-style octal
        });
        test("octal_malformed_throws", [this]() {
            auto e = make_engine();
            check_throws([&]() { e->execute("auto x = 08; x"); });
        });

        // ---- #7 parser: deep prefix-operator chain must not crash ----
        test("deep_unary_chain_modest", [this]() {
            auto e = make_engine();
            std::string src(100, '!');  // even count of '!' applied to true => true; well under the guard
            src += "true";
            check(e->execute(src).as_bool());
        });
        test("deep_unary_chain_guard_no_crash", [this]() {
            auto e = make_engine();
            std::string src(400, '!');  // > MAX_PARSE_DEPTH
            src += "true";
            // The depth guard makes the parser fail gracefully (no stack overflow):
            // since the 2026-06 review fixes, guard failures surface as a thrown
            // parse_error with a diagnostic instead of a silent null. Engine stays usable.
            check_throws([&]() { e->execute(src); });
            check_eq((int64_t)3, e->execute("1 + 2").as_int());
        });

        // ---- #19 range-for inside a function ----
        test("range_for_in_function", [this]() {
            auto e = make_engine();
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
            auto e = make_engine();
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
            auto e = make_engine();
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
            auto e = make_engine();
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
            auto e = make_engine();
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
            auto e = make_engine();
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
            auto e = make_engine();
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
            auto e = make_engine();
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
            auto e = make_engine();
            auto r = e->execute(R"(
                var m = {"a": 1, "b": 2};
                var x = m["zzz"];
                var y = m["qqq"];
                m.size()
            )");
            check_eq((int64_t)2, r.as_int());
        });
        test("map_assign_still_inserts", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                var m = {"a": 1};
                m["new"] = 5;
                m["a"] + m["new"] + m.size()
            )");
            check_eq((int64_t)8, r.as_int());  // 1 + 5 + 2
        });

        // ---- #28 cpp-bound 4-byte float must be read as a float, not 8 bytes ----
        test("cpp_bound_float_read", [this]() {
            auto e = make_engine();
            float hp = 12.5f;
            e->add_global_ref("hp", hp);
            check_near(12.5, e->execute("hp").as_float(), 0.0001);
        });
        // ---- #29 cpp-bound int64 -> float must keep all 64 bits ----
        test("cpp_bound_int64_to_float", [this]() {
            auto e = make_engine();
            int64_t big = 5000000000LL;  // > INT32_MAX
            e->add_global_ref("big", big);
            check_near(5000000000.0, e->execute("big * 1.0").as_float(), 1.0);
        });

        // ---- #26/#27 complex-typed & float map keys keep std::map intact ----
        test("array_map_keys_stable", [this]() {
            auto e = make_engine();
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
            auto e = make_engine();
            const char* src = "auto x = 9223372036854775807; x + 1";  // INT64_MAX + 1
            if (e->throw_on_overflow()) {
                check_throws([&]() { e->execute(src); });
            } else {
                check_eq((int64_t)INT64_MIN, e->execute(src).as_int());  // wraps to INT64_MIN
            }
        });
        test("int_overflow_mul", [this]() {
            auto e = make_engine();
            const char* src = "auto x = 9223372036854775807; x * 2";  // INT64_MAX * 2
            if (e->throw_on_overflow()) {
                check_throws([&]() { e->execute(src); });
            } else {
                check_eq((int64_t)-2, e->execute(src).as_int());  // 2*(2^63-1) wraps to -2
            }
        });
        test("int_min_div_neg1", [this]() {
            auto e = make_engine();
            const char* src = "auto x = -9223372036854775807 - 1; auto d = -1; x / d";  // INT64_MIN / -1
            if (e->throw_on_overflow()) {
                check_throws([&]() { e->execute(src); });
            } else {
                check_eq((int64_t)INT64_MIN, e->execute(src).as_int());  // wraps to INT64_MIN
            }
        });
        test("int_no_false_overflow", [this]() {  // policy-independent: 2e9*2 = 4e9 fits int64
            auto e = make_engine();
            check_eq((int64_t)4000000000LL, e->execute("auto x = 2000000000; x * 2").as_int());
        });
        test("int_mod_neg1_is_zero", [this]() {   // policy-independent: a % -1 == 0, no trap
            auto e = make_engine();
            check_eq((int64_t)0, e->execute("auto x = -9223372036854775807 - 1; auto d = -1; x % d").as_int());
        });

        // ---- #35 JSON float serialization round-trips at full precision ----
        test("json_float_roundtrip", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            check(e->execute("var x = 0.1 + 0.2; from_json(to_json(x)) == x").as_bool());
        });
        // ---- #34 deeply nested JSON raises a catchable error (no stack overflow) ----
        test("json_deep_nesting_throws", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            std::string s(300, '[');   // 300 > JAI_MAX_JSON_PARSE_DEPTH (128)
            s.append(300, ']');
            e->add_global("s", s);
            check_throws([&]() { e->execute("from_json(s)"); });
        });

        // ---- #23 field initializers run in DECLARATION order (not intern order) ----
        test("field_init_declaration_order", [this]() {
            auto e = make_engine();
            // 'total' is interned first (global) -> smaller name_id than 'base'; the old
            // map-ordered init ran total's initializer before base's -> base unset.
            auto r = e->execute(R"(
                auto total = 0;
                class Account { int base = 100; int total = this.base + 5; }
                auto a = Account();
                a.total
            )");
            check_eq((int64_t)105, r.as_int());
        });

        // ---- #24 this() delegation: target's assignment wins, other defaults kept ----
        test("this_delegation_no_clobber", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                class C {
                    int x = 999;
                    int y = 7;
                    C() : this(42) { }
                    C(int v) { this.x = v; }
                }
                auto c = C();
                c.x * 1000 + c.y
            )");
            check_eq((int64_t)42007, r.as_int());  // x=42 (target), y=7 (default kept)
        });

        // ---- #31 array callbacks that mutate the same array must not crash ----
        test("remove_if_callback_mutation", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                var arr = [1, 2, 3, 4, 5, 6, 7, 8];
                arr.remove_if([](auto x) -> auto { if (x == 2) { arr.push(99); } return x % 2 == 0; });
                arr.length()
            )");
            check_eq((int64_t)4, r.as_int());  // survivors [1,3,5,7]; the push is discarded
        });
        test("sort_mixed_numeric_no_ub", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                var a = [3.5, 1, 2.5, 2];
                a.sort();
                a[0]
            )");
            check_eq((int64_t)1, r.as_int());  // value order: [1, 2, 2.5, 3.5]
        });

        // ---- #41 reference into an array survives reallocation (no heap corruption) ----
        // ---- #20 closure capture of enclosing function locals (all forms) ----
        test("closure_capture_local_auto_no_capture", [this]() {
            // [] lambda (no explicit capture) must still see enclosing locals
            // via the automatic-local-capture feature.
            auto e = make_engine();
            auto r = e->execute(R"(
                auto make() -> auto {
                    auto local = 7;
                    return []() -> auto { return local + 1; };
                }
                auto f = make();
                f()
            )");
            check_eq((int64_t)8, r.as_int());
        });
        test("closure_capture_local_eq", [this]() {
            // [=] captures local by value — snapshot at capture time
            auto e = make_engine();
            auto r = e->execute(R"(
                auto make() -> auto {
                    auto local = 10;
                    return [=]() -> auto { return local * 2; };
                }
                make()()
            )");
            check_eq((int64_t)20, r.as_int());
        });
        test("closure_capture_local_inline", [this]() {
            // Inline (non-escaping) closure also works
            auto e = make_engine();
            auto r = e->execute(R"(
                auto result = 0;
                auto x = 5;
                auto f = [=]() -> auto { return x + 3; };
                result = f();
                result
            )");
            check_eq((int64_t)8, r.as_int());
        });
        test("closure_capture_local_snapshot", [this]() {
            // Captured value is a snapshot — mutation of outer var doesn't affect closure
            auto e = make_engine();
            auto r = e->execute(R"(
                auto x = 10;
                auto f = [=]() -> auto { return x; };
                x = 999;
                f()
            )");
            check_eq((int64_t)10, r.as_int());
        });
        test("closure_capture_local_explicit", [this]() {
            // Explicit [local] capture of a function local
            auto e = make_engine();
            auto r = e->execute(R"(
                auto make(auto n) -> auto {
                    auto doubled = n * 2;
                    return [doubled]() -> auto { return doubled + 1; };
                }
                make(6)()
            )");
            check_eq((int64_t)13, r.as_int());
        });
        test("closure_capture_multiple_locals", [this]() {
            // Multiple locals captured (auto + different types)
            auto e = make_engine();
            auto r = e->execute(R"(
                auto make() -> auto {
                    auto a = 3;
                    auto b = 4;
                    return [=]() -> auto { return a * a + b * b; };
                }
                make()()
            )");
            check_eq((int64_t)25, r.as_int());
        });

        // ---- for-body shadow scoping: fresh scope per iteration (vm/while/C++ parity) ----
        // The interpreter's counting-loop fast path reused one body env across iterations,
        // so a body `var g` shadow leaked into the next iteration's reads.
        test("for_body_shadow_fresh_per_iteration", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            auto r = e->execute(R"(
                var g = 1;
                var out = "";
                for (var i = 0; i < 3; ++i) {
                    out += to_string(g) + ";";
                    var g = 100;
                }
                out + to_string(g);
            )");
            check_eq(std::string("1;1;1;1"), r.as_string());
        });
        test("for_body_shadow_fn_call_variant", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                function h() -> auto { return 1; }
                var s = 0;
                for (var i = 0; i < 3; ++i) {
                    s += h();
                    var h = 9;
                    s += h;
                }
                s;
            )");
            check_eq((int64_t)30, r.as_int());
        });
        test("for_body_shadow_general_path_variant", [this]() {
            // non-literal init keeps this off the counting fast path
            auto e = make_engine();
            stdlib::register_all(*e);
            auto r = e->execute(R"(
                var start = 0;
                var g = 1;
                var out = "";
                for (var i = start; i < 3; ++i) {
                    out += to_string(g) + ";";
                    var g = 100;
                }
                out;
            )");
            check_eq(std::string("1;1;1;"), r.as_string());
        });

        test("range_for_ref_realloc_no_corruption", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                auto arr = [10, 20, 30];
                for (auto& x : arr) {
                    var j = 0;
                    while (j < 64) { arr.push(0); j = j + 1; }  // force a reallocation
                    x = 999;                                    // write through AFTER realloc
                    break;
                }
                arr[0]
            )");
            check_eq((int64_t)999, r.as_int());  // recomputed element address, not a dangling ptr
        });
    }
};

} // namespace jai::foundry::tests

using review_regression_tests = jai::foundry::tests::review_regression_tests;
FOUNDRY_REGISTER(review_regression_tests)
