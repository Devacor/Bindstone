// Regression tests for bugs found during the 2026-05 deep review.
// Each test documents the finding number from that review and reproduces the
// edge case that previously failed. Pure-script reproducers live here; the
// C++-bound numeric reproducers (cpp_bound float/int reads) live in
// source/tests/semantics/cpp_bound_numeric_tests.cpp.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>
#include <optional>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// A dynamic_binder-bound base for hybrid (script class : C++ class) rows. The
// by-reference rows cover every property kind: box-decodable member pointers
// (CPP-MEMBER), a class-typed member pointer and a lambda pair (PROPERTY), read-only.
struct inner_probe {
    int64_t x = 0;
};

struct hybrid_prop_probe {
    int64_t value = 0;
    std::string name = "n";
    double ratio = 1.5;
    bool flag = false;
    int32_t narrow = 0;
    inner_probe child;
    int64_t lam_backing = 0;
    int64_t lam_reads = 0;
    int64_t lam_writes = 0;
    int64_t doubled() const { return value * 2; }
    void bump_cpp() { value += 100; }
    hybrid_prop_probe& self() { return *this; }
};

inline void bind_hybrid_probe(engine& e) {
    dynamic_binder<inner_probe>(e, "InnerProbe")
        .constructor<>()
        .property("x", &inner_probe::x)
        .build();
    dynamic_binder<hybrid_prop_probe>(e, "HybridProbe")
        .constructor<>()
        .method("doubled", &hybrid_prop_probe::doubled)
        .method("bump_cpp", &hybrid_prop_probe::bump_cpp)
        .method("self", &hybrid_prop_probe::self)
        .property("value", &hybrid_prop_probe::value)
        .property("name", &hybrid_prop_probe::name)
        .property("ratio", &hybrid_prop_probe::ratio)
        .property("flag", &hybrid_prop_probe::flag)
        .property("narrow", &hybrid_prop_probe::narrow)
        .property("child", &hybrid_prop_probe::child)
        .property("lam",
                  [](hybrid_prop_probe& s) -> int64_t { ++s.lam_reads; return s.lam_backing; },
                  [](hybrid_prop_probe& s, int64_t v) { ++s.lam_writes; s.lam_backing = v; })
        .property("ro", [](const hybrid_prop_probe& s) -> int64_t { return s.value + 100; }, nullptr)
        .build();
}

// The C++ object behind a script handle (hybrid layer or plain wrapper)
inline std::shared_ptr<hybrid_prop_probe> probe_of(engine& e, const char* expr) {
    return e.execute(expr).get_class_instance()->get_cpp_object_as<hybrid_prop_probe>();
}

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

        // ---- chained subscript on a temporary across the execute boundary ----
        // The result of [temp][0]["k"] is a map-entry reference holding the LAST pin on
        // the temporary map; the boundary collapse `x = x.deref()` assigned from storage
        // x itself owned, and destroy-then-copy in storage::operator= read freed memory
        // (0xDD under Debug). Pins the pin-before-destroy assignment order, both backends.
        test("chained_subscript_on_temporary_survives_boundary", [this]() {
            auto e = make_engine();
            check_eq(std::string("zz"), e->execute("[{\"name\":\"zz\"}][0][\"name\"]").as<std::string>());
            check_eq((int64_t)7, e->execute("[[7]][0][0]").as_int());
            check_eq(std::string("deep"), e->execute("{\"a\": [{\"b\": \"deep\"}]}[\"a\"][0][\"b\"]").as<std::string>());
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
        // ---- range-for over a NESTED container read (jaidoom edge): for (x : m["a"])
        // failed "not a map" on both backends — the source expression arrives as a
        // reference wrapper and iter-init type-checked the wrapper. Deref'd handles
        // share the live node, so by-ref element mutation still lands in the original. ----
        test("range_for_iterates_nested_container_reads", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);   // map iteration mints pair objects
            auto r = e->execute(R"(
                var m = {"a": {"b": 1, "c": 2}};
                var acc = "";
                for (var kv : m["a"]) { acc += kv.first + "=" + to_string(kv.second) + ";"; }
                var arr = {"list": [10, 20, 30]};
                int sum = 0;
                for (var x : arr["list"]) { sum += x; }
                var nested = [[1, 2], [3, 4]];
                for (var y : nested[1]) { sum += y; }
                for (auto& z : arr["list"]) { z = z * 2; }
                acc + "|" + to_string(sum) + "|" + to_string(arr["list"][0]) + "," + to_string(arr["list"][2]);
            )");
            check_eq(std::string("b=1;c=2;|67|20,60"), r.as_string(),
                "nested reads iterate and by-ref mutation lands in the original");
        });

        // ---- container literals hold VALUES (jaidoom netplay finding): element reads
        // arrive as reference wrappers (rhs-lvalue read shape) and literals must
        // normalize them like assignment does — the un-derefed wrapper made
        // {"x": arr[i]} serialize as null AND aliased the source array. ----
        test("container_literals_normalize_element_reads", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(*e);
            auto r = e->execute(R"(
                array<int> typed = [100, 200, 300];
                var plain = [10, 20];
                var m = {"t": typed[1], "p": plain[0]};
                var a = [typed[0], plain[1]];
                var nested = {"outer": {"inner": typed[2]}};
                typed[1] = -1;              // literals snapshot: mutations must not show through
                plain[0] = -1;
                to_json(m) + "|" + to_json(a) + "|" + to_json(nested);
            )");
            check_eq(std::string("{\"p\":10,\"t\":200}|[100,20]|{\"outer\":{\"inner\":300}}"), r.as_string(),
                "element reads land as numbers and snapshot at literal construction");
        });
        test("int_mod_neg1_is_zero", [this]() {   // policy-independent: a % -1 == 0, no trap
            auto e = make_engine();
            check_eq((int64_t)0, e->execute("auto x = -9223372036854775807 - 1; auto d = -1; x % d").as_int());
        });

        // ---- shift semantics CONTRACT (policy-independent, both overflow builds):
        // bitwise ops including << >> operate on the 64-bit pattern mod 2^64 by DESIGN —
        // bits shifted past bit 63 are discarded, never an overflow error (matching
        // Rust/C#: checked arithmetic excludes bit operations). Only the COUNT is
        // guarded (0..63, catchable runtime error). GLOOM/demoreel/crawler xorshift
        // RNGs and the (0x9E3779B9 << 32) seed idiom depend on every pin here. ----
        test("shift_left_wraps_mod_2_64", [this]() {
            auto e = make_engine();
            check_eq((int64_t)INT64_MIN, e->execute("auto x = 1; auto s = 63; x << s").as_int());
            check_eq((int64_t)INT64_MIN, e->execute("1 << 63").as_int());   // literal spelling too
            check_eq((int64_t)-2, e->execute("auto y = 9223372036854775807; y << 1").as_int());
            auto g = e->execute("auto g = (0x9E3779B9 << 32) | 0x7F4A7C15; g");   // the GLOOM seed idiom
            check_true(g.as_int() < 0, "shift into the sign bit wraps negative, no error");
            check_eq((int64_t)0x9E3779B9, e->execute("auto g = (0x9E3779B9 << 32) | 0x7F4A7C15; (g >> 32) & 0xFFFFFFFF").as_int());
            check_eq((int64_t)0x7F4A7C15, e->execute("auto g = (0x9E3779B9 << 32) | 0x7F4A7C15; g & 0xFFFFFFFF").as_int());
        });
        test("shift_left_vs_mul_contrast", [this]() {   // the checked-overflow boundary, pinned
            auto e = make_engine();
            const char* shl = "auto x = 4611686018427387904; x << 1";   // 2^62 << 1 wraps to INT64_MIN
            const char* mul = "auto x = 4611686018427387904; x * 2";    // same math, ARITHMETIC op
            check_eq((int64_t)INT64_MIN, e->execute(shl).as_int());
            if (e->throw_on_overflow()) {
                check_throws([&]() { e->execute(mul); });
            } else {
                check_eq((int64_t)INT64_MIN, e->execute(mul).as_int());
            }
        });
        test("shift_right_is_arithmetic", [this]() {
            auto e = make_engine();
            check_eq((int64_t)-4, e->execute("auto x = -8; auto s = 1; x >> s").as_int());
            check_eq((int64_t)-1, e->execute("auto x = -9223372036854775807 - 1; x >> 63").as_int());
        });
        test("shift_count_range_errors_text_parity", [this]() {
            std::string msg[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                check_throws([&]() { e->execute("auto x = 1; auto s = 64; x << s;"); });
                check_throws([&]() { e->execute("auto x = 1; auto s = -1; x << s;"); });
                check_throws([&]() { e->execute("auto x = 1; auto s = 64; x >> s;"); });
                try { e->execute("auto x = 1; auto s = 64; x << s;"); }
                catch (const std::exception& ex) { msg[idx] = ex.what(); }
                check_eq((int64_t)3, e->execute("1 + 2").as_int());   // engine stays usable
                ++idx;
            }
            check_eq(msg[0], msg[1], "shift-count error text is byte-identical across backends");
            check_true(msg[0].find("Shift amount must be between 0 and 63") != std::string::npos,
                "the count guard names its range");
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

        // Range-for over a container expression that raises a script exception must
        // unwind with the ORIGINAL exception; the interpreter fell into the
        // not-iterable ladder instead (empty catch text / 'Type mismatch'). Found by
        // the differential fuzzer (seeds 1356/1484 post-grammar-extension: a
        // reload-removed coroutine method called as the for-range container).
        test("range_for_throwing_container_unwinds_original", [this]() {
            const char* src = R"(
                class A { int f = 1; coroutine int co() { yield f; return 2; } }
                var a = A();
                class A { int f = 1; }
                var out = "";
                try { for (auto y : a.co()) { out = out + y; } } catch (ex) { out = out + "co[" + ex + "]"; }
                try { for (auto y2 : a.f) { out = out + y2; } } catch (ex2) { out = out + "|f[" + ex2 + "]"; }
                out = out + "|" + a.f;
                out;
            )";
            std::array<std::string, 2> got;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                got[use_vm ? 1 : 0] = e->execute(src).as<std::string>();
            }
            check(got[0].find("co[A has no member 'co']") != std::string::npos,
                  "interp: original exception surfaces from the container expression");
            check_eq(got[0], got[1], "range-for container-throw parity");
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
        // ---- fuzz seed 110753: ++/-- on a MISSING member. The member read raises through
        // the unwinding state (placeholder null on the stack) and the interpreter's ++/--
        // numeric gate then clobbered it with a bare invalid_numeric_operand ("" when
        // caught); the vm unwinds at the read ----
        test("fuzz_incdec_missing_member_surfaces_member_error", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            auto r = e->execute(R"(
                class C6 { int m8 = 1; }
                var v = C6();
                var out = "";
                try { v.m1--; } catch (e) { out += to_string(e) + "|"; }
                try { v.m1++; } catch (e) { out += to_string(e) + "|"; }
                try { --v.m1; } catch (e) { out += to_string(e) + "|"; }
                try { ++v.m1; } catch (e) { out += to_string(e) + "|"; }
                try { var r = v.m1--; out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                var arr = [C6()];
                try { arr[0].m1--; } catch (e) { out += to_string(e) + "|"; }
                class H { C6 c = C6(); }
                var h = H();
                try { h.c.m1--; } catch (e) { out += to_string(e) + "|"; }
                h.c.m8--;
                out + to_string(h.c.m8);
            )");
            std::string expected;
            for (int i = 0; i < 7; ++i) expected += "C6 has no member 'm1'|";
            check_eq(expected + "0", r.as_string());
        });
        // Same clobber class: a receiver/base whose evaluation raised is a placeholder
        // null, and the interpreter's member read / member store / subscript store then
        // reported the placeholder ("accessed on null", "Cannot assign to rvalue
        // expression", a bare `true`) instead of the pending exception
        test("fuzz_raising_receiver_surfaces_original_exception", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            auto r = e->execute(R"(
                function thrower() { throw "boom"; }
                var out = "";
                try { var x = thrower().m1; out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                try { thrower().m1 = 1; out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                try { thrower().m1 -= 1; out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                try { thrower().m1--; out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                try { thrower()[0] = 1; out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                try { thrower()[0]--; out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                try { thrower().m1(); out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                var arr = [1, 2];
                try { arr[thrower()]--; out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                try { arr[thrower()] = 1; out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                out;
            )");
            std::string expected;
            for (int i = 0; i < 9; ++i) expected += "boom|";
            check_eq(expected, r.as_string());
        });
        // Non-lvalue assignment targets: the vm rejects them at compile time without
        // evaluating anything; the interpreter evaluated target and rhs first (side
        // effects, and a raising target won the error)
        test("assignment_to_call_result_rejected_before_evaluation", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            auto r = e->execute(R"(
                var cnt = 0;
                function bump() { cnt++; return 5; }
                function thrower() { throw "boom"; }
                var out = "";
                try { bump() = 1; out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                try { bump() += 1; out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                try { bump()++; out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                try { bump() = bump(); out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                try { thrower()--; out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                try { bump() += thrower(); out += "unreached|"; } catch (e) { out += to_string(e) + "|"; }
                out + to_string(cnt);
            )");
            std::string expected;
            for (int i = 0; i < 6; ++i) expected += "Complex assignment targets not yet implemented|";
            check_eq(expected + "0", r.as_string());
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
        // symmetric on both backends and stays. Strict (the default) rejects the
        // undeclared name at load, so the runtime ordering is pinned under warn. ----
        test("fuzz_compound_undef_eval_order_parity", [this]() {
            auto run_both = [](const char* src, check_mode mode) {
                std::string msg[2];
                int idx = 0;
                for (bool use_vm : {false, true}) {
                    auto e = jai::engine::make();
                    if (use_vm) { e->set_backend(jai::backend_type::vm); }
                    e->static_checking(mode);
                    try { e->execute(src); }
                    catch (const std::exception& ex) { msg[idx] = ex.what(); }
                    ++idx;
                }
                return std::make_pair(msg[0], msg[1]);
            };
            auto expect_both = [&](const char* src, const std::string& needle, const std::string& what) {
                auto [i_msg, v_msg] = run_both(src, check_mode::warn);
                check_eq(i_msg, v_msg, what + " error text is byte-identical across backends");
                check_true(i_msg.find(needle) != std::string::npos,
                    what + " blames the right name (got: " + i_msg + ")");
                auto [i_strict, v_strict] = run_both(src, check_mode::strict);
                check_eq(i_strict, v_strict, what + " strict listing is byte-identical across backends");
                check_true(i_strict.find("static check failed") == 0 &&
                    i_strict.find("use of undeclared identifier 'v1'") != std::string::npos,
                    what + " is a strict load error (got: " + i_strict + ")");
            };
            // rhs load fails first (rhs-first sequencing): plain undefined-variable text
            expect_both("v1 += v1;", "Undefined variable 'v1'", "undef += same undef");
            expect_both("v1 += fn8();", "Undefined variable 'fn8'", "undef += undef call");
            // rhs fine, target undefined: symmetric decorated text
            expect_both("v1 += 1;", "Undefined variable 'v1' (no 'this' in scope)", "undef += literal");
        });
        // ---- fuzz seed 100991: the vm's register-file lift computed a lifting RIGHT
        // subtree into its temp before an operand-shaped LEFT subscript resolved, so
        // `v6[1] - (g7 + 3)` in a function body raised the rhs overflow where the
        // interpreter raises the lhs index error (left-to-right sequencing). The subscript
        // now materializes into a temp first (op_operand_temp) on every lift level. ----
        test("fuzz_lift_subscript_left_sequencing_parity", [this]() {
            auto run_both = [](const std::string& src) {
                std::string out[2];
                int idx = 0;
                for (bool use_vm : {false, true}) {
                    auto e = jai::engine::make();
                    if (use_vm) { e->set_backend(jai::backend_type::vm); }
                    try { out[idx] = e->execute(src).to_string(); }
                    catch (const std::exception& ex) { out[idx] = std::string("ERROR: ") + ex.what(); }
                    ++idx;
                }
                return std::make_pair(out[0], out[1]);
            };
            auto expect_both = [&](const std::string& src, const std::string& needle, const char* what) {
                auto [i_out, v_out] = run_both(src);
                check_eq(i_out, v_out, std::string(what) + " is byte-identical across backends");
                check_true(i_out.find(needle) != std::string::npos,
                    std::string(what) + " lands the expected text (got: " + i_out + ")");
            };
            const std::string oob = "Array index 1 out of bounds for array of size 0";
            const std::string big = "9223372036854775807";
            auto fn = [](const std::string& body) { return "function f() { " + body + " } f();"; };
            expect_both(fn("array<float> v6 = []; int g7 = " + big + "; auto v = v6[1] - (g7 + 3); return 1;"),
                oob, "typed subscript left of an overflowing rhs");
            expect_both(fn("array<var> v6 = []; int g7 = " + big + "; auto v = v6[1] - (g7 + 3); return 1;"),
                oob, "dynamic subscript left of an overflowing rhs");
            expect_both(fn("array<int> v6 = []; int z = 0; auto v = v6[1] - (1 / z); return 1;"),
                oob, "subscript left of a dividing rhs");
            expect_both(fn("array<int> v6 = []; int g7 = " + big + "; auto v = (v6[1] - (g7 + 3)) * 2; return 1;"),
                oob, "nested lift level");
            expect_both(fn("array<int> a = [7]; array<int> v6 = []; int g7 = " + big + "; auto v = a[0] - (v6[1] - (g7 + 3)); return 1;"),
                oob, "subscript left inside a lifted right subtree");
            expect_both(fn("array<var> g = [[1, 2], [3, 4]]; array<int> v6 = []; int g7 = " + big + "; auto v = g[1][0] - (v6[1] * (g7 + 3)); return 1;"),
                oob, "two-level subscript left, subscript inside the lifted rhs");
            expect_both(fn("array<int> v6 = []; int g7 = " + big + "; return v6[1] - (g7 + 3);"),
                oob, "fused return operand");
            expect_both("class C { array<int> a = []; int g = " + big + "; int m() { return a[1] - (g + 3); } } C().m();",
                oob, "bare-field subscript in a method");
            expect_both(fn("array<int> v6 = []; int g7 = " + big + "; auto l = [=]() -> int { return v6[1] - (g7 + 3); }; return l();"),
                oob, "captured subscript in a lambda");
            // a sound left read still lets the rhs raise, and materialized operands compute as before
            expect_both(fn("array<int> a = [7]; int g7 = " + big + "; auto v = a[0] - (g7 + 3); return 1;"),
                "Integer overflow in '+'", "sound left read then rhs overflow");
            expect_both(fn("array<int> a = [7]; int x = 3; int y = 4; return a[0] - (x * y);"), "-5", "materialized int operand");
            expect_both(fn("array<float> a = [1.5]; int x = 3; int y = 4; return a[0] * (x + y);"), "10.5", "materialized float operand");
            expect_both(fn("array<string> a = [\"ab\"]; string s = \"c\"; string t = \"d\"; return a[0] + (s + t);"), "abcd", "materialized string operand");
            expect_both(fn("array<var> g = [[1, 2], [3, 4]]; int x = 3; int y = 4; return g[1][0] - (x * y);"), "-9", "materialized two-level operand");
            expect_both(fn("array<int> a = [7]; int x = 3; int y = 4; int acc = 0; for (int i = 0; i < 3; i++) { acc += a[0] - (x * y) + i; } return acc;"), "-12", "materialized operand across loop iterations");
        });
        // ---- A member store on a non-object receiver raised the bool `true` on the
        // interpreter (make_value(const char*) picked the bool overload) while the vm raised
        // the text; the caught value is the string on both. ----
        test("member_store_on_non_object_raises_string", [this]() {
            auto run_both = [](const char* src) {
                std::string out[2];
                int idx = 0;
                for (bool use_vm : {false, true}) {
                    auto e = jai::engine::make();
                    if (use_vm) { e->set_backend(jai::backend_type::vm); }
                    e->static_checking(check_mode::off);
                    jai::stdlib::register_all(e);
                    try { out[idx] = e->execute(src).to_string(); }
                    catch (const std::exception& ex) { out[idx] = std::string("ERROR: ") + ex.what(); }
                    ++idx;
                }
                return std::make_pair(out[0], out[1]);
            };
            auto expect_both = [&](const char* src, const std::string& expected, const char* what) {
                auto [i_out, v_out] = run_both(src);
                check_eq(expected, i_out, std::string("interp: ") + what);
                check_eq(expected, v_out, std::string("vm: ") + what);
            };
            expect_both("function f() {} string r = \"\"; try { f.x = 1; } catch (e) { r = type_of(e) + \":\" + to_string(e); } r",
                "string:Cannot assign to member of non-object type", "store on a function");
            expect_both("var n = 5; string r = \"\"; try { n.x = 1; } catch (e) { r = type_of(e) + \":\" + to_string(e); } r",
                "string:Cannot assign to member of non-object type", "store on an int");
            expect_both("class C { static int s = 0; } string r = \"\"; try { C.s = 8; } catch (e) { r = type_of(e) + \":\" + to_string(e); } r",
                "string:Cannot assign to member of non-object type", "dot store on a class name");
        });
        // ---- returnattr: a failed typed-return conversion raises at the `return` with the
        // returning frame on top (the vm captures before the callee frame pops, the
        // interpreter before cleanup; a resumed coroutine's frames sit above the resumer's
        // on both). what() and format_stack_trace() are byte-identical across backends. ----
        test("return_conversion_attribution_parity", [this]() {
            auto run_both = [](const char* src) {
                std::string out[2];
                int idx = 0;
                for (bool use_vm : {false, true}) {
                    auto e = jai::engine::make();
                    if (use_vm) { e->set_backend(jai::backend_type::vm); }
                    try { e->execute(src); out[idx] = "OK"; }
                    catch (const std::exception& ex) { out[idx] = std::string(ex.what()) + "\n" + e->format_stack_trace(); }
                    ++idx;
                }
                return std::make_pair(out[0], out[1]);
            };
            auto expect_both = [&](const char* src, const std::string& expected, const char* what) {
                auto [i_out, v_out] = run_both(src);
                check_eq(expected, i_out, std::string("interp: ") + what);
                check_eq(expected, v_out, std::string("vm: ") + what);
            };
            auto mismatch = [](const char* who) {
                return std::string("Cannot return string from ") + who + " (declared -> int): Type mismatch\n";
            };
            expect_both("int f() {\n  var x = \"s\";\n  return x;\n}\nint g() { return 1; }\nint a = g();\nint b = f();\n",
                mismatch("'f'") + "  at f (<script>:3)\n  at <script> (<script>:7)\n", "plain return");
            expect_both("int f() {\n  var x = \"s\";\n  return x;\n}\nint g() {\n  int r = f();\n  return r;\n}\nint b = g();\n",
                mismatch("'f'") + "  at f (<script>:3)\n  at g (<script>:6)\n  at <script> (<script>:9)\n", "nested return");
            expect_both("var x = \"s\";\nauto l = []() -> int {\n  return x;\n};\nint b = l();\n",
                mismatch("lambda") + "  at <lambda> (<script>:3)\n  at <script> (<script>:5)\n", "lambda return");
            expect_both("class C {\n  int m() {\n    var x = \"s\";\n    return x;\n  }\n}\nauto c = C();\nint b = c.m();\n",
                mismatch("'m'") + "  at m (<script>:4)\n  at <script> (<script>:8)\n", "method return");
            expect_both("coroutine int co() {\n  yield 1;\n  var x = \"s\";\n  return x;\n}\nauto h = co();\nvar a = h.resume();\nvar b = h.resume();\n",
                mismatch("'co'") + "  at co (<script>:4)\n  at <script> (<script>:8)\n", "coroutine return, top-level resume");
            expect_both("coroutine int co() {\n  yield 1;\n  var x = \"s\";\n  return x;\n}\nauto h = co();\nint g() {\n  h.resume();\n  return h.resume();\n}\nint b = g();\n",
                mismatch("'co'") + "  at co (<script>:4)\n  at g (<script>:9)\n  at <script> (<script>:11)\n", "coroutine return, nested resume");
            expect_both("coroutine int co() {\n  var x = \"s\";\n  return x;\n}\nauto h = co();\nint g() {\n  return h.resume();\n}\nint b = g();\n",
                mismatch("'co'") + "  at co (<script>:3)\n  at g (<script>:7)\n  at <script> (<script>:9)\n", "coroutine return, first resume");
            expect_both("coroutine int co() {\n  yield 1;\n  var a = [];\n  int y = a[0];\n  return y;\n}\nauto h = co();\nvar a = h.resume();\nvar b = h.resume();\n",
                "Array index 0 out of bounds for array of size 0: Array index out of bounds\n  at co (<script>:4)\n  at <script> (<script>:9)\n", "coroutine body error");
            expect_both("coroutine int co() {\n  yield 1;\n  throw \"x\";\n  return 1;\n}\nauto h = co();\nvar a = h.resume();\nvar b = h.resume();\n",
                "x\n  at co (<script>:3)\n  at <script> (<script>:8)\n", "coroutine body throw");
            expect_both("int& f() {\n  return 5;\n}\nint b = f();\n",
                "Function with reference return type must return an lvalue reference: Invalid reference\n  at f (<script>:2)\n  at <script> (<script>:4)\n", "ref return");
            // returncatch: a try around the failing return INSIDE the callee catches it; the
            // catch's own return converts in turn and a rethrow keeps the callee frame on top
            expect_both("int f() {\n  try { var x = \"s\"; return x; } catch (e) { return -1; }\n}\nthrow f() == -1 ? \"callee:-1\" : \"callee:other\";\n",
                "callee:-1\n  at <script> (<script>:4)\n", "caught at the return site");
            expect_both("int f() {\n  try {\n    var x = \"s\";\n    return x;\n  } catch (e) {\n    var y = \"t\";\n    return y;\n  }\n}\nint b = f();\n",
                mismatch("'f'") + "  at f (<script>:7)\n  at <script> (<script>:10)\n", "catch return converts in turn");
            expect_both("int f() {\n  try {\n    var x = \"s\";\n    return x;\n  } catch (e) {\n    throw e;\n  }\n}\nint b = f();\n",
                "Cannot return string from 'f' (declared -> int)\n  at f (<script>:6)\n  at <script> (<script>:9)\n", "catch rethrow");
            expect_both("int& f() {\n  try { return 5; } catch (e) { throw \"c:\" + e; }\n}\nint b = f();\n",
                "c:Function with reference return type must return an lvalue reference\n  at f (<script>:2)\n  at <script> (<script>:4)\n", "ref return caught at the site");
            expect_both("coroutine int co() {\n  yield 1;\n  try { var x = \"s\"; return x; } catch (e) { return -1; }\n}\nauto h = co();\nh.resume();\nthrow h.resume() == -1 ? \"co:-1\" : \"co:other\";\n",
                "co:-1\n  at <script> (<script>:7)\n", "coroutine return caught at the site");
            expect_both("class T { int to_int() { throw \"boom\"; } }\nint f() {\n  try { var t = T(); return t; } catch (e) { return -7; }\n}\nthrow f() == -7 ? \"t:-7\" : \"t:other\";\n",
                "t:-7\n  at <script> (<script>:5)\n", "throwing to_int caught at the site");
        });
        // ---- The typed-boundary texts name the VALUE's type (never the var tag): parameters,
        // returns and class targets, byte-identical across backends. ----
        test("typed_boundary_error_names_dynamic_type", [this]() {
            auto run_both = [](const char* src) {
                std::string out[2];
                int idx = 0;
                for (bool use_vm : {false, true}) {
                    auto e = jai::engine::make();
                    if (use_vm) { e->set_backend(jai::backend_type::vm); }
                    try { out[idx] = e->execute(src).to_string(); }
                    catch (const std::exception& ex) { out[idx] = std::string("ERROR: ") + ex.what(); }
                    ++idx;
                }
                return std::make_pair(out[0], out[1]);
            };
            auto expect_both = [&](const char* src, const std::string& expected, const char* what) {
                auto [i_out, v_out] = run_both(src);
                check_eq(expected, i_out, std::string("interp: ") + what);
                check_eq(expected, v_out, std::string("vm: ") + what);
            };
            const char* tail = " string r = \"\"; try { f(); } catch (e) { r = e; } r";
            auto ret = [&](const char* decl) { return std::string(decl) + " int f() { return v; }" + tail; };
            expect_both(ret("var v = \"s\";").c_str(), "Cannot return string from 'f' (declared -> int)", "string return");
            expect_both(ret("var v = cast<char>(97);").c_str(), "Cannot return char from 'f' (declared -> int)", "char return");
            expect_both(ret("var v = null;").c_str(), "Cannot return null from 'f' (declared -> int)", "null return");
            expect_both(ret("class Cat {} var v = Cat();").c_str(), "Cannot return Cat from 'f' (declared -> int)", "object return");
            expect_both(ret("var v = [1];").c_str(), "Cannot return array from 'f' (declared -> int)", "array return");
            expect_both(ret("var v = {\"a\": 1};").c_str(), "Cannot return map from 'f' (declared -> int)", "map return");
            expect_both((std::string("class Cat {} var v = true; Cat f() { return v; }") + tail).c_str(),
                "Cannot convert bool to Cat (no suitable single-argument constructor)", "bool into class return");
            expect_both((std::string("class Cat {} var v = 1.5; Cat f() { return v; }") + tail).c_str(),
                "Cannot convert float to Cat (no suitable single-argument constructor)", "float into class return");
            expect_both("void h(int a) {} var x = \"s\"; string r = \"\"; try { h(x); } catch (e) { r = e; } r",
                "Cannot pass string to parameter 'a' of 'h' (declared int)", "string parameter");
            expect_both("class A {} class B {} void fa(A v) {} var b = B(); string r = \"\"; try { fa(b); } catch (e) { r = e; } r",
                "Cannot convert B to A (no suitable single-argument constructor)", "class parameter");
        });
        test("fuzz_compound_undef_rhs_side_effects_run_first", [this]() {
            const char* src = R"(
                var log = "";
                function s() { log += "x"; return 1; }
                try { v1 += s(); } catch (err) { log += "c"; }
                log;
            )";
            auto strict = make_engine();
            check_throws<static_check_error>([&]() { strict->execute(src); });
            auto e = make_engine();
            e->static_checking(check_mode::warn);
            check_eq(std::string("xc"), e->execute(src).as_string());
            // strict admits the same ordering through a typed target: the rhs call runs,
            // then the var-sourced string fails the int store
            auto typed = make_engine();
            check_eq(std::string("xc"), typed->execute(R"(
                var log = "";
                var bad = "s";
                function s() { log += "x"; return bad; }
                int t = 0;
                try { t += s(); } catch (err) { log += "c"; }
                log;
            )").as_string());
        });

        // ---- fuzz KD-CORO-RESUME-PAST-END (seeds 4/38/98/195/216/387/626/840/...):
        // resuming past the final yield of a no-return coroutine: the interpreter
        // replayed the stale last-yielded value (and a zero-yield METHOD coroutine
        // returned its receiver via the implicit-'this' rule) while the vm completed
        // with null. A finished body completes with null on both backends, like any
        // function falling off the end; explicit returns are unaffected. ----
        test("fuzz_coro_resume_past_end_yields_null", [this]() {
            const char* src = R"(
                coroutine int co() { yield 10; yield 11; }
                auto h = co();
                var a = to_string(h.resume()) + "|" + to_string(h.resume()) + "|" + to_string(h.resume());
                class D { int v = 7; coroutine int g() { v = 8; } }
                auto d = D();
                auto hm = d.g();
                var b = to_string(hm.resume()) + "|" + to_string(d.v);
                class F { int q = 3; coroutine int g2() { yield 1; return this.q; } }
                auto f = F();
                auto hf = f.g2();
                var c = to_string(hf.resume()) + "|" + to_string(hf.resume());
                a + " / " + b + " / " + c;
            )";
            std::string out[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                stdlib::register_all(*e);
                out[idx++] = e->execute(src).as_string();
            }
            check_eq(std::string("10|11|null / null|8 / 1|3"), out[0], "interp: past-end resume completes null");
            check_eq(out[0], out[1], "backends agree byte-identically");
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

        // ---- Callee-first for member and computed callees (C++17 [expr.call]; the
        // interpreter is the oracle): `recv.m(args)` evaluates the receiver and looks the
        // member up BEFORE any argument runs, so null-receiver / unknown-member /
        // not-callable errors beat argument side effects (the vm used to evaluate the
        // arguments first - fuzz seed 132479 blamed 'length' for `v8.push(v8.length())`
        // on a null v8). Overload resolution stays post-args. Also the scalar-receiver
        // text: `n.nosuch` on an int/float/bool/char/function receiver reads
        // `int has no member 'nosuch'` (both backends raised an EMPTY text). Each program
        // logs argument side effects into `log` and returns `log + "|" + caught`. ----
        auto run_order_both = [](const char* src, check_mode mode) {
            std::string out[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->static_checking(mode);
                try {
                    auto r = e->execute(src);
                    out[idx] = r.is_string() ? r.as_string() : std::string("<non-string>");
                } catch (const std::exception& ex) { out[idx] = std::string("what:") + ex.what(); }
                ++idx;
            }
            return std::make_pair(out[0], out[1]);
        };
        auto expect_order = [this, run_order_both](const char* src, const std::string& expected,
                                                   const std::string& what, check_mode mode) {
            auto [i, v] = run_order_both(src, mode);
            check_eq(expected, i, what + " (interpreter)");
            check_eq(i, v, what + " parity");
            return i;
        };
        const char* side_prelude = "var log = \"\"; int side() { log += \"A\"; return 1; }\n";
        test("callee_first_method_null_receiver_before_args", [this, expect_order, run_order_both, side_prelude]() {
            expect_order((std::string(side_prelude) + R"(
                var v8 = null; var caught = "";
                try { v8.push(side()); } catch (e) { caught = e; }
                log + "|" + caught;
            )").c_str(), "|Cannot call 'push' on null ('v8' is null)", "null receiver beats arg side effects", check_mode::strict);
            // the fuzz shape (seed 132479): the callee's null error names the CALLEE member
            auto [i, v] = run_order_both(R"(
                auto v7 = [4611686018427387904, 5]; auto v8 = v7.reverse(); v8.push(v8.length());
            )", check_mode::strict);
            check_eq(i, v, "uncaught null-receiver what() parity");
            check_true(i.find("Cannot call 'push' on null ('v8' is null)") != std::string::npos,
                "the callee member is blamed, not the argument's (got: " + i + ")");
            expect_order((std::string(side_prelude) + R"(
                var v8 = null; var r = v8?.push(side());
                log + "|" + (r == null);
            )").c_str(), "|true", "null-safe call skips the arguments", check_mode::strict);
        });
        test("scalar_receiver_unknown_member_text", [this, expect_order, run_order_both, side_prelude]() {
            const std::pair<const char*, const char*> receivers[] = {
                {"5", "int"}, {"1.5", "float"}, {"true", "bool"}, {"'c'", "char"}, {"side", "function"},
            };
            for (const auto& [init, type_name] : receivers) {
                const std::string src = std::string(side_prelude) +
                    "var n = " + init + "; var caught = \"\";\n"
                    "try { n.nosuch(side()); } catch (e) { caught = e; }\n"
                    "log + \"|\" + caught;";
                expect_order(src.c_str(), std::string("|") + type_name + " has no member 'nosuch'",
                    std::string("scalar receiver ") + type_name, check_mode::strict);
            }
            expect_order(R"(
                var n = 5; var caught = "";
                try { var z = n.nosuch; } catch (e) { caught = e; }
                caught;
            )", "int has no member 'nosuch'", "scalar member read", check_mode::strict);
            auto [i, v] = run_order_both("var n = 5; n.nosuch(1);", check_mode::strict);
            check_eq(i, v, "uncaught scalar-member what() parity");
            check_true(i.find("int has no member 'nosuch'") != std::string::npos,
                "uncaught scalar-member text (got: " + i + ")");
            // container receivers keep their own texts, still before the arguments
            expect_order((std::string(side_prelude) + R"(
                var s = "abc"; var caught = "";
                try { s.nosuch(side()); } catch (e) { caught = e; }
                log + "|" + caught;
            )").c_str(), "|string has no member 'nosuch'", "string receiver", check_mode::strict);
            expect_order((std::string(side_prelude) + R"(
                var a = [1]; var caught = "";
                try { a.nosuch(side()); } catch (e) { caught = e; }
                log + "|" + caught;
            )").c_str(), "|array has no member 'nosuch'", "array receiver", check_mode::strict);
            expect_order((std::string(side_prelude) + R"(
                var m = {"k": 1}; var caught = "";
                try { m.nosuch(side()); } catch (e) { caught = e; }
                log + "|" + caught;
            )").c_str(), "|map has no member or key 'nosuch'", "map receiver", check_mode::strict);
            expect_order((std::string(side_prelude) + R"(
                class W { int x = 0; }
                shared_ptr<W> o = W(); weak_ptr<W> w = o; var caught = "";
                try { w.nosuch(side()); } catch (e) { caught = e; }
                log + "|" + caught;
            )").c_str(), "|weak_ptr has no member 'nosuch'", "weak_ptr receiver", check_mode::warn);
        });
        test("object_receiver_lookup_before_args", [this, expect_order, side_prelude]() {
            const std::string cls = std::string(side_prelude) + R"(
                class O {
                    int x = 1;
                    var lam = null;
                    int f(int a) { return a; }
                    int f(int a, int b) { return a + b; }
                }
                var obj = O(); var caught = "none";
            )";
            expect_order((cls + R"(
                try { obj.nosuch(side()); } catch (e) { caught = e; }
                log + "|" + caught;
            )").c_str(), "|O has no member 'nosuch'", "unknown member beats arg side effects", check_mode::warn);
            // a plain field is not callable: the check fires before the arguments
            expect_order((cls + R"(
                try { obj.x(side()); } catch (e) { caught = "c:" + e; }
                log + "|" + caught;
            )").c_str(), "|c:Cannot call 'x': it is an int, not a function", "field shadow not-callable beats arg side effects", check_mode::warn);
            expect_order((cls + R"(
                obj.lam = [](int a) { return a * 2; };
                var r = obj.lam(side());
                log + "|" + r;
            )").c_str(), "A|2", "callable field still calls once", check_mode::warn);
            // overload resolution needs the argument values: stays post-args
            expect_order((cls + R"(
                var r1 = obj.f(side()); var r2 = obj.f(side(), 2);
                log + "|" + r1 + ":" + r2;
            )").c_str(), "AA|1:3", "multi-overload method resolves post-args", check_mode::warn);
        });
        test("nested_callee_lookup_order", [this, expect_order, side_prelude]() {
            const std::string cls = std::string(side_prelude) + R"(
                class C { int h() { log += "H"; return 1; } }
                class A { int f(int v) { return v; } }
                class A2 { int z = 0; }
                var c = C(); var caught = "";
            )";
            expect_order((cls + R"(
                var a = A(); var b = null;
                try { a.f(b.g(c.h())); } catch (e) { caught = e; }
                log + "|" + caught;
            )").c_str(), "|Cannot call 'g' on null ('b' is null)", "inner null receiver beats the innermost call", check_mode::warn);
            expect_order((cls + R"(
                var a = null; var b = null;
                try { a.f(b.g(c.h())); } catch (e) { caught = e; }
                log + "|" + caught;
            )").c_str(), "|Cannot call 'f' on null ('a' is null)", "outer null receiver beats everything", check_mode::warn);
            expect_order((cls + R"(
                var a = A2(); var b = null;
                try { a.f(b.g(c.h())); } catch (e) { caught = e; }
                log + "|" + caught;
            )").c_str(), "|A2 has no member 'f'", "outer unknown member beats the inner null", check_mode::warn);
        });
        test("trivial_args_unchanged", [this, expect_order, side_prelude]() {
            expect_order(R"(
                class O { int f(int a, int b, string s) { return a + b + s.length(); } }
                string run() {
                    var obj = O(); int local = 2; var arr = [1];
                    arr.push(local);
                    return "" + obj.f(1, local, "abc") + ":" + arr.length();
                }
                run();
            )", "6:2", "slot-local / literal arguments", check_mode::strict);
            expect_order((std::string(side_prelude) + R"(
                var v8 = null; var n = 5;
                var c1 = ""; var c2 = ""; var c3 = ""; var c4 = "";
                try { v8.push(1); } catch (e) { c1 = e; }
                try { v8.push(side()); } catch (e) { c2 = e; }
                try { n.nosuch(1); } catch (e) { c3 = e; }
                try { n.nosuch(side()); } catch (e) { c4 = e; }
                log + "|" + (c1 == c2) + ":" + (c3 == c4) + "|" + c1 + "|" + c3;
            )").c_str(), "|true:true|Cannot call 'push' on null ('v8' is null)|int has no member 'nosuch'",
                "probed and unprobed shapes raise the same texts", check_mode::strict);
        });
        test("computed_callee_callable_check_before_args", [this, expect_order, side_prelude]() {
            expect_order((std::string(side_prelude) + R"(
                var arr = [1]; var caught = "none";
                try { arr[0](side()); } catch (e) { caught = "c:" + e; }
                log + "|" + caught;
            )").c_str(), "|c:Cannot call a value of type int: it is not a function", "non-callable element beats arg side effects", check_mode::strict);
            expect_order((std::string(side_prelude) + R"(
                var fs = [[](int a) -> int { return a * 2; }];
                var r = fs[0](side());
                log + "|" + r;
            )").c_str(), "A|2", "callable element still calls", check_mode::strict);
            expect_order((std::string(side_prelude) + R"(
                function mk() -> auto { return [](int a) -> int { return a + 100; }; }
                var r = mk()(side());
                log + "|" + r;
            )").c_str(), "A|101", "call-result callee still calls", check_mode::strict);
        });
        test("operator_overloads_unaffected", [this, expect_order, run_order_both]() {
            expect_order(R"(
                var log = "";
                class V {
                    int v = 0;
                    V(int x) { v = x; }
                    function operator+(V o) -> V { log += "+"; return V(v + o.v); }
                }
                V make() { log += "M"; return V(2); }
                var a = V(1);
                var r = a + make();
                log + "|" + r.v;
            )", "M+|3", "operator overload call", check_mode::strict);
            auto [i, v] = run_order_both(R"(
                var log = "";
                class V { int v = 0; V(int x) { v = x; } function operator+(V o) -> V { return V(v + o.v); } }
                V make() { log += "M"; return V(2); }
                var n = null; var caught = "none";
                try { var q = n + make(); } catch (e) { caught = "c"; }
                log + "|" + caught;
            )", check_mode::strict);
            check_eq(i, v, "null left operand parity");
        });
        test("coroutine_calls_unaffected", [this, expect_order, side_prelude]() {
            // Method calls inside a coroutine body, resumer-side method calls between
            // resumes, and a handle method taking a non-trivial argument. (A yield INSIDE
            // a call argument - `acc.add((yield 1) ? 0 : 5)` - is a pre-existing
            // interpreter replay divergence, free-function callees included; not pinned.)
            expect_order(R"(
                class Acc { int total = 0; int add(int v) { total += v; return total; } }
                var acc = Acc();
                coroutine int gen() {
                    var t = (yield 1) ? 0 : 5;
                    acc.add(t + 0);
                    return acc.total;
                }
                var h = gen();
                var first = h.resume();
                var acc2 = Acc(); var between = acc2.add(2 + 3);
                var final = h.resume();
                "" + first + ":" + between + ":" + final + ":" + h.done();
            )", "1:5:5:true", "method calls around a yield", check_mode::strict);
            expect_order((std::string(side_prelude) + R"(
                coroutine int gen() { yield 1; return 2; }
                var h = gen(); h.resume(); h.resume();
                var d = h.done(side());
                log + "|" + d;
            )").c_str(), "A|true", "handle method with a non-trivial argument", check_mode::strict);
        });

        // ---- hybrid methods: a bare C++ property name reads/writes through the binder
        // accessors exactly like this.name (lookup ladder: locals -> members -> globals) ----
        test("hybrid_bare_cpp_property_read", [this]() {
            auto e = make_engine();
            bind_hybrid_probe(*e);
            e->execute(R"(
                class W : HybridProbe {
                    W() : super() {}
                    int v() { return value; }
                    int v1() { return value + 1; }
                    int v2() { return 1 + value; }
                    bool lt() { return value < 10; }
                    string nm() { return name + "!"; }
                    int sum() { return value + value; }
                    int viaLocal() { int k = 2; return value + k; }
                }
                W w = W(); w.value = 5; w.name = "x";
            )");
            check_eq((int64_t)5, e->execute("w.v()").as_int());
            check_eq((int64_t)6, e->execute("w.v1()").as_int());
            check_eq((int64_t)6, e->execute("w.v2()").as_int());
            check_true(e->execute("w.lt()").as_bool());
            check_eq(std::string("x!"), e->execute("w.nm()").as_string());
            check_eq((int64_t)10, e->execute("w.sum()").as_int());
            check_eq((int64_t)7, e->execute("w.viaLocal()").as_int());
            check_eq((int64_t)6, e->execute("w.value = 6; w.v()").as_int(), "no stale shadow after a C++-side change");
            check_false(e->execute("w").get_class_instance()->has_user_overflow_fields(),
                        "bare reads never materialize a field for a C++ property");
        });
        test("hybrid_bare_cpp_property_store_writes_through", [this]() {
            auto e = make_engine();
            bind_hybrid_probe(*e);
            e->execute(R"(
                class W : HybridProbe {
                    W() : super() {}
                    void set(int x) { value = x; }
                    void rename(string s) { name = s; }
                    int v() { return value; }
                }
                W w = W(); w.set(7); w.rename("r");
            )");
            check_eq((int64_t)7, e->execute("w.value").as_int());
            check_eq((int64_t)14, e->execute("w.doubled()").as_int(), "the C++ member itself moved");
            check_eq((int64_t)7, e->execute("w.v()").as_int());
            check_eq(std::string("r"), e->execute("w.name").as_string());
            check_false(e->execute("w").get_class_instance()->has_user_overflow_fields());
        });
        test("hybrid_bare_cpp_property_compound_and_incdec", [this]() {
            auto e = make_engine();
            bind_hybrid_probe(*e);
            e->execute(R"(
                class W : HybridProbe {
                    W() : super() {}
                    void bump() { value += 2; value++; ++value; value -= 1; }
                    int post() { return value++; }
                    int pre() { return ++value; }
                    int postdec() { return value--; }
                    int predec() { return --value; }
                    void bang() { name += "!"; }
                    void times() { value *= 3; }
                }
                W w = W(); w.value = 5; w.name = "x"; w.bump();
            )");
            check_eq((int64_t)8, e->execute("w.value").as_int());
            check_eq((int64_t)16, e->execute("w.doubled()").as_int());
            check_eq((int64_t)8, e->execute("w.post()").as_int());
            check_eq((int64_t)9, e->execute("w.value").as_int());
            check_eq((int64_t)10, e->execute("w.pre()").as_int());
            check_eq((int64_t)10, e->execute("w.value").as_int());
            check_eq((int64_t)10, e->execute("w.postdec()").as_int());
            check_eq((int64_t)8, e->execute("w.predec()").as_int());
            check_eq((int64_t)8, e->execute("w.value").as_int());
            check_eq((int64_t)24, e->execute("w.times(); w.value").as_int());
            check_eq(std::string("x!"), e->execute("w.bang(); w.name").as_string());
            check_false(e->execute("w").get_class_instance()->has_user_overflow_fields());
        });
        test("hybrid_bare_cpp_property_in_lambdas", [this]() {
            auto e = make_engine();
            bind_hybrid_probe(*e);
            e->execute(R"(
                class W : HybridProbe {
                    int q = 3;
                    W() : super() {}
                    int a() { auto f = [this]() { return value; }; return f(); }
                    int b() { auto f = []() { return value + 1; }; return f(); }
                    int bq() { auto f = []() { return q + 1; }; return f(); }
                    int b0() { auto f = []() { return value; }; return f(); }
                    int b1() { auto f = []() { int k = 1; return value + k; }; return f(); }
                    int b2() { auto f = []() { return this.value + 1; }; return f(); }
                    int b3() { auto f = []() { return 1 + value; }; return f(); }
                    int a1() { auto f = [this]() { return value + 1; }; return f(); }
                    void c() { auto f = [this]() { value = 9; }; f(); }
                    int d() { auto f = [=]() { return value; }; return f(); }
                    int dq() { auto f = [=]() { return q; }; return f(); }
                    int r() { auto f = [&]() { return value; }; return f(); }
                    void cc() { auto f = [this]() { value += 1; value++; }; f(); }
                }
                W w = W(); w.value = 5;
            )");
            auto run_int = [&](const char* src, const char* label) -> int64_t {
                try { return e->execute(src).as_int(); }
                catch (const std::exception& ex) { check_true(false, std::string(label) + ": " + ex.what()); return -1; }
            };
            check_eq((int64_t)5, run_int("w.a()", "[this] lambda read"), "[this] lambda reads the property");
            check_eq((int64_t)4, run_int("w.bq()", "bare lambda field read"), "bare lambda reads a script field (baseline)");
            check_eq((int64_t)5, run_int("w.b0()", "bare lambda plain property read"));
            check_eq((int64_t)6, run_int("w.b1()", "bare lambda property + local"));
            check_eq((int64_t)6, run_int("w.b2()", "bare lambda this.property + 1"));
            check_eq((int64_t)6, run_int("w.b3()", "bare lambda 1 + property"));
            check_eq((int64_t)6, run_int("w.a1()", "[this] lambda property + 1"));
            check_eq((int64_t)6, run_int("w.b()", "bare lambda property read"), "bare lambda reads the property");
            check_eq((int64_t)9, run_int("w.c(); w.value", "[this] lambda store"), "[this] lambda store writes through");
            check_eq((int64_t)3, run_int("w.dq()", "[=] field snapshot"), "[=] snapshots a script field (baseline)");
            check_eq((int64_t)9, run_int("w.d()", "[=] property snapshot"), "[=] snapshots the property like a field");
            check_eq((int64_t)9, run_int("w.r()", "[&] property share"), "[&] shares the member itself (by_ref_capture_of_cpp_property_is_live)");
            check_eq((int64_t)11, run_int("w.cc(); w.value", "[this] lambda compound"));
            check_false(e->execute("w").get_class_instance()->has_user_overflow_fields());
        });
        test("hybrid_script_field_shadows_cpp_property", [this]() {
            auto e = make_engine();
            bind_hybrid_probe(*e);
            e->execute(R"(
                class S : HybridProbe {
                    int value = 42;
                    S() : super() {}
                    int v() { return value; }
                    int tv() { return this.value; }
                    void set(int x) { value = x; }
                    void tset(int x) { this.value = x; }
                    void bump() { value += 1; value++; }
                }
                S s = S();
            )");
            check_eq((int64_t)42, e->execute("s.v()").as_int());
            check_eq((int64_t)42, e->execute("s.tv()").as_int());
            check_eq((int64_t)42, e->execute("s.value").as_int());
            check_eq((int64_t)0, e->execute("s.doubled()").as_int(), "the C++ member is untouched");
            e->execute("s.set(43)");
            check_eq((int64_t)43, e->execute("s.v()").as_int());
            check_eq((int64_t)43, e->execute("s.tv()").as_int());
            check_eq((int64_t)0, e->execute("s.doubled()").as_int(), "a bare store lands on the shadowing script field");
            e->execute("s.tset(44)");
            check_eq((int64_t)44, e->execute("s.v()").as_int());
            check_eq((int64_t)0, e->execute("s.doubled()").as_int());
            e->execute("s.bump()");
            check_eq((int64_t)46, e->execute("s.v()").as_int());
            check_eq((int64_t)0, e->execute("s.doubled()").as_int());
        });
        test("hybrid_bare_cpp_property_error_texts_match_this_form", [this]() {
            std::string bare_msg[2], this_msg[2], undef_msg[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->static_checking(check_mode::off);   // the undef row is a RUNTIME text
                bind_hybrid_probe(*e);
                e->execute(R"(
                    class W : HybridProbe {
                        W() : super() {}
                        void bad() { value = "str"; }
                        void bad_this() { this.value = "str"; }
                        int ro_read() { return ro; }
                        void ro_store() { ro = 5; }
                        void ro_store_this() { this.ro = 5; }
                        int undef() { return zzz_nope + 1; }
                    }
                    W w = W(); w.value = 5;
                )");
                bool threw = false;
                try { e->execute("w.bad();"); } catch (const std::exception& ex) { threw = true; bare_msg[idx] = ex.what(); }
                check_true(threw, "a mistyped bare store raises");
                threw = false;
                try { e->execute("w.bad_this();"); } catch (const std::exception& ex) { threw = true; this_msg[idx] = ex.what(); }
                check_true(threw, "a mistyped this. store raises");
                check_eq((int64_t)5, e->execute("w.value").as_int(), "a rejected store leaves the member alone");
                check_eq((int64_t)105, e->execute("w.ro_read()").as_int(), "bare read of a getter-only property");
                e->execute("w.ro_store(); w.ro_store_this();");
                check_eq((int64_t)105, e->execute("w.ro_read()").as_int(), "read-only stores are silent no-ops both ways");
                check_eq((int64_t)5, e->execute("w.value").as_int());
                threw = false;
                try { e->execute("w.undef();"); } catch (const std::exception& ex) { threw = true; undef_msg[idx] = ex.what(); }
                check_true(threw);
                check_true(undef_msg[idx].find("Undefined variable 'zzz_nope'") != std::string::npos, undef_msg[idx]);
                ++idx;
            }
            check_eq(bare_msg[0], bare_msg[1], "bare-store error text is byte-identical across backends");
            check_eq(this_msg[0], this_msg[1], "this-store error text is byte-identical across backends");
            check_eq(undef_msg[0], undef_msg[1], "undefined-name text is byte-identical across backends");
        });
        // ---- by-reference binding of C++ properties (Dev ruling: bind the MEMBER ITSELF;
        // docs/reference_model.md CPP-MEMBER / PROPERTY). Every row runs both backends. ----
        auto propref_engine = [](bool use_vm) {
            auto e = jai::engine::make();
            if (use_vm) { e->set_backend(jai::backend_type::vm); }
            bind_hybrid_probe(*e);
            return e;
        };
        auto caught = [](engine& e, const char* src) -> std::string {
            try { e.execute(src); } catch (const std::exception& ex) { return ex.what(); }
            return "";
        };
        test("cpp_property_by_ref_param_writes_member", [this, propref_engine]() {
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->execute(R"(
                    void by_ref(int& p) { p = 41; p += 1; }
                    void by_ref_f(float& f) { f = 2.0; f *= 2; }
                    void by_ref_b(bool& b) { b = true; }
                    void by_ref_s(string& s) { s += "!"; }
                    class W : HybridProbe {
                        W() : super() {}
                        void arg_ref() { by_ref(value); }
                        void arg_ref_this() { by_ref(this.value); }
                        void others() { by_ref_f(ratio); by_ref_b(flag); by_ref_s(name); }
                        void others_this() { by_ref_f(this.ratio); by_ref_b(this.flag); by_ref_s(this.name); }
                    }
                    W w = W();
                )");
                auto cpp = probe_of(*e, "w");
                e->execute("w.arg_ref();");
                check_eq((int64_t)42, cpp->value, "bare by-ref param wrote the C++ member");
                check_eq((int64_t)84, e->execute("w.doubled()").as_int());
                cpp->value = 0;
                e->execute("w.arg_ref_this();");
                check_eq((int64_t)42, cpp->value, "this. by-ref param wrote the C++ member");
                e->execute("w.others();");
                check_near(4.0, cpp->ratio, 1e-9);
                check_true(cpp->flag, "bool member through bool&");
                check_eq(std::string("n!"), cpp->name, "std::string member through string&");
                cpp->ratio = 1.0; cpp->flag = false; cpp->name = "m";
                e->execute("w.others_this();");
                check_near(4.0, cpp->ratio, 1e-9);
                check_true(cpp->flag);
                check_eq(std::string("m!"), cpp->name);
                check_false(e->execute("w").get_class_instance()->has_user_overflow_fields(),
                            "no field materialized for a C++ property");
            }
        });
        test("cpp_property_ref_decl_compound", [this, propref_engine]() {
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->execute(R"(
                    class W : HybridProbe {
                        W() : super() {}
                        int decl_ref() { auto& r = value; r += 1; ++r; r = r + 1; return r; }
                        int decl_ref_this() { auto& r = this.value; r += 1; ++r; r = r + 1; return r; }
                        int typed_ref() { int& r = value; r -= 2; return r; }
                        float fr() { auto& r = ratio; r += 0.5; r = r * 2; return r; }
                        bool br() { auto& r = flag; r = !r; return r; }
                        string sr() { auto& r = name; r += "x"; r = r + "y"; return r; }
                        int postfix() { auto& r = value; int old = r++; r--; return old; }
                    }
                    W w = W(); w.value = 5;
                )");
                auto cpp = probe_of(*e, "w");
                check_eq((int64_t)8, e->execute("w.decl_ref()").as_int(), "plain =, += and ++ all land");
                check_eq((int64_t)8, cpp->value, "the C++ member advanced by 3");
                check_eq((int64_t)11, e->execute("w.decl_ref_this()").as_int());
                check_eq((int64_t)11, cpp->value);
                check_eq((int64_t)9, e->execute("w.typed_ref()").as_int(), "int& declaration");
                check_eq((int64_t)9, cpp->value);
                check_near(4.0, e->execute("w.fr()").as_float(), 1e-9);
                check_near(4.0, cpp->ratio, 1e-9);
                check_true(e->execute("w.br()").as_bool());
                check_true(cpp->flag);
                check_eq(std::string("nxy"), e->execute("w.sr()").as_string());
                check_eq(std::string("nxy"), cpp->name);
                check_eq((int64_t)9, e->execute("w.postfix()").as_int(), "postfix yields the old value");
                check_eq((int64_t)9, cpp->value);
            }
        });
        test("cpp_property_ref_reads_are_live", [this, propref_engine]() {
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->execute(R"(
                    class W : HybridProbe {
                        W() : super() {}
                        int live() { auto& r = value; bump_cpp(); return r; }
                        int live_copy() { auto& r = value; int snap = r; bump_cpp(); return snap; }
                    }
                    W w = W(); w.value = 1;
                )");
                check_eq((int64_t)101, e->execute("w.live()").as_int(), "a C++-side change is visible through the reference");
                check_eq((int64_t)101, e->execute("w.live_copy()").as_int(), "a by-value copy detaches");
                check_eq((int64_t)201, probe_of(*e, "w")->value);
            }
        });
        test("cpp_property_ref_outlives_call", [this, propref_engine, caught]() {
            std::string retired_msg[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->execute(R"(
                    class W : HybridProbe {
                        W() : super() {}
                        function grab() -> int& { return value; }
                        function grab_this() -> int& { return this.value; }
                    }
                    W w = W(); w.value = 1;
                    auto& r = w.grab(); r = 9;
                    auto& r2 = w.grab_this(); r2 += 1;
                )");
                auto cpp = probe_of(*e, "w");
                check_eq((int64_t)10, cpp->value, "a returned reference writes the member");
                // the reference keeps the object alive after the script drops its handle
                std::weak_ptr<hybrid_prop_probe> weak = cpp;
                cpp.reset();
                e->execute("w = null;");
                check_false(weak.expired(), "the CPP-MEMBER pin keeps the object alive");
                e->execute("r = 77;");
                check_eq((int64_t)77, weak.lock()->value, "and the write still lands after the handle is gone");
                // retired layer: a fresh bind reports, byte-identical on both backends
                auto plain = std::make_shared<hybrid_prop_probe>();
                plain->value = 3;
                e->add_global("kept", e->extend(plain, "W"));
                e->execute("auto& live = kept.value; live = 4;");
                check_eq((int64_t)4, plain->value, "bound through an extended object's layer");
                (void)e->script_layer_detach(plain);
                retired_msg[idx] = caught(*e, "auto& again = kept.value;");
                check_true(retired_msg[idx].find("was detached") != std::string::npos, retired_msg[idx]);
                ++idx;
            }
            check_eq(retired_msg[0], retired_msg[1], "retired-layer text is byte-identical across backends");
        });
        test("lambda_property_ref_routes_through_accessors", [this, propref_engine]() {
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->execute(R"(
                    void by_ref(int& p) { p = 5; p += 1; }
                    void by_ref_x(int& p) { p = 21; }
                    class W : HybridProbe {
                        W() : super() {}
                        void arg() { by_ref(lam); }
                        int decl() { auto& r = lam; r = 5; r += 1; return r; }
                        void child_decl() { auto& c = child; c.x = 7; }
                        void child_chain() { by_ref_x(child.x); }
                        void child_chain_this() { by_ref_x(this.child.x); }
                    }
                    W w = W();
                )");
                auto cpp = probe_of(*e, "w");
                e->execute("w.arg();");
                check_eq((int64_t)6, cpp->lam_backing, "a lambda-pair property writes through its setter");
                check_eq((int64_t)2, cpp->lam_writes, "one setter call per store");
                cpp->lam_writes = 0; cpp->lam_reads = 0;
                check_eq((int64_t)6, e->execute("w.decl()").as_int());
                check_eq((int64_t)2, cpp->lam_writes);
                check_gt(cpp->lam_reads, (int64_t)0, "reads go through the getter");
                e->execute("w.child_decl();");
                check_eq((int64_t)7, cpp->child.x, "a class-typed member property derefs to the live member");
                e->execute("w.child_chain();");
                check_eq((int64_t)21, cpp->child.x, "a chain through a class-typed property binds the inner member");
                cpp->child.x = 0;
                e->execute("w.child_chain_this();");
                check_eq((int64_t)21, cpp->child.x);
                check_eq((int64_t)21, e->execute("by_ref_x(w.child.x); w.child.x").as_int(), "from outside too");
            }
        });
        test("read_only_property_ref_is_silent_noop", [this, propref_engine]() {
            // Pins TODAY's contract for a setter-less property (the row-29 no-op), matched
            // through a reference. Dev question: a strict-by-default language should
            // arguably raise on every spelling instead (see the report).
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->execute(R"(
                    void by_ref(int& p) { p = 1; }
                    class W : HybridProbe {
                        W() : super() {}
                        int decl() { auto& r = ro; r = 5; r += 1; return r; }
                        int arg() { by_ref(ro); return ro; }
                    }
                    W w = W(); w.value = 5;
                )");
                check_eq((int64_t)105, e->execute("w.decl()").as_int(), "stores through a read-only ref are no-ops");
                check_eq((int64_t)105, e->execute("w.arg()").as_int());
                check_eq((int64_t)5, probe_of(*e, "w")->value);
            }
        });
        test("script_field_shadows_property_for_refs", [this, propref_engine]() {
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->execute(R"(
                    void by_ref(int& p) { p += 1; }
                    class S : HybridProbe {
                        int value = 7;
                        S() : super() {}
                        void go() { by_ref(value); by_ref(this.value); auto& r = value; r += 1; auto& t = this.value; t += 1; }
                    }
                    S s = S(); s.go();
                )");
                check_eq((int64_t)11, e->execute("s.value").as_int(), "the script field took every write");
                check_eq((int64_t)0, probe_of(*e, "s")->value, "the C++ member is untouched");
                check_eq((int64_t)12, e->execute("by_ref(s.value); s.value").as_int(), "shadowing holds from outside");
                check_eq((int64_t)0, probe_of(*e, "s")->value);
            }
        });
        test("plain_cpp_receiver_binds", [this, propref_engine]() {
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                hybrid_prop_probe raw;
                raw.value = 10;
                e->add_global_ref("g", raw);
                e->execute(R"(
                    void by_ref(int& p) { p += 1; }
                    void by_ref_s(string& s) { s += "!"; }
                    HybridProbe p = HybridProbe(); p.value = 41;
                    by_ref(p.value);
                    auto& r = p.value; r += 1;
                    by_ref(g.value);
                    auto& gr = g.value; gr += 1;
                    var s = p.self(); by_ref(s.value);
                    by_ref_s(g.name);
                )");
                check_eq((int64_t)44, probe_of(*e, "p")->value, "a script-constructed C++ object binds the member (T& return handle included)");
                check_eq((int64_t)12, raw.value, "a raw add_global_ref handle binds the member");
                check_eq(std::string("n!"), raw.name);
            }
        });
        test("hybrid_receiver_from_outside", [this, propref_engine]() {
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->execute(R"(
                    void by_ref(int& p) { p += 1; }
                    class W : HybridProbe { W() : super() {} }
                    class Holder { W w = W(); }
                    W w = W(); w.value = 1;
                    by_ref(w.value);
                    auto& r = w.value; r += 1;
                    Holder h = Holder();
                    by_ref(h.w.value);
                    auto& hr = h.w.value; hr += 1;
                )");
                check_eq((int64_t)3, probe_of(*e, "w")->value);
                check_eq((int64_t)2, probe_of(*e, "h.w")->value, "a chain through a script field reaches the member");
            }
        });
        test("by_value_still_copies", [this, propref_engine]() {
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->execute(R"(
                    int by_val(int p) { p = 99; return p + 1; }
                    class W : HybridProbe {
                        W() : super() {}
                        int v() { return by_val(value); }
                        int vt() { return by_val(this.value); }
                        int copy() { int x = this.value; x = 5; auto y = value; y = 6; return x + y; }
                    }
                    W w = W(); w.value = 42;
                )");
                check_eq((int64_t)100, e->execute("w.v()").as_int());
                check_eq((int64_t)100, e->execute("w.vt()").as_int());
                check_eq((int64_t)11, e->execute("w.copy()").as_int());
                check_eq((int64_t)42, probe_of(*e, "w")->value, "by-value passes and copies leave the member alone");
            }
        });
        test("mistyped_store_through_property_ref_texts_match", [this, propref_engine, caught]() {
            std::string ref_msg[2], direct_msg[2], narrow_ref[2], narrow_direct[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->static_checking(check_mode::off);   // runtime texts under test
                e->execute(R"(
                    void by_ref_str(string& s) { s = "z"; }
                    class W : HybridProbe {
                        W() : super() {}
                        void bad() { auto& r = value; r = "str"; }
                        void bad_direct() { this.value = "str"; }
                        void narrow_ref() { auto& r = narrow; r = 5000000000; }
                        void narrow_direct() { this.narrow = 5000000000; }
                    }
                    W w = W(); w.value = 5;
                )");
                ref_msg[idx] = caught(*e, "w.bad();");
                direct_msg[idx] = caught(*e, "w.bad_direct();");
                check_true(!ref_msg[idx].empty(), "a mistyped store through the reference raises");
                check_eq((int64_t)5, probe_of(*e, "w")->value, "and leaves the member alone");
                narrow_ref[idx] = caught(*e, "w.narrow_ref();");
                check_eq((int32_t)(5000000000LL), probe_of(*e, "w")->narrow);
                narrow_direct[idx] = caught(*e, "w.narrow_direct();");
                ++idx;
            }
            check_eq(ref_msg[0], ref_msg[1], "mistyped-store text is byte-identical across backends");
            check_eq(direct_msg[0], direct_msg[1]);
            check_eq(direct_msg[0], ref_msg[0], "the reference raises the direct store's own conversion text");
            // Narrow widths: the CPP-MEMBER box stores like an add_global_ref int32 (a
            // silent truncation) where the direct setter range-checks ("Integer value out
            // of range for int") - pinned as observed; flagged for a Dev ruling.
            check_eq(narrow_ref[0], narrow_ref[1], "narrow-int text is byte-identical across backends");
            check_eq(narrow_direct[0], narrow_direct[1]);
            check_true(narrow_ref[0].empty(), "reference store into int32 truncates (bound-variable rule)");
            check_true(narrow_direct[0].find("out of range") != std::string::npos, narrow_direct[0]);
        });
        test("method_name_still_not_bindable", [this, propref_engine, caught]() {
            std::string decl_msg[2], arg_msg[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->static_checking(check_mode::off);
                e->execute(R"(
                    void by_ref(int& p) { p = 1; }
                    class W : HybridProbe {
                        W() : super() {}
                        void decl() { auto& r = doubled; }
                        void arg() { by_ref(doubled); }
                    }
                    W w = W();
                )");
                decl_msg[idx] = caught(*e, "w.decl();");
                arg_msg[idx] = caught(*e, "w.arg();");
                check_true(decl_msg[idx].find("Cannot take reference of undefined variable") != std::string::npos, decl_msg[idx]);
                check_true(arg_msg[idx].find("Cannot take reference of undefined variable") != std::string::npos, arg_msg[idx]);
                ++idx;
            }
            check_eq(decl_msg[0], decl_msg[1]);
            check_eq(arg_msg[0], arg_msg[1]);
        });
        test("null_receiver_ref_texts_unchanged", [this, propref_engine, caught]() {
            std::string arg_msg[2], decl_msg[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->static_checking(check_mode::off);
                e->execute(R"(
                    void by_ref(int& p) { p = 1; }
                    HybridProbe n = null;
                )");
                arg_msg[idx] = caught(*e, "by_ref(n.value);");
                decl_msg[idx] = caught(*e, "auto& r = n.value;");
                check_true(!arg_msg[idx].empty() && !decl_msg[idx].empty(), "null receivers still raise");
                ++idx;
            }
            check_eq(arg_msg[0], arg_msg[1], "null-receiver ref-arg text is byte-identical across backends");
            check_eq(decl_msg[0], decl_msg[1], "null-receiver ref-decl text is byte-identical across backends");
        });
        test("by_ref_capture_of_cpp_property_is_live", [this, propref_engine]() {
            // [&] follows the field rule (cell-share the member); [=] keeps the getter snapshot
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->execute(R"(
                    class W : HybridProbe {
                        W() : super() {}
                        int live() { auto f = [&]() { return value; }; bump_cpp(); return f(); }
                        void write() { auto f = [&]() { value = 9; value += 1; }; f(); }
                        int snap() { auto f = [=]() { return value; }; bump_cpp(); return f(); }
                    }
                    W w = W(); w.value = 1;
                )");
                auto cpp = probe_of(*e, "w");
                check_eq((int64_t)101, e->execute("w.live()").as_int(), "[&] sees the C++-side change");
                e->execute("w.write();");
                check_eq((int64_t)10, cpp->value, "[&] writes land in the member");
                check_eq((int64_t)10, e->execute("w.snap()").as_int(), "[=] still snapshots");
                check_eq((int64_t)110, cpp->value);
            }
        });
        test("script_accessor_override_binds_through_accessors", [this, propref_engine]() {
            // A script _set_/_get_ override in the chain wins (row 29): the reference routes
            // through it instead of aiming at the member
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->execute(R"(
                    void by_ref(int& p) { p = 4; }
                    class W : HybridProbe {
                        int sets = 0;
                        W() : super() {}
                        void _set_value(int v) override { sets += 1; this.name = "set"; }
                        void go() { by_ref(value); auto& r = value; r = 5; }
                    }
                    W w = W(); w.go();
                )");
                auto cpp = probe_of(*e, "w");
                check_eq((int64_t)2, e->execute("w.sets").as_int(), "both stores went through the script setter");
                check_eq(std::string("set"), cpp->name);
                check_eq((int64_t)0, cpp->value, "the member was never written directly");
            }
        });
        test("property_ref_follows_reload_shadowing_field", [this, propref_engine]() {
            // A live PROPERTY ref re-resolves per touch: a reload that adds a same-named
            // script field re-points it at that field (the shadow rule, late); a live
            // CPP-MEMBER ref keeps aiming at the C++ member (its address is stable)
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->execute(R"(
                    class W : HybridProbe { W() : super() {} }
                    W w = W(); w.value = 1; w.lam = 2;
                    auto& lr = w.lam;
                    auto& vr = w.value;
                    lr = 3; vr = 4;
                )");
                auto cpp = probe_of(*e, "w");
                check_eq((int64_t)3, cpp->lam_backing);
                check_eq((int64_t)4, cpp->value);
                e->execute("class W : HybridProbe { int lam = 50; int value = 60; W() : super() {} }");
                check_eq((int64_t)50, e->execute("lr").as_int(), "the PROPERTY ref now reads the shadowing script field");
                e->execute("lr = 51;");
                check_eq((int64_t)51, e->execute("w.lam").as_int(), "and writes it");
                check_eq((int64_t)3, cpp->lam_backing, "the C++ side is untouched");
                e->execute("vr = 5;");
                check_eq((int64_t)5, cpp->value, "the CPP-MEMBER ref still writes the member");
                check_eq((int64_t)60, e->execute("w.value").as_int(), "while the spelling now reads the script field");
            }
        });
        test("property_refs_inside_lambdas_and_cast", [this, propref_engine, caught]() {
            // Bare lambdas made in a hybrid method reach the property through the env's
            // 'this' for every ref spelling; cast<int&> binds it too. Explicit [&name] /
            // [name] captures of a property keep today's undefined-capture text (they have
            // no member rung - unchanged, byte-identical).
            std::string ex_ref[2], ex_val[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = propref_engine(use_vm);
                e->static_checking(check_mode::off);
                e->execute(R"(
                    void by_ref(int& p) { p += 1; }
                    class W : HybridProbe {
                        W() : super() {}
                        void lam_arg() { auto f = []() { by_ref(value); }; f(); }
                        void lam_decl() { auto f = []() { auto& r = value; r += 1; }; f(); }
                        void cast_ref() { auto& r = cast<int&>(value); r = 40; }
                        int ex_ref() { auto f = [&value]() { return value; }; return f(); }
                        int ex_val() { auto f = [value]() { return value; }; return f(); }
                    }
                    W w = W(); w.value = 1;
                )");
                auto cpp = probe_of(*e, "w");
                e->execute("w.lam_arg();");
                check_eq((int64_t)2, cpp->value, "ref arg inside a bare lambda binds the member");
                e->execute("w.lam_decl();");
                check_eq((int64_t)3, cpp->value, "ref decl inside a bare lambda binds the member");
                e->execute("w.cast_ref();");
                check_eq((int64_t)40, cpp->value, "cast<int&> binds the member");
                ex_ref[idx] = caught(*e, "w.ex_ref()");
                ex_val[idx] = caught(*e, "w.ex_val()");
                check_true(ex_ref[idx].find("Cannot capture undefined variable 'value'") != std::string::npos, ex_ref[idx]);
                check_true(ex_val[idx].find("Cannot capture undefined variable 'value'") != std::string::npos, ex_val[idx]);
                ++idx;
            }
            check_eq(ex_ref[0], ex_ref[1]);
            check_eq(ex_val[0], ex_val[1]);
        });
        test("hybrid_two_level_chain_bare_cpp_property", [this]() {
            auto e = make_engine();
            bind_hybrid_probe(*e);
            e->execute(R"(
                class Kind : HybridProbe { Kind() : super() {} int kv() { return value; } }
                class W2 : Kind {
                    W2() : super() {}
                    int v() { return value; }
                    void set(int x) { value = x; }
                    void bump() { value += 10; }
                }
                W2 w = W2(); w.set(3);
            )");
            check_eq((int64_t)3, e->execute("w.v()").as_int());
            check_eq((int64_t)3, e->execute("w.kv()").as_int());
            check_eq((int64_t)3, e->execute("w.value").as_int());
            check_eq((int64_t)6, e->execute("w.doubled()").as_int());
            check_eq((int64_t)13, e->execute("w.bump(); w.value").as_int());
            check_false(e->execute("w").get_class_instance()->has_user_overflow_fields());
        });
        test("arity_text_numeric_through_computed_callees", [this]() {
            const char* calls[] = {
                "f(1)", "s(1)", "fs[0](1)", "pick()(1)",
                "g(1)", "sg(1)", "gs[0](1)", "pickg()(1)",
                "g(1,2,3)", "sg(1,2,3)", "gs[0](1,2,3)", "pickg()(1,2,3)"};
            const char* expected[] = {
                "'f' expects 0 arguments, got 1",
                "'g' expects 2 arguments, got 1",
                "'g' expects 2 arguments, got 3"};
            std::string texts[2][12];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->static_checking(check_mode::off);   // strict rejects these at load; the RUNTIME text is under test
                e->execute(R"(
                    void f() {}
                    void g(int a, int b) {}
                    var pick() { return f; }
                    var pickg() { return g; }
                    var s = f; var fs = [f];
                    var sg = g; var gs = [g];
                    string r = "ok";
                )");
                for (int i = 0; i < 12; ++i) {
                    texts[idx][i] = e->execute(std::string("r = \"ok\"; try { ") + calls[i] + "; } catch (err) { r = err; } r").as_string();
                    check_eq(std::string(expected[i / 4]), texts[idx][i], calls[i]);
                    check_eq(std::string::npos, texts[idx][i].find("this"), calls[i]);
                    check_eq(std::string::npos, texts[idx][i].find("class_definition"), calls[i]);
                }
                ++idx;
            }
            for (int i = 0; i < 12; ++i) { check_eq(texts[0][i], texts[1][i], calls[i]); }
        });
        test("arity_text_defaults_lambdas_coroutines", [this]() {
            struct row { const char* call; const char* text; };
            const row rows[] = {
                {"h()", "'h' expects 2 arguments, got 0"},
                {"sh()", "'h' expects 2 arguments, got 0"},
                {"h(1,2,3)", "'h' expects 2 arguments, got 3"},
                {"sh(1,2,3)", "'h' expects 2 arguments, got 3"},
                {"h(1)", "ok"},
                {"sh(1)", "ok"},
                {"lam()", "lambda expects 1 argument, got 0"},
                {"lam(1,2)", "lambda expects 1 argument, got 2"},
                {"auto c = co(); c.resume()", "'co' expects 1 argument, got 0"}};
            std::string texts[2][9];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->static_checking(check_mode::off);
                e->execute(R"(
                    void h(int a, int b = 2) {}
                    var sh = h;
                    var lam = [](int a) { return a; };
                    coroutine int co(int a) { yield a; return 0; }
                    string r = "ok";
                )");
                for (int i = 0; i < 9; ++i) {
                    texts[idx][i] = e->execute(std::string("r = \"ok\"; try { ") + rows[i].call + "; } catch (err) { r = err; } r").as_string();
                    check_eq(std::string(rows[i].text), texts[idx][i], rows[i].call);
                }
                ++idx;
            }
            for (int i = 0; i < 9; ++i) { check_eq(texts[0][i], texts[1][i], rows[i].call); }
        });
        test("arity_text_uncaught_prefix", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->static_checking(check_mode::off);
                bool threw = false;
                std::string msg;
                try { e->execute("void f() {} f(1);"); } catch (const std::exception& ex) { threw = true; msg = ex.what(); }
                check_true(threw);
                check_eq((size_t)0, msg.find("'f' expects 0 arguments, got 1"), msg);
            }
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

        // ---- math::wrap_add / wrap_sub / wrap_mul: explicit mod-2^64 arithmetic,
        // POLICY-INDEPENDENT (identical in checked and wrap overflow builds) — the
        // sanctioned replacement for the 16-bit limb products GLOOM/demoreel/crawler
        // hand-roll around the checked '*'. No throw_on_overflow() gate anywhere here:
        // unconditional values ARE the policy-independence pin. ----
        test("wrap_trio_values", [this]() {
            auto e = make_engine();
            check_eq((int64_t)INT64_MIN, e->execute("math::wrap_add(9223372036854775807, 1)").as_int());
            check_eq((int64_t)INT64_MAX, e->execute("math::wrap_sub(-9223372036854775807 - 1, 1)").as_int());
            check_eq((int64_t)-2, e->execute("math::wrap_mul(9223372036854775807, 2)").as_int());
            check_eq((int64_t)1, e->execute("math::wrap_mul(-1, -1)").as_int());
            check_eq((int64_t)8589934593LL, e->execute("math::wrap_mul(4294967297, 4294967297)").as_int());   // (2^32+1)^2 mod 2^64 = 2^33+1
            // Agrees exactly with checked ops when nothing overflows
            check_eq((int64_t)4000000000LL, e->execute("math::wrap_mul(2000000000, 2)").as_int());
            check_eq((int64_t)-5, e->execute("math::wrap_add(-2, -3)").as_int());
        });
        // Equivalence proof against INDEPENDENT arithmetic: a 16-bit limb multiply
        // (crawler.jai's u64_mul shape — masked columns, no op ever near overflow)
        // must agree with wrap_mul across seeded full-range operands and edge pairs.
        test("wrap_mul_matches_limb_multiply", [this]() {
            auto e = make_engine();
            auto r = e->execute(R"(
                function limb_mul(int a, int b) -> int {
                    int a0 = a & 0xFFFF; int a1 = (a >> 16) & 0xFFFF; int a2 = (a >> 32) & 0xFFFF; int a3 = (a >> 48) & 0xFFFF;
                    int b0 = b & 0xFFFF; int b1 = (b >> 16) & 0xFFFF; int b2 = (b >> 32) & 0xFFFF; int b3 = (b >> 48) & 0xFFFF;
                    int c0 = a0 * b0;
                    int c1 = a1 * b0 + a0 * b1;
                    int c2 = a2 * b0 + a1 * b1 + a0 * b2;
                    int c3 = a3 * b0 + a2 * b1 + a1 * b2 + a0 * b3;
                    int r0 = c0 & 0xFFFF; int t1 = c1 + (c0 >> 16);
                    int r1 = t1 & 0xFFFF; int t2 = c2 + (t1 >> 16);
                    int r2 = t2 & 0xFFFF; int t3 = c3 + (t2 >> 16);
                    return r0 | (r1 << 16) | (r2 << 32) | ((t3 & 0xFFFF) << 48);
                }
                math::random_seed(666);
                auto ok = limb_mul(9223372036854775807, 2) == math::wrap_mul(9223372036854775807, 2) &&
                          limb_mul(-1, -1) == math::wrap_mul(-1, -1) &&
                          limb_mul(-9223372036854775807 - 1, 3) == math::wrap_mul(-9223372036854775807 - 1, 3);
                for (int i = 0; i < 20; ++i) {
                    auto a = math::random(-9223372036854775807 - 1, 9223372036854775807);
                    auto b = math::random(-9223372036854775807 - 1, 9223372036854775807);
                    if (limb_mul(a, b) != math::wrap_mul(a, b)) { ok = false; }
                }
                ok;
            )");
            check(r.as_bool());
        });
        test("wrap_trio_errors", [this]() {
            auto e = make_engine();
            check_throws([&]() { e->execute("math::wrap_add(1);"); });          // arity
            check_throws([&]() { e->execute("math::wrap_mul(1.5, 2);"); });     // float rejected
            check_throws([&]() { e->execute("math::wrap_sub(\"a\", 2);"); });   // type
            check_eq((int64_t)3, e->execute("1 + 2").as_int());                 // engine stays usable
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

// Char integral promotion (Dev ruling 2026-07-12, the DOOM WAD-parser feedback):
// a char operand enters ARITHMETIC and BITWISE binary operators as int64 in 0..255
// (unsigned by spec). Strictly additive: char+string concat, native char comparisons,
// and object custom operators keep their pre-promotion meaning. One shared kernel,
// detail/char_promotion.hpp, feeds both backends.
class char_promotion_tests : public suite {
public:
    char_promotion_tests() : suite("Char Promotion") {}

    void forge_tests() override {
        test("char_minus_char", [this]() {
            auto e = make_engine();
            check_eq((int64_t)25, e->execute("'z' - 'a'").as_int());
        });
        test("char_plus_int", [this]() {
            auto e = make_engine();
            auto r = e->execute("'a' + 1");
            check(r.is_int());
            check_eq((int64_t)98, r.as_int());
        });
        test("digit_parse_shape", [this]() {
            auto e = make_engine();
            check_eq((int64_t)42, e->execute(R"(
                var s = "42";
                (s[0] - '0') * 10 + (s[1] - '0')
            )").as_int());
        });
        test("typed_char_local_promotes", [this]() {
            auto e = make_engine();
            check_eq((int64_t)1, e->execute("char c = 'b'; c - 'a'").as_int());
        });
        test("char_bitwise_and_mask", [this]() {
            auto e = make_engine();
            check_eq((int64_t)0, e->execute("'A' & 0x20").as_int());
            check_eq((int64_t)32, e->execute("'a' & 0x20").as_int());
            check_eq((int64_t)96, e->execute("'a' & 'b'").as_int());
        });
        test("char_shift_and_modulo", [this]() {
            auto e = make_engine();
            check_eq((int64_t)194, e->execute("'a' << 1").as_int());
            check_eq((int64_t)1, e->execute("'a' % 2").as_int());
        });
        test("char_float_mix_promotes_to_float", [this]() {
            auto e = make_engine();
            check_near(145.5, e->execute("'a' * 1.5").as_float(), 1e-9);
        });
        test("unsigned_high_bit_byte", [this]() {
            auto e = make_engine();
            // Script string contains one raw 0xC3 byte; s[0] must promote as 195, never -61.
            check_eq((int64_t)195, e->execute("var s = \"\xC3\"; s[0] + 0").as_int());
            check_eq((int64_t)195, e->execute("var s = \"\xC3\"; s[0] * 1").as_int());
        });
        test("compound_assign_through_promotion", [this]() {
            auto e = make_engine();
            check_eq((int64_t)102, e->execute("var x = 5; x += 'a'; x").as_int());
        });
        // The additive guarantees: everything that worked before promotion still means
        // the same thing.
        test("string_concat_preserved", [this]() {
            auto e = make_engine();
            check_eq(std::string("prex"), e->execute("\"pre\" + 'x'").as_string());
            check_eq(std::string("xpost"), e->execute("'x' + \"post\"").as_string());
        });
        test("no_implicit_int_to_char", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(e);
            // The promoted result is an int; it does not sneak back into the char domain.
            check_eq(std::string("int"), e->execute("type_of('a' + 1)").as_string());
            check_eq(std::string("char"), e->execute("type_of('a')").as_string());
        });
        // Comparison inting (second ruling, same day): comparisons promote exactly like
        // arithmetic, so the char domain has ONE rule instead of two.
        test("char_equals_int", [this]() {
            auto e = make_engine();
            check(e->execute("'a' == 97").as_bool());
            check(e->execute("97 == 'a'").as_bool());
            check(e->execute("'a' != 98").as_bool());
            check_false(e->execute("'a' == 98").as_bool());
            // The pre-ruling silent-false binary idiom, now truthful:
            check(e->execute("var s = \"\\x1A\"; s[0] == 0x1A").as_bool());
        });
        test("char_equals_string_stays_false", [this]() {
            auto e = make_engine();
            // Promotion never crosses into the string domain.
            check_false(e->execute("'a' == \"a\"").as_bool());
        });
        test("char_ordered_vs_int", [this]() {
            auto e = make_engine();
            check(e->execute("'a' < 98").as_bool());
            check(e->execute("'a' >= 97").as_bool());
            check(e->execute("64 < 'A'").as_bool());
            check(e->execute("'a' < 'b'").as_bool());
            check(e->execute("'a' == 'a'").as_bool());
        });
        test("char_spaceship_promotes", [this]() {
            auto e = make_engine();
            check_eq((int64_t)1, e->execute("'a' <=> 65").as_int());
            check_eq((int64_t)0, e->execute("'b' <=> 'b'").as_int());
            check_eq((int64_t)-1, e->execute("'A' <=> 'a'").as_int());
        });
        test("high_bit_comparisons_unsigned", [this]() {
            auto e = make_engine();
            // Byte 0xC3 must order as 195, not -61 — signed-native ordering is gone.
            check(e->execute("var s = \"\\xC3\"; s[0] > 'a'").as_bool());
            check(e->execute("var s = \"\\xC3\"; s[0] > 127").as_bool());
            check_eq((int64_t)1, e->execute("var s = \"\\xC3\"; s[0] <=> 'a'").as_int());
        });
        // Byte minting: \xNN escapes (exactly two hex digits) + stdlib to_char.
        test("hex_escapes_in_literals", [this]() {
            auto e = make_engine();
            check_eq(std::string("AB"), e->execute("\"\\x41\\x42\"").as_string());
            check(e->execute("'\\x41' == 'A'").as_bool());
            check_eq((int64_t)0, e->execute("\"\\x00\"[0] + 0").as_int());
            check_eq((int64_t)3, e->execute("\"a\\x00b\".size()").as_int());
            // Exactly two digits: the third hex char is literal text, not part of the escape.
            check_eq(std::string("A5"), e->execute("\"\\x415\"").as_string());
        });
        test("hex_escape_malformed_rejected", [this]() {
            auto e = make_engine();
            check_throws([&]() { e->execute("\"\\xZZ\""); });
            check_throws([&]() { e->execute("\"\\x4\""); });
            check_eq((int64_t)3, e->execute("1 + 2").as_int());   // engine stays usable
        });
        test("to_char_mints_bytes", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(e);
            check(e->execute("to_char(65) == 'A'").as_bool());
            check_eq(std::string("char"), e->execute("type_of(to_char(200))").as_string());
            check_eq((int64_t)200, e->execute("to_char(200) + 0").as_int());
            // The BMP-writer shape: computed bytes append onto a binary string.
            check_eq(std::string("BC"), e->execute("var out = \"\"; out += to_char(66); out += to_char(67); out").as_string());
            check_throws([&]() { e->execute("to_char(256)"); });
            check_throws([&]() { e->execute("to_char(-1)"); });
        });
    }
};

// Constructor-call semantics pinned around the in-loop ctor-body entry (op_new slice
// B): the body runs on the dispatch loop for vm class/shared_ptr mints and direct
// class-name calls; every observable below must read identically on both backends and
// match the pre-slice probes.
class constructor_semantics_tests : public suite {
public:
    constructor_semantics_tests() : suite("Constructor Semantics") {}
    void forge_tests() override {
        test("ctor_falloff_returns_instance", [this]() {
            auto e = make_engine();
            check_eq((int64_t)5, e->execute("class A { int x = 1; A() { x = 5; } } auto a = A(); a.x").as_int());
        });
        test("ctor_explicit_valued_return_wins", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(e);
            // Pinned by probe before the in-loop entry existed: `return 99` in a ctor
            // yields 99 (an int) to the call site, not the instance.
            check_eq(std::string("int"), e->execute(
                "class B { int x = 2; B() { x = 7; return 99; } } type_of(B())").as_string());
            check_eq((int64_t)99, e->execute(
                "class B { int x = 2; B() { x = 7; return 99; } } B() + 0").as_int());
        });
        test("ctor_bare_return_yields_null", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(e);
            // Also pinned by probe: bare `return;` yields null (errors at typed use)
            check_eq(std::string("null"), e->execute(
                "class A { int x = 1; A() { return; } } type_of(A())").as_string());
        });
        test("shared_ptr_mint_stamps_type", [this]() {
            auto e = make_engine();
            jai::stdlib::register_all(e);
            // type_of reports the value category ("object") on both backends; the
            // shared_ptr stamp's observable is weak_ptr minting from the result
            check_eq(std::string("object"), e->execute(
                "class C { int x = 3; C(int v) { x = v; } } var c = new shared_ptr<C>(9); type_of(c)").as_string());
            check_eq((int64_t)9, e->execute(
                "class C { int x = 3; C(int v) { x = v; } } var c = new shared_ptr<C>(9); c.x").as_int());
        });
        test("ctor_throw_is_catchable_at_call_site", [this]() {
            auto e = make_engine();
            check_eq(std::string("caught:boom"), e->execute(R"(
                class T { T() { throw "boom"; } }
                var msg = "";
                try { var t = T(); } catch (ex) { msg = "caught:" + ex; }
                msg
            )").as_string());
        });
        test("ctor_super_and_delegation_compose", [this]() {
            auto e = make_engine();
            // 43 = the pre-slice both-backend answer (pinned, not derived): the
            // delegation runs the target's body, then the delegating body composes
            check_eq((int64_t)43, e->execute(R"(
                class Base { int b = 0; Base(int v) { b = v; } }
                class D : Base {
                    int d = 0;
                    D(int v) : super(v * 2) { d = v; }
                    D() : this(3) { d = d + 40; }
                }
                auto x = D();
                x.b + x.d
            )").as_int());
            // super alone, no delegation: both fields observable
            check_eq((int64_t)9, e->execute(R"(
                class Base { int b = 0; Base(int v) { b = v; } }
                class E : Base { int d = 0; E(int v) : super(v * 2) { d = v; } }
                auto x = E(3);
                x.b + x.d
            )").as_int());
        });
        test("ctor_recursion_in_body", [this]() {
            auto e = make_engine();
            // A ctor body calling the class recursively exercises reentrant in-loop
            // ctor frames (the transient-function/window discipline)
            check_eq((int64_t)3, e->execute(R"(
                class N {
                    int depth = 0;
                    N(int d) {
                        depth = d;
                        if (d > 0) { auto child = N(d - 1); depth = child.depth + 1; }
                    }
                }
                N(3).depth
            )").as_int());
        });
    }
};

} // namespace jai::foundry::tests

using constructor_semantics_tests = jai::foundry::tests::constructor_semantics_tests;
FOUNDRY_REGISTER(constructor_semantics_tests)
using char_promotion_tests = jai::foundry::tests::char_promotion_tests;
FOUNDRY_REGISTER(char_promotion_tests)
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
