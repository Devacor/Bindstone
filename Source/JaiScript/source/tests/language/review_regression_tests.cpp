// Regression tests for bugs found during the 2026-05 deep review.
// Each test documents the finding number from that review and reproduces the
// edge case that previously failed. Pure-script reproducers live here; the
// C++-bound numeric reproducers (cpp_bound float/int reads) live in
// source/tests/semantics/cpp_bound_numeric_tests.cpp.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>
#include <optional>

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
        // ---- fuzz FZ-SPACESHIP-OVERFLOW-SWALLOW (seed 9336): the parser wrap-folds
        // 1 + INT64_MAX into a literal INT64_MIN; the interpreter's literal unary-minus
        // fast path then negated it UNCHECKED (silent wrap) while the VM raised ----
        test("fuzz_unary_minus_literal_overflow", [this]() {
            auto e = make_engine();
            const char* src = "(-((1 + 9223372036854775807))) <=> 9223372036854775807";
            if (e->throw_on_overflow()) {
                check_throws([&]() { e->execute(src); });
            } else {
                check_eq((int64_t)-1, e->execute(src).as_int());  // wraps: INT64_MIN <=> INT64_MAX
            }
        });
        test("fuzz_unary_minus_overflow_text_parity", [this]() {
            std::string msg[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                if (!e->throw_on_overflow()) { return; }  // wrap build: nothing to compare
                try { e->execute("(-((1 + 9223372036854775807))) <=> 1;"); }
                catch (const std::exception& ex) { msg[idx] = ex.what(); }
                ++idx;
            }
            check_eq(msg[0], msg[1], "overflow error text is byte-identical across backends");
            // PARSER-FOLD-WRAPS fix: the folder now declines to fold 1 + INT64_MAX, so
            // the runtime '+' raises (correct attribution) before unary '-' ever runs.
            check_true(msg[0].find("Integer overflow in '+'") != std::string::npos,
                "the un-folded '+' raises with its own operator name");
        });

        // ---- PARSER-FOLD-WRAPS: the constant folder used raw + - * (signed-overflow UB
        // and a silent wrap bypassing the checked policy). It now folds through
        // jai::ints and declines to fold on overflow so the runtime raises. ----
        test("parser_fold_overflow_defers_to_runtime", [this]() {
            std::string msg[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                if (!e->throw_on_overflow()) {
                    // Wrap build: folding the wrapped value IS the policy
                    check_eq((int64_t)INT64_MIN, e->execute("9223372036854775807 + 1").as_int());
                    continue;
                }
                try { e->execute("9223372036854775807 + 1"); }
                catch (const std::exception& ex) { msg[idx] = ex.what(); }
                check_true(msg[idx].find("Integer overflow in '+'") != std::string::npos,
                    "pure-literal INT64_MAX + 1 raises at runtime instead of wrapping at parse time");
                ++idx;
            }
            if (idx == 2) {
                check_eq(msg[0], msg[1], "fold-declined '+' text is byte-identical across backends");
            }
        });
        test("parser_fold_div_min_by_neg1_defers_to_runtime", [this]() {
            auto e = make_engine();
            // Every operand folds to a literal; INT64_MIN / -1 must NOT fold (UB/trap)
            const char* src = "(0 - 9223372036854775807 - 1) / (0 - 1)";
            if (e->throw_on_overflow()) {
                check_throws([&]() { e->execute(src); });
            } else {
                check_eq((int64_t)INT64_MIN, e->execute(src).as_int());
            }
        });

        // ---- INCDEC-WRAPS-SILENTLY: plain ++/-- on int variables/fields now applies
        // the overflow policy like every other int op (both backends, same text as the
        // checked counting-for update). ----
        test("incdec_overflow_checked_identifier", [this]() {
            std::string msg[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                if (!e->throw_on_overflow()) {
                    check_eq((int64_t)INT64_MIN, e->execute("var a = 9223372036854775807; a++; a;").as_int());
                    continue;
                }
                try { e->execute("var a = 9223372036854775807; a++;"); }
                catch (const std::exception& ex) { msg[idx] = ex.what(); }
                check_true(msg[idx].find("Integer overflow in '++'") != std::string::npos,
                    "a++ at INT64_MAX raises and names '++'");
                ++idx;
            }
            if (idx == 2) {
                check_eq(msg[0], msg[1], "'++' overflow text is byte-identical across backends");
            }
        });
        test("incdec_overflow_checked_prefix_decrement", [this]() {
            auto e = make_engine();
            const char* src = "var a = -9223372036854775807 - 1; --a;";
            if (e->throw_on_overflow()) {
                bool threw = false;
                std::string msg;
                try { e->execute(src); } catch (const std::exception& ex) { threw = true; msg = ex.what(); }
                check_true(threw);
                check_true(msg.find("Integer overflow in '--'") != std::string::npos, "--a at INT64_MIN names '--'");
            } else {
                check_eq((int64_t)INT64_MAX, e->execute("var a = -9223372036854775807 - 1; --a; a;").as_int());
            }
        });
        test("incdec_overflow_checked_this_field", [this]() {
            auto e = make_engine();
            const char* src =
                "class C { int f = 9223372036854775807; void bump() { f++; } }"
                "auto c = C(); c.bump();";
            if (e->throw_on_overflow()) {
                bool threw = false;
                std::string msg;
                try { e->execute(src); } catch (const std::exception& ex) { threw = true; msg = ex.what(); }
                check_true(threw);
                check_true(msg.find("Integer overflow in '++'") != std::string::npos, "this-field f++ names '++'");
            } else {
                auto r = e->execute(
                    "class C { int f = 9223372036854775807; void bump() { f++; } }"
                    "auto c = C(); c.bump(); c.f;");
                check_eq((int64_t)INT64_MIN, r.as_int());
            }
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

        // ---- fuzz FZ-TYPED-FN-NO-RETURN (seeds 66/239/2529/3657/3784): ++/-- on a
        // slot-based function local resolved only through the environment, so the
        // interpreter raised a bogus "Undefined variable '<local>'" (VM ran fine) ----
        test("fuzz_incdec_slot_local", [this]() {
            auto e = make_engine();
            check_eq((int64_t)1, e->execute(R"(
                function f() { int g = 0; ++g; return g; }
                f();
            )").as_int());
        });
        test("fuzz_incdec_slot_param_in_while", [this]() {
            auto e = make_engine();
            check_eq((int64_t)4, e->execute(R"(
                function f(int g) { while (++g < 4) {} return g; }
                f(0);
            )").as_int());
        });
        test("fuzz_incdec_postfix_slot_local", [this]() {
            auto e = make_engine();
            check_eq((int64_t)5, e->execute(R"(
                function f() { int g = 0; while (g++ < 4) {} return g; }
                f();
            )").as_int());
        });
        test("fuzz_typed_fn_falls_off_end_returns_null", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                function f(int p) -> string { int g = 0; while (++g < 4) {} }
                f(1);
            )");
            check_true(r.is_null(), "typed fn falling off the end yields null (vm parity)");
            check_eq((int64_t)2, e->execute("1 + 1").as_int());
        });

        // ---- fuzz FZ-NEG-ZERO (seeds 1985/2148/2255): the interpreter's cached-zero
        // float fast path matched -0.0 (IEEE -0.0 == 0.0) and handed back +0.0 ----
        test("fuzz_neg_zero_preserved", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            check_eq(std::string("-0.000000"), e->execute(R"(
                double x = 0.0;
                to_string(-x);
            )").as_string());
            check_eq(std::string("-0.000000"), e->execute("to_string(-(0.0))").as_string());
            check_eq(std::string("-0.000000"), e->execute("to_string(0.0 * -1.0)").as_string());
        });
        test("fuzz_neg_zero_map_key_ordering", [this]() {
            // strong_order keys: -0.0 and +0.0 stay DISTINCT map keys on both backends
            auto e = make_engine();
            check_eq((int64_t)2, e->execute(R"(
                double z = 0.0;
                var m = {};
                m[-z] = "neg";
                m[z] = "pos";
                m.size();
            )").as_int());
        });

        // ---- fuzz FZ-OVERFLOW-OP-NAME (seed 2706): the counting-for update overflow
        // named the wrong operator, differently per backend (interp let i++ wrap and
        // blamed a later '*'; vm blamed '+='). The update now applies the overflow
        // policy on both backends and names the SOURCE operator. Found alongside:
        // the interpreter fast path negated literal '-=' steps twice, so
        // `for (...; i -= 2)` counted UP forever. ----
        test("fuzz_cfor_minus_eq_literal_descends", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            check_eq(std::string("4;2;"), e->execute(R"(
                var out = "";
                for (int i = 4; i > 0; i -= 2) { out += to_string(i) + ";"; }
                out;
            )").as_string());
        });
        test("fuzz_cfor_update_overflow_names_plusplus", [this]() {
            std::string msg[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                if (!e->throw_on_overflow()) { return; }  // wrap build: loop exits via wrap
                try { e->execute("for (var i = 9223372036854775806; i > 0; i++) { }"); }
                catch (const std::exception& ex) { msg[idx] = ex.what(); }
                ++idx;
            }
            check_eq(msg[0], msg[1], "cfor ++ overflow text is byte-identical across backends");
            check_true(msg[0].find("Integer overflow in '++'") != std::string::npos,
                "cfor ++ overflow names the increment (got: " + msg[0] + ")");
        });
        test("fuzz_cfor_update_overflow_names_minus_eq", [this]() {
            std::string msg[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                if (!e->throw_on_overflow()) { return; }
                try { e->execute("for (var i = -9223372036854775807; i < 0; i -= 2) { }"); }
                catch (const std::exception& ex) { msg[idx] = ex.what(); }
                ++idx;
            }
            check_eq(msg[0], msg[1], "cfor -= overflow text is byte-identical across backends");
            check_true(msg[0].find("Integer overflow in '-='") != std::string::npos,
                "cfor -= overflow names the compound step (got: " + msg[0] + ")");
        });

        // ---- fuzz FZ-FLOAT-DIVZERO-TEXT (seed 201): float zero-divisor on the
        // literal/identifier shapes fell to the interpreter's generic handler (bare
        // "Division by zero") while the vm's fused op raised the informative text ----
        test("fuzz_float_divzero_text_parity", [this]() {
            auto expect_both = [&](const char* src, const std::string& needle, const std::string& what) {
                std::string msg[2];
                int idx = 0;
                for (bool use_vm : {false, true}) {
                    auto e = jai::engine::make();
                    if (use_vm) { e->set_backend(jai::backend_type::vm); }
                    try { e->execute(src); }
                    catch (const std::exception& ex) { msg[idx] = ex.what(); }
                    ++idx;
                }
                check_eq(msg[0], msg[1], what + " error text is byte-identical across backends");
                check_true(msg[0].find(needle) != std::string::npos,
                    what + " keeps the informative text (got: " + msg[0] + ")");
            };
            expect_both("var z = 0.0; 0.5 / z;", "Division by zero in float operation", "literal/var float divide");
            expect_both("var z = 0.0; 0.5 % z;", "Modulo by zero in float operation", "literal/var float modulo");
            expect_both("var z = 0.5; z % 0.0;", "Modulo by zero in float operation", "var/literal float modulo");
        });

        // ---- fuzz FZ-RECURSION-MSG-TEXT (seed 137): interp hit the native-stack guard
        // ("Native stack exhausted...") while the vm hit the depth cap, whose "{0}" was
        // passed the depth as a SYMBOL id and printed an empty "()" ----
        test("fuzz_recursion_limit_text_parity", [this]() {
            std::string msg[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execution_budget(0);   // deep recursion outruns the default budget in Debug
                try { e->execute("function forever(int n) -> int { return n + forever(n - 1); } forever(3);"); }
                catch (const std::exception& ex) { msg[idx] = ex.what(); }
                ++idx;
            }
            check_eq(msg[0], msg[1], "recursion-limit error text is byte-identical across backends");
            check_true(msg[0].find("Maximum recursion depth (10000) exceeded") != std::string::npos,
                "recursion-limit message carries the depth (got: " + msg[0] + ")");
        });

        // ---- fuzz FZ-UNDEF-VAR-THIS-DECORATION (seeds 34/28): compound-assign to an
        // undefined identifier resolved the TARGET before evaluating the rhs on the
        // interpreter, so `v1 += v1` blamed the lhs while the vm (C++17 order: rhs
        // first) blamed the rhs load. The "(no 'this' in scope)" decoration itself is
        // symmetric on both backends and stays. ----
        test("fuzz_compound_undef_eval_order_parity", [this]() {
            auto expect_both = [&](const char* src, const std::string& needle, const std::string& what) {
                std::string msg[2];
                int idx = 0;
                for (bool use_vm : {false, true}) {
                    auto e = jai::engine::make();
                    if (use_vm) { e->set_backend(jai::backend_type::vm); }
                    try { e->execute(src); }
                    catch (const std::exception& ex) { msg[idx] = ex.what(); }
                    ++idx;
                }
                check_eq(msg[0], msg[1], what + " error text is byte-identical across backends");
                check_true(msg[0].find(needle) != std::string::npos,
                    what + " blames the right name (got: " + msg[0] + ")");
            };
            // rhs load fails first (rhs-first sequencing): plain undefined-variable text
            expect_both("v1 += v1;", "Undefined variable 'v1'", "undef += same undef");
            expect_both("v1 += fn8();", "Undefined variable 'fn8'", "undef += undef call");
            // rhs fine, target undefined: symmetric decorated text
            expect_both("v1 += 1;", "Undefined variable 'v1' (no 'this' in scope)", "undef += literal");
        });
        test("fuzz_compound_undef_rhs_side_effects_run_first", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                var log = "";
                function s() { log += "x"; return 1; }
                try { v1 += s(); } catch (err) { log += "c"; }
                log;
            )");
            check_eq(std::string("xc"), r.as_string());
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

        // ---- 2026-07: builtin methods through references (docs-agent finding) ----
        // String builtins invoked through a ref DECL segfaulted on both backends: the
        // identifier-receiver fast path passed the RAW stored value (a reference wrapper)
        // as `self`, and string builtins read self's storage unchecked. Array/map
        // builtins and ref PARAMS took other paths and worked - this pins the full
        // matrix: {string, array, map} x {ref decl, ref param, range-for auto& element},
        // observer AND mutating (mutation must land in the referenced target).
        test("string_builtins_through_ref_decl", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(e);
            auto r = e->execute(R"(
                auto s = "abc";
                auto& r = s;
                int n = r.length();
                r.to_upper();
                to_string(n) + "|" + s + "|" + r;
            )");
            check_eq(std::string("3|ABC|ABC"), r.as_string());
        });

        test("string_builtins_through_ref_param", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(e);
            auto r = e->execute(R"(
                auto s = "abc";
                function up(var& t) -> int { t.to_upper(); return t.length(); }
                int n = up(s);
                to_string(n) + "|" + s;
            )");
            check_eq(std::string("3|ABC"), r.as_string());
        });

        test("string_builtins_through_range_for_ref", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(e);
            auto r = e->execute(R"(
                var strs = ["ab", "cd"];
                for (auto& s : strs) { s.to_upper(); }
                strs[0] + strs[1];
            )");
            check_eq(std::string("ABCD"), r.as_string());
        });

        test("array_map_builtins_through_ref_decl", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(e);
            auto r = e->execute(R"(
                var arr = [1, 2, 3];
                auto& ra = arr;
                ra.push(4);
                var m = {"k": 1};
                auto& rm = m;
                rm["j"] = 2;
                to_string(ra.size()) + "|" + to_string(arr.size()) + "|" +
                to_string(rm.size()) + "|" + to_string(m.size());
            )");
            check_eq(std::string("4|4|2|2"), r.as_string());
        });

        test("array_map_builtins_through_ref_param", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(e);
            auto r = e->execute(R"(
                function grow(var& a) { a.push(9); }
                function put(var& m) { m["x"] = 5; }
                var arr = [1];
                var mp = {"k": 1};
                grow(arr);
                put(mp);
                to_string(arr.size()) + "|" + to_string(arr[1]) + "|" + to_string(mp.size());
            )");
            check_eq(std::string("2|9|2"), r.as_string());
        });

        // ---- Decl fast path (variable_decl::decl_fast_flags): slot decls of matching
        // scalar payloads store without enforce/clone/homogeneity. These pin the
        // semantics the fast path must preserve on BOTH backends; the mismatch shapes
        // must keep falling through to the full path. ----
        test("decl_fast_typed_conversion_still_applies", [this]() {
            auto e = make_engine();
            check_eq((int64_t)4, e->execute("auto f() { int d = 4.7; return d; } f();").as_int());
            check_near(5.0, e->execute("auto g() { float x = 5; return x; } g();").as_float(), 1e-9);
        });
        test("decl_fast_typed_mismatch_still_errors", [this]() {
            auto e = make_engine();
            check_throws([&]() { e->execute(R"(auto f() { int x = "s"; return x; } f();)"); });
        });
        test("decl_fast_var_stays_dynamic", [this]() {
            auto e = make_engine();
            check_eq(std::string("str"), e->execute(R"(auto f() { var v = 5; v = "str"; return v; } f();)").as_string());
            // var decl of a scalar must be tagged 'any', not the payload type
            check_near(6.5, e->execute("auto g() { var q = 5; q = 6.5; return q; } g();").as_float(), 1e-9);
        });
        test("decl_fast_auto_locks_inferred_type", [this]() {
            auto e = make_engine();
            check_eq((int64_t)6, e->execute("auto f() { auto a = 5; a = 6; return a; } f();").as_int());
            check_throws([&]() { e->execute(R"(auto f() { auto a = 5; a = "s"; return a; } f();)"); });
        });
        test("decl_fast_element_init_value_copies", [this]() {
            auto e = make_engine();
            // element read arrives as a reference wrapper: the decl must store the
            // VALUE (fast path bails, full path derefs + clones), never alias
            auto r = e->execute(R"(
                auto f() {
                    var arr = [1, 2, 3];
                    int total = 0;
                    for (int i = 0; i < 3; ++i) {
                        int e = arr[i];
                        e = e + 90;
                        total += e;
                    }
                    return arr[0] + arr[1] + arr[2] + total;
                }
                f();
            )");
            check_eq((int64_t)282, r.as_int());   // elements unchanged (6) + 91+92+93
        });
        test("decl_fast_typed_from_local_no_alias", [this]() {
            auto e = make_engine();
            check_eq((int64_t)57, e->execute("auto f() { int a = 5; int b = a; b = 7; return a * 10 + b; } f();").as_int());
        });
        test("decl_fast_loop_decls", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                auto f() {
                    int total = 0;
                    for (int i = 0; i < 100; ++i) {
                        int x = i + 1;
                        var y = x * 2;
                        total += y;
                    }
                    return total;
                }
                f();
            )");
            check_eq((int64_t)10100, r.as_int());
        });
        test("decl_fast_float_bool_char_decls", [this]() {
            auto e = make_engine();
            check_near(3.0, e->execute(R"(
                auto f() {
                    float acc = 0.0;
                    for (int i = 0; i < 4; ++i) {
                        float h = i * 0.5;
                        acc += h;
                    }
                    return acc;
                }
                f();
            )").as_float(), 1e-9);
            // bool from comparison takes the fast path; bool from int still converts (truthy)
            check_eq((int64_t)3, e->execute(R"(
                auto g() {
                    bool b = 1 == 1;
                    bool c = 5;
                    int r = 0;
                    if (b) { r += 1; }
                    if (c) { r += 2; }
                    return r;
                }
                g();
            )").as_int());
            check(e->execute("auto h() { char c = 'x'; return c == 'x'; } h();").as_bool());
            check_eq((int64_t)1, e->execute("auto k() { int a = true; return a; } k();").as_int());
        });
        test("decl_fast_shared_weak_decls_unaffected", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                class P { int v = 1; }
                auto f() {
                    shared_ptr<P> a = P();
                    var b = a;
                    b.v = 7;
                    weak_ptr<P> w = a;
                    int alive = 0;
                    if (!w.expired()) { alive = 1; }
                    return a.v * 10 + alive;
                }
                f();
            )");
            check_eq((int64_t)71, r.as_int());
        });
        test("decl_fast_ref_escaping_decl_still_boxed", [this]() {
            auto e = make_engine();
            // x is escape-marked (bare-identifier ref-param argument): the decl must
            // still box into a cell so the callee's ref writes land in x - the decl
            // fast path must bail on ref_escaping. (NOTE: `auto& r = <plain local>` and
            // slot-local [&] captures are PRE-EXISTING gaps on the pre-fan-out engine -
            // "Cannot take reference of undefined variable" / silent value capture, the
            // latter a documented parked stage-D item - so the escape pin uses the
            // ref-param shape, the one escape route that works for slot locals today.)
            check_eq((int64_t)42, e->execute(
                "function set42(int& t) { t = 42; } function f() -> int { int x = 1; set42(x); return x; } f();").as_int());
        });
    }
};

// Reference-capture semantics (Dev ruling 2026-07-09: C++ lambda semantics ARE the
// contract - "[&] needs to work or all ref captures fail"). These were silently broken
// for SLOT-resident function locals (pre-cells "capture by value, by-ref is UAF"
// compromises that the cell model obsoleted): [&]/[&x] captured copies, and
// `auto& r = <local>` errored "undefined variable" (escape marker boxes the SLOT, the
// binder searched only the env).
class ref_capture_semantics_tests : public suite {
public:
    ref_capture_semantics_tests() : suite("Ref Capture Semantics") {}

    void forge_tests() override {
        test("default_by_ref_capture_writes_through", [this]() {
            auto e = make_engine();
            check_eq((int64_t)42, e->execute(
                "function f() -> int { int x = 1; auto g = [&]() { x = 42; }; g(); return x; } f();").as_int());
        });

        test("explicit_by_ref_capture_writes_through", [this]() {
            auto e = make_engine();
            check_eq((int64_t)42, e->execute(
                "function f() -> int { int x = 1; auto g = [&x]() { x = 42; }; g(); return x; } f();").as_int());
        });

        test("by_value_capture_still_copies", [this]() {
            auto e = make_engine();
            check_eq((int64_t)1, e->execute(
                "function f() -> int { int x = 1; auto g = [=]() { x = 42; }; g(); return x; } f();").as_int());
            check_eq((int64_t)1, e->execute(
                "function f2() -> int { int x = 1; auto g = [x]() { x = 42; }; g(); return x; } f2();").as_int());
        });

        test("mixed_capture_modes", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                function f() -> int {
                    int a = 1;
                    int b = 10;
                    auto g = [=, &a]() { a = 7; b = 70; };
                    g();
                    return a * 100 + b;   // a written through, b untouched
                }
                f();
            )");
            check_eq((int64_t)710, r.as_int());
        });

        test("ref_capture_reads_live_value", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                function f() -> int {
                    int x = 5;
                    auto g = [&]() -> int { return x; };
                    x = 9;
                    return g();   // by-ref sees the CURRENT value, not creation-time
                }
                f();
            )");
            check_eq((int64_t)9, r.as_int());
        });

        test("escaping_ref_capture_outlives_frame", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                function make() -> auto {
                    int n = 0;
                    return [&]() -> int { n = n + 1; return n; };
                }
                auto counter = make();
                counter();
                counter();
                counter();
            )");
            check_eq((int64_t)3, r.as_int());
        });

        test("two_closures_share_one_ref_capture", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                function f() -> int {
                    int shared = 0;
                    auto inc = [&]() { shared = shared + 1; };
                    auto dec = [&]() { shared = shared - 10; };
                    inc(); inc(); dec();
                    return shared;
                }
                f();
            )");
            check_eq((int64_t)-8, r.as_int());
        });

        test("ref_decl_of_plain_local_binds", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                function f() -> int {
                    int x = 1;
                    auto& r = x;
                    r = 42;
                    int seen = x;
                    x = 7;
                    return seen * 100 + r;   // alias wrote 42 into x; x's write shows through r
                }
                f();
            )");
            check_eq((int64_t)4207, r.as_int());
        });

        test("typed_ref_decl_of_local_converts_through", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                function f() -> int {
                    int x = 1;
                    auto& r = x;
                    r = 6.9;         // store through the alias into an int-declared cell
                    return x;
                }
                f();
            )");
            check_eq((int64_t)6, r.as_int());
        });
    }
};

// Element-read mint elision (docs/element_read_overhead_design.md Stage 1): subscript
// reads consumed as transient values push a shallow copy instead of a reference_holder.
// These pin the semantics the elision must preserve on BOTH backends.
class element_read_elision_tests : public suite {
public:
    element_read_elision_tests() : suite("Element Read Elision") {}

    void forge_tests() override {
        test("subscript_operands_value_ops", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            auto r = e->execute(R"(
                array<int> a = [10, 20, 30, 40];
                auto s = a[0] + a[1] * a[2] - a[3];
                auto c = a[1] < a[2];
                auto b = (a[0] & a[1]) | (a[2] >> 1);
                to_string(s) + "|" + to_string(c) + "|" + to_string(b);
            )");
            check_eq(std::string("570|true|15"), r.as_string());
        });

        test("subscript_chain_2d_operand", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                var grid = [[1, 2], [3, 4]];
                grid[1][0] * 10 + grid[0][1];
            )");
            check_eq((int64_t)32, r.as_int());
        });

        test("subscript_in_conditions", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                array<int> a = [0, 5, 7];
                auto n = 0;
                if (a[1]) { n = n + 1; }
                while (a[2] > n) { n = n + 1; }
                auto t = a[0] ? 100 : (a[1] && a[2] ? 50 : 0);
                n + t;
            )");
            check_eq((int64_t)57, r.as_int());
        });

        test("subscript_assignment_rhs", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                array<int> src = [7, 8, 9];
                array<int> dst = [0, 0, 0];
                dst[0] = src[2];
                auto x = src[1];
                x += src[0];
                dst[0] * 100 + x;
            )");
            check_eq((int64_t)915, r.as_int());
        });

        test("map_transient_read_never_inserts", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            auto r = e->execute(R"(
                var m = {"a": 3};
                auto hit = m["a"] + 1;
                auto missing = m["nope"] == null;
                to_string(m.size()) + "|" + to_string(hit) + "|" + to_string(missing);
            )");
            check_eq(std::string("1|4|true"), r.as_string());
        });

        // Impure sibling (a call that reallocates the array) keeps the LEFT operand on
        // the reference path - the classifier's callout_free rule. Values must be
        // correct and Debug's 0xDD fill must not trip (no dangling element pointer).
        test("impure_sibling_keeps_mint", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                var a = [1, 2, 3];
                auto grow(var& arr) -> auto { auto i = 0; while (i < 64) { arr.push(0); i = i + 1; } return 5; }
                a[0] + grow(a);
            )");
            check_eq((int64_t)6, r.as_int());
        });

        // Compound element store still writes through (its target op_index/read stays
        // on the reference path) while its subscript RHS is elided.
        test("compound_element_store_with_subscript_rhs", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                array<int> a = [1, 2, 3];
                a[0] += a[2];
                a[1] = a[1] + a[0];
                a[0] * 10 + a[1];
            )");
            check_eq((int64_t)46, r.as_int());
        });

        // Reference decls / ref returns over subscripts are lvalue consumers - the
        // classifier must leave them minting (red-team regression from the memo).
        test("ref_decl_and_write_through_still_bind", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                var arr = [1, 2, 3];
                var& second = arr[1];
                second = 20;
                arr[1] + arr[0];
            )");
            check_eq((int64_t)21, r.as_int());
        });

        // A registered binary-operator override flips the runtime gate: subscript
        // operands must reach the override (via the mint path) and produce its result.
        test("operator_override_disables_elision", [this]() {
            auto e = make_engine();
            e->add_function("<", [](script_int a, script_int b) { return a > b; });   // deliberately inverted
            auto r = e->execute(R"(
                array<int> a = [1, 9];
                a[1] < a[0];
            )");
            check(r.as_bool());   // inverted override: 9 "<" 1 is true
        });

        // Element reference holders are pool-recycled: heavy mint/release churn in a
        // loop (ref-decl binds in a function called repeatedly) must stay correct.
        test("reference_holder_pool_churn", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            auto r = e->execute(R"(
                var data = [0, 0, 0, 0];
                auto bump(var& arr, int i) -> auto { var& cell = arr[i]; cell = cell + 1; return cell; }
                auto total = 0;
                auto n = 0;
                while (n < 400) { total = total + bump(data, n % 4); n = n + 1; }
                to_string(total) + "|" + to_string(data[0]) + "|" + to_string(data[3]);
            )");
            check_eq(std::string("20200|100|100"), r.as_string());
        });

        // Class operator methods deliberately do NOT flip the runtime gate (that would
        // kill the elision for any host binding operator types): subscript operands
        // feeding a class operator arrive as evaluation-time copies on BOTH backends
        // (the interpreter's historical order). Pin: correct dispatch and values.
        test("class_operator_method_operands_elided_correctly", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                class Vec {
                    int v = 0;
                    Vec(int n) { v = n; }
                    function operator+(Vec o) -> Vec { return Vec(v + o.v); }
                }
                var arr = [Vec(3), Vec(4)];
                var plain = [10, 20];
                auto combined = arr[0] + arr[1];        // object elements -> class operator+
                auto untouched = plain[0] + plain[1];   // int elements -> builtin, still elided
                combined.v * 100 + untouched;
            )");
            check_eq((int64_t)730, r.as_int());
        });

        // Cross-engine values: engine B minting a reference to an engine-A value must not
        // stamp B's interned reference twin onto A's type_info (dangles when B dies).
        test("cross_engine_reference_no_foreign_twin_stamp", [this]() {
            auto a = make_engine();
            a->execute("var arr = [1, 2, 3];");
            script_value shared_arr = a->get_variable("arr");
            {
                auto b = make_engine();
                b->add_global("borrowed", shared_arr);
                b->execute("var& r = borrowed[0]; r = 9;");
            }
            // B is gone; A minting references for the same element type must not
            // resolve through a pointer B cached onto A's type_info.
            auto r = a->execute("var& s = arr[1]; s = 5; arr[0] + arr[1];");
            check_eq((int64_t)14, r.as_int());
        });

        // ---- Stage 2: fused subscript stores + flat operator table ----

        // Fused a[i]=v / a[i]op=v fast paths and their slow replays must keep exact
        // semantics: typed conversion, string-element concat, map auto-insert, nested
        // chains, and rvalue-target errors.
        test("fused_store_semantics", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            auto r = e->execute(R"(
                array<int> ints = [1, 2, 3];
                ints[0] = 4.7;                 // typed element: converts like assignment
                ints[1] += 2;
                ints[2] *= 3;
                var strs = ["a", "b"];
                strs[0] += "x";                // string element: slow-replay concat
                var m = {"k": 10};
                m["k"] += 5;                   // map target: slow-replay through the entry ref
                m["new"] = 1;                  // auto-insert
                var grid = [[1, 2], [3, 4]];
                grid[1][0] = grid[0][1] + 40;  // nested chain target + elided RHS
                to_string(ints[0]) + "|" + to_string(ints[1]) + "|" + to_string(ints[2]) + "|" +
                    strs[0] + "|" + to_string(m["k"]) + "|" + to_string(m.size()) + "|" + to_string(grid[1][0]);
            )");
            check_eq(std::string("4|4|9|ax|15|2|42"), r.as_string());
        });

        test("fused_store_error_paths", [this]() {
            auto e = make_engine();
            // Typed element mismatch keeps its error
            check_throws([&]() { e->execute(R"(array<int> a = [1]; a[0] = "nope";)"); });
            // Compound divide by zero keeps its error
            check_throws([&]() { e->execute(R"(var a = [4]; a[0] /= 0;)"); });
            // Rvalue base (function return) stays a store error
            check_throws([&]() { e->execute(R"(auto f() -> auto { return [1, 2]; } f()[0] = 5;)"); });
        });

        // A registered global operator override must reach subscript compound stores
        // through the flat table (no environment probe on the path anymore).
        test("operator_table_dispatches_compound_override", [this]() {
            auto e = make_engine();
            e->add_function("+", [](script_int a, script_int b) { return a * b; });   // deliberately multiplies
            auto r = e->execute(R"(
                var a = [3];
                a[0] += 5;
                a[0];
            )");
            check_eq((int64_t)15, r.as_int());
        });

        // ---- Stage 3: fused subscript-read operands + interp subscript-compound ----

        // a[ident] / a[literal] as binary operands (both sides, conditions, compound RHS)
        // and the interpreter's in-place numeric subscript-compound must keep exact
        // values across int/float/mixed, OOB errors, map bases, and impure RHS bails.
        test("fused_subscript_operand_semantics", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            auto r = e->execute(R"(
                array<int> a = [10, 20, 30, 40];
                var f = [0.5, 2.5];
                int i = 1;
                int j = 3;
                auto s1 = a[i] + a[j];         // ident-index both sides
                auto s2 = a[0] * a[i];         // literal + ident
                auto s3 = a[i] < a[j];         // fused cmp bail path
                auto s4 = f[0] + a[i];         // float element mixed
                auto total = 0;
                int k = 0;
                while (a[k] < 35) { total += a[k]; k += 1; }   // condition + compound RHS
                to_string(s1) + "|" + to_string(s2) + "|" + to_string(s3) + "|" +
                    to_string(s4) + "|" + to_string(total);
            )");
            check_eq(std::string("60|200|true|20.500000|60"), r.as_string());
        });

        test("fused_subscript_operand_errors_and_maps", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            // OOB through the fused operand keeps the exact op_index error
            check_throws([&]() { e->execute("var a = [1]; int i = 5; a[i] + 1;"); });
            // Map base takes the replay path: never inserts, missing key = null
            auto r = e->execute(R"(
                var m = {"x": 7};
                auto hit = m["x"] + 1;
                to_string(m.size()) + "|" + to_string(hit);
            )");
            check_eq(std::string("1|8"), r.as_string());
        });

        // N-level chains as fused operands: array levels step in place; a MAP level
        // mid-chain replays that level (never-insert) and the walk continues.
        test("fused_subscript_chain_n_level", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            auto r = e->execute(R"(
                var grid = [[1, 2], [3, 4]];
                var cube = [[[10, 20], [30, 40]], [[50, 60], [70, 80]]];
                var deep = [[[[5, 6], [7, 8]]]];
                var m = {"rows": [[100, 200], [300, 400]]};
                int y = 1;
                int x = 0;
                int z = 1;
                auto g = grid[y][x] + grid[0][1];          // 2-level both sides
                auto c = cube[z][y][x] * 2;                // 3-level
                auto d = deep[0][0][y][x] + 1;             // 4-level
                auto mm = m["rows"][y][x] + grid[y][x];    // map level mid-chain + array levels
                auto cmp = grid[y][x] < cube[z][y][x];     // chains in a fused comparison
                to_string(g) + "|" + to_string(c) + "|" + to_string(d) + "|" +
                    to_string(mm) + "|" + to_string(cmp);
            )");
            check_eq(std::string("5|140|8|303|true"), r.as_string());
        });

        test("fused_subscript_chain_errors", [this]() {
            auto e = make_engine();
            // OOB at an INNER level keeps the exact op_index error
            check_throws([&]() { e->execute("var g = [[1]]; int i = 9; g[i][0] + 1;"); });
            // OOB at the LAST level too
            check_throws([&]() { e->execute("var g = [[1]]; int j = 9; g[0][j] + 1;"); });
        });

        test("interp_subscript_compound_inplace", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            auto r = e->execute(R"(
                array<int> pix = [0, 0, 0];
                var fl = [1.5, 2.5];
                int gi = 1;
                pix[gi] += 41;                  // ident index, int
                pix[0] += pix[gi] + 1;          // subscript RHS (callout-free)
                fl[1] *= 2.0;                   // float in place
                var arr2 = [10];
                auto bump() -> auto { arr2.push(99); return 5; }
                arr2[0] += bump();              // impure RHS: general path (ref re-resolve)
                to_string(pix[0]) + "|" + to_string(pix[1]) + "|" + to_string(fl[1]) + "|" +
                    to_string(arr2[0]) + "|" + to_string(arr2.size());
            )");
            check_eq(std::string("42|41|5.000000|15|2"), r.as_string());
        });

        // A pooled holder escaping the engine must stay destructible after engine death
        // (orphaned pool: the last release frees it, no crash / no touch of freed engine).
        // A by-ref lambda capture boxes x into a CELL reference_holder held by the
        // closure env, so copying the function value out keeps a pooled block alive.
        test("pool_orphan_reference_outlives_engine", [this]() {
            std::optional<script_value> escaped;
            {
                auto e = make_engine();
                e->execute("var x = 5; auto f = [&]() { return x + 1; };");
                escaped.emplace(e->get_variable("f"));
                check(escaped->is_function());
            }
            escaped.reset();   // release after the engine is gone
            check_true(true);
        });
    }
};

// math:: language intrinsics (detail/math_intrinsics.hpp): recognized as call shapes by
// both backends, evaluated through ONE shared kernel - values, errors, determinism, and
// coexistence with user code all pinned here.
class math_intrinsics_tests : public suite {
public:
    math_intrinsics_tests() : suite("Math Intrinsics") {}

    void forge_tests() override {
        test("numeric_values", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            auto r = e->execute(R"(
                to_string(math::floor(2.7)) + "|" + to_string(math::ceil(2.1)) + "|" +
                to_string(math::itrunc(3.9)) + "|" + to_string(math::ifloor(-1.5)) + "|" +
                to_string(math::sqrt(16.0)) + "|" + to_string(math::pow(2.0, 10.0)) + "|" +
                to_string(math::abs(-7)) + "|" + to_string(math::min(3, 9)) + "|" +
                to_string(math::max(3, 9)) + "|" + to_string(math::clamp(15, 0, 10));
            )");
            check_eq(std::string("2.000000|3.000000|3|-2|4.000000|1024.000000|7|3|9|10"), r.as_string());
        });

        test("trig_and_distance", [this]() {
            auto e = make_engine();
            check_near(0.0, e->execute("math::sin(0.0)").as_float(), 1e-12);
            check_near(1.0, e->execute("math::cos(0.0)").as_float(), 1e-12);
            check_near(5.0, e->execute("math::distance(0.0, 0.0, 3.0, 4.0)").as_float(), 1e-12);
            check_near(25.0, e->execute("math::distance_squared(0.0, 0.0, 3.0, 4.0)").as_float(), 1e-12);
            check_near(180.0, e->execute("math::radians_to_degrees(3.14159265358979323846)").as_float(), 1e-9);
            check_near(3.14159265358979323846, e->execute("math::degrees_to_radians(180.0)").as_float(), 1e-12);
        });

        // The mix family must be VALUE-IDENTICAL to the stdlib registrations it
        // supersedes (scripts migrate without hash drift); lerp == mix by ruling.
        test("mix_family_matches_stdlib", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            auto r = e->execute(R"(
                auto ok = math::mix(2.0, 10.0, 0.25) == mix(2.0, 10.0, 0.25) &&
                          math::lerp(2.0, 10.0, 0.25) == math::mix(2.0, 10.0, 0.25) &&
                          math::unmix(2.0, 10.0, 4.0) == unmix(2.0, 10.0, 4.0) &&
                          math::mix_in(0.0, 1.0, 0.3, 2.0) == mix_in(0.0, 1.0, 0.3, 2.0) &&
                          math::mix_out(0.0, 1.0, 0.3, 2.0) == mix_out(0.0, 1.0, 0.3, 2.0) &&
                          math::mix_in_out(0.0, 1.0, 0.7, 2.0) == mix_in_out(0.0, 1.0, 0.7, 2.0) &&
                          math::mix_out_in(0.0, 1.0, 0.7, 2.0) == mix_out_in(0.0, 1.0, 0.7, 2.0) &&
                          math::unmix_in(0.0, 1.0, 0.3, 2.0) == unmix_in(0.0, 1.0, 0.3, 2.0) &&
                          math::unmix_out(0.0, 1.0, 0.3, 2.0) == unmix_out(0.0, 1.0, 0.3, 2.0) &&
                          math::unmix_in_out(0.0, 1.0, 0.7, 2.0) == unmix_in_out(0.0, 1.0, 0.7, 2.0) &&
                          math::unmix_out_in(0.0, 1.0, 0.7, 2.0) == unmix_out_in(0.0, 1.0, 0.7, 2.0);
                ok;
            )");
            check(r.as_bool());
        });

        test("random_determinism_and_ranges", [this]() {
            auto e = make_engine();
            // Same seed -> exact same sequence (mt19937_64 is standard-specified)
            auto r = e->execute(R"(
                math::random_seed(1234);
                auto a1 = math::random();
                auto a2 = math::random(0, 100);
                auto a3 = math::random(-2.5, 2.5);
                math::random_seed(1234);
                auto b1 = math::random();
                auto b2 = math::random(0, 100);
                auto b3 = math::random(-2.5, 2.5);
                a1 == b1 && a2 == b2 && a3 == b3;
            )");
            check(r.as_bool());
            // Inclusive endpoints: degenerate range returns its endpoint; unit range in [0,1]
            check_eq((int64_t)7, e->execute("math::random(7, 7)").as_int());
            auto bounds = e->execute(R"(
                math::random_seed(42);
                auto ok = true;
                for (int i = 0; i < 200; ++i) {
                    auto u = math::random();
                    if (u < 0.0 || u > 1.0) { ok = false; }
                    auto n = math::random(1, 6);
                    if (n < 1 || n > 6) { ok = false; }
                    auto f = math::random(-1.0, 1.0);
                    if (f < -1.0 || f > 1.0) { ok = false; }
                }
                ok;
            )");
            check(bounds.as_bool());
        });

        test("intrinsic_errors", [this]() {
            auto e = make_engine();
            check_throws([&]() { e->execute("math::floor(\"nope\");"); });          // type
            check_throws([&]() { e->execute("math::floor(1.0, 2.0);"); });          // arity
            check_throws([&]() { e->execute("math::random(5, 1);"); });             // inverted range
            // Engine stays usable after an intrinsic error
            check_eq((int64_t)3, e->execute("1 + 2").as_int());
        });

        // Intrinsics coexist with user code: other namespaces, operator overloads on
        // types, and the still-registered bare stdlib names are all untouched.
        test("coexists_with_user_code", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            auto r = e->execute(R"(
                namespace geo { float area(float w, float h) { return w * h; } }
                class Vec {
                    float x = 0.0; float y = 0.0;
                    Vec(float px, float py) { x = px; y = py; }
                    function operator+(Vec o) -> Vec { return Vec(x + o.x, y + o.y); }
                }
                auto v = Vec(1.0, 2.0) + Vec(3.0, 4.0);
                auto bare = floor(2.9);
                to_string(geo::area(3.0, 4.0)) + "|" + to_string(v.x + v.y) + "|" +
                    to_string(bare) + "|" + to_string(math::floor(2.9));
            )");
            check_eq(std::string("12.000000|10.000000|2.000000|2.000000"), r.as_string());
        });

        // Parallel bodies admit math:: intrinsics (pure value functions; admission-time
        // resolution doubles as the AST-cache prewarm) but reject the random trio
        // (engine-owned rng = worker race).
        test("intrinsics_in_parallel_bodies", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            auto r = e->execute(R"(
                var xs = [1.44, 4.0, 9.0, 20.25];
                int body(float x) { return math::itrunc(math::sqrt(x) * 10.0); }
                var out = parallel_transform(xs, body);
                to_string(out[0]) + "|" + to_string(out[1]) + "|" + to_string(out[2]) + "|" + to_string(out[3]);
            )");
            check_eq(std::string("12|20|30|45"), r.as_string());
            check_throws([&]() {
                e->execute(R"(
                    var ys = [1.0, 2.0];
                    float rbody(float y) { return y + math::random(); }
                    parallel_transform(ys, rbody);
                )");
            });
        });

        // Intrinsics inside hot shapes: loops, method bodies, nested in expressions
        test("intrinsics_in_expressions", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                auto total = 0;
                for (int i = 0; i < 10; ++i) {
                    total += math::itrunc(math::sqrt(100.0) + i);
                }
                total;
            )");
            check_eq((int64_t)145, r.as_int());
        });
    }
};

// Typed-slot store proof (assignment_expr::typed_store_provable + store_flag_type_provable):
// provably-matching int/float stores skip enforcement in both backends; everything these
// tests pin is the OLD behavior that must survive the skip.
class typed_store_proof_tests : public suite {
public:
    typed_store_proof_tests() : suite("Typed Store Proof") {}

    void forge_tests() override {
        // Provable store must not strip the slot's type lock: the float store AFTER the
        // provable int store still converts.
        test("provable_store_keeps_type_lock", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                auto f() -> auto {
                    int x = 0;
                    x = 5;
                    x = 4.7;
                    return x;
                }
                f();
            )");
            check_eq((int64_t)4, r.as_int());
        });
        test("float_slot_int_rhs_still_converts", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                auto f() -> auto {
                    float v = 0.0;
                    v = 2.5;
                    v = 3;
                    return v / 2;
                }
                f();
            )");
            check_near(1.5, r.as_float(), 0.0001);   // 3 stored as float, / stays float
        });
        test("typed_mismatch_still_errors", [this]() {
            auto e = make_engine();
            check_throws([&]() {
                e->execute(R"(
                    auto f() -> auto { int x = 0; x = 5; x = "nope"; return x; }
                    f();
                )");
            });
        });
        test("provable_int_loop_correct", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                auto f() -> auto {
                    int total = 0;
                    for (int i = 0; i < 100; i = i + 1) { total = total + i; }
                    return total;
                }
                f();
            )");
            check_eq((int64_t)4950, r.as_int());
        });
        test("provable_float_loop_correct", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                auto f() -> auto {
                    float acc = 0.0;
                    for (int i = 0; i < 10; i = i + 1) { acc = acc + 0.5; }
                    return acc;
                }
                f();
            )");
            check_near(5.0, r.as_float(), 0.0001);
        });
        // Mixed int/float RHS is NOT provable: the truncating conversion still runs
        // per iteration (x = trunc(x + 1.5) advances by exactly 1).
        test("unprovable_mixed_rhs_converts_each_store", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                auto f() -> auto {
                    int x = 0;
                    for (int i = 0; i < 10; i = i + 1) { x = x + 1.5; }
                    return x;
                }
                f();
            )");
            check_eq((int64_t)10, r.as_int());
        });
        // Comparison RHS is bool, never provable for an int slot: bool->int conversion holds.
        test("bool_rhs_still_converts", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                auto f() -> auto { int x = 0; x = (1 < 2); return x; }
                f();
            )");
            check_eq((int64_t)1, r.as_int());
        });
        // var/auto declarations never prove: var stays dynamic after an int store,
        // auto stays locked to its inferred type.
        test("var_stays_dynamic_after_int_store", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                auto f() -> auto { var v = 0; v = 1; v = "s"; return v; }
                f();
            )");
            check_eq(std::string("s"), r.as_string());
        });
        test("auto_stays_locked_after_int_store", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                auto f() -> auto { auto a = 1; a = 2; a = 3.9; return a; }
                f();
            )");
            check_eq((int64_t)3, r.as_int());
        });
        // Ref-alias stores (store_flag_ref_alias) keep store-through; provable stores
        // through the escape-boxed cell stay alias-visible and keep the type lock.
        test("ref_alias_and_cell_stores_unaffected", [this]() {
            auto e = make_engine();
            // Ref-PARAM alias (the working escape shape; `auto& r = <plain local>` is a
            // pre-existing engine gap): typed stores through the alias and through the
            // origin must interleave correctly, incl. the float->int conversion.
            auto r = e->execute(R"(
                function poke(int& t) -> int {
                    t = 5;
                    t = 6.9;
                    return t;
                }
                auto f() -> auto {
                    int x = 1;
                    auto seen = poke(x);
                    x = 42;
                    return seen * 100 + x;
                }
                f();
            )");
            check_eq((int64_t)642, r.as_int());   // alias saw the converted 6; x ends 42
        });
        // §12.1: cpp-bound global targets keep write-through (globals have no slot, so
        // stores to them are never stamped).
        test("cpp_bound_global_write_through_unaffected", [this]() {
            auto e = make_engine();
            int64_t bound = 1;
            e->add_global_ref("bound", bound);
            e->execute(R"(
                auto f() -> auto { bound = 7; return bound; }
                f();
            )");
            check_eq((int64_t)7, bound);
        });
        // Scope exactness: after an inner block's `int x` expires, a same-named store
        // targets the global var - which must stay dynamic (no stolen proof).
        test("expired_block_shadow_never_locks_outer_var", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            auto r = e->execute(R"(
                var x = 0;
                auto f() -> auto { { int x = 1; } x = 3; return 0; }
                f();
                auto after = x;
                x = "s";
                to_string(after) + x;
            )");
            check_eq(std::string("3s"), r.as_string());
        });
        // math:: intrinsic classifications: int-returning and float-returning stores.
        test("math_intrinsic_classified_stores", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                auto f() -> auto {
                    int i = 0;
                    i = math::ifloor(2.9);
                    float g = 0.0;
                    g = math::sqrt(9.0);
                    return i + g;
                }
                f();
            )");
            check_near(5.0, r.as_float(), 0.0001);
        });
        // Compound stores are untouched by the proof: += still converts the promoted result.
        test("compound_store_conversion_unaffected", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                auto f() -> auto { int x = 1; x += 2.5; return x; }
                f();
            )");
            check_eq((int64_t)3, r.as_int());
        });
    }
};

// Flat-value-class stamp (class_definition::flat_value_semantics, parallel_for v1):
// instances of a flat class are alias-free by construction, so parallel workers may
// mutate an owned instance in place. These pin the stamp's shape rules and the
// epoch-based invalidation (a nested field class redefining must invalidate its
// CONTAINING classes' stamps).
class flat_value_class_tests : public suite {
public:
    flat_value_class_tests() : suite("Flat Value Class") {}

    bool flat_of(std::shared_ptr<jai::engine>& e, const char* name) {
        auto cls = e->get_class_registry().find_script_class(name);
        check_not_null(cls.get());
        return cls->flat_value_semantics();
    }

    void forge_tests() override {
        test("all_primitive_fields_flat", [this]() {
            auto e = make_engine();
            e->execute("class P { int a = 0; float b = 1.5; bool c = false; char d = 'x'; }");
            check_true(flat_of(e, "P"));
        });
        test("no_fields_flat", [this]() {
            auto e = make_engine();
            e->execute("class P { int helper(int v) { return v * 2; } }");
            check_true(flat_of(e, "P"));
        });
        // Strings ARE value-closed (Dev ruling: a member string is an engine value
        // type, per-instance; the barrier normalization detaches rare shared nodes)
        test("string_field_flat", [this]() {
            auto e = make_engine();
            e->execute("class P { int a = 0; string s = \"\"; }");
            check_true(flat_of(e, "P"));
        });
        test("var_field_not_flat", [this]() {
            auto e = make_engine();
            e->execute("class P { var v = 0; }");
            check_false(flat_of(e, "P"));
        });
        test("typed_value_container_fields_flat", [this]() {
            auto e = make_engine();
            e->execute("class P { array<int> xs = []; array<string> names = []; int n = 0; }");
            check_true(flat_of(e, "P"));
        });
        test("untyped_container_field_not_flat", [this]() {
            auto e = make_engine();
            e->execute("class P { var xs = []; }");
            check_false(flat_of(e, "P"));
        });
        // Transitive value-closure (Dev ruling): containers of containers, containers
        // of flat classes - anything that cannot point at something else
        test("nested_container_field_flat", [this]() {
            auto e = make_engine();
            e->execute("class P { array<array<int>> grid = []; map<string, array<float>> table = {}; }");
            check_true(flat_of(e, "P"));
        });
        test("nested_flat_class_field_flat", [this]() {
            auto e = make_engine();
            e->execute(R"(
                class Vec2 { int x = 0; int y = 0; }
                class Outer { Vec2 a = Vec2(); array<Vec2> pts = []; int n = 0; }
            )");
            check_true(flat_of(e, "Vec2"));
            check_true(flat_of(e, "Outer"));
        });
        test("nested_var_field_poisons_outer", [this]() {
            auto e = make_engine();
            e->execute(R"(
                class Inner { var anything = 0; }
                class Outer { Inner a = Inner(); }
            )");
            check_false(flat_of(e, "Outer"));
        });
        test("weak_ptr_field_not_flat", [this]() {
            auto e = make_engine();
            e->execute("class P { int a = 0; weak_ptr<P> other = null; }");
            check_false(flat_of(e, "P"));
        });
        test("flat_inheritance_chain_flat", [this]() {
            auto e = make_engine();
            e->execute(R"(
                class Base { int hp = 10; string name = ""; }
                class Kid : Base { float speed = 1.0; }
            )");
            check_true(flat_of(e, "Kid"));
        });
        test("var_parent_poisons_child", [this]() {
            auto e = make_engine();
            e->execute(R"(
                class Base { var payload = 0; }
                class Kid : Base { int hp = 10; }
            )");
            check_false(flat_of(e, "Kid"));
        });
        // THE invalidation case: Outer's cached stamp must fall when Inner hot-reloads
        // to a non-flat shape (per-class epochs cannot see containment; the stamp
        // validates against engine::class_definition_epoch).
        test("nested_redefinition_invalidates_containing_stamp", [this]() {
            auto e = make_engine();
            e->execute(R"(
                class Inner { float x = 0.0; }
                class Outer { Inner a = Inner(); }
            )");
            check_true(flat_of(e, "Outer"));
            e->execute("class Inner { float x = 0.0; var tag = 0; }");
            check_false(flat_of(e, "Outer"));
        });
        test("cpp_class_not_flat", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(e);
            auto r = e->execute("pair(1, 2)");
            auto holder = r.get_object_holder();
            check_not_null(holder.get());
            auto pair_cls = e->get_class_definition(holder->type_id);
            check_not_null(pair_cls.get());
            check_false(pair_cls->flat_value_semantics());
        });
        test("allow_unsafe_parallel_defaults_off", [this]() {
            auto e = make_engine();
            check_false(e->allow_unsafe_parallel());
            e->allow_unsafe_parallel(true);
            check_true(e->allow_unsafe_parallel());
            e->allow_unsafe_parallel(false);
            check_false(e->allow_unsafe_parallel());
        });
    }
};

} // namespace jai::foundry::tests

using review_regression_tests = jai::foundry::tests::review_regression_tests;
FOUNDRY_REGISTER(review_regression_tests)
using typed_store_proof_tests = jai::foundry::tests::typed_store_proof_tests;
FOUNDRY_REGISTER(typed_store_proof_tests)
using math_intrinsics_tests = jai::foundry::tests::math_intrinsics_tests;
FOUNDRY_REGISTER(math_intrinsics_tests)
using element_read_elision_tests = jai::foundry::tests::element_read_elision_tests;
FOUNDRY_REGISTER(element_read_elision_tests)
using ref_capture_semantics_tests = jai::foundry::tests::ref_capture_semantics_tests;
FOUNDRY_REGISTER(ref_capture_semantics_tests)
using flat_value_class_tests = jai::foundry::tests::flat_value_class_tests;
FOUNDRY_REGISTER(flat_value_class_tests)
