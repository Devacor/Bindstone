// Opt-in static type checking (docs/static_checking.md): mode API, the type-ladder
// rule table, accumulation/recovery/cap, strict gating (throws before anything runs),
// warn retrieval, hot-reload + registration-epoch re-checks, jaibite stamping, and a
// generator-corpus false-positive gate (false positives are the feature's death).

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>

#include "fuzz/fuzz_generator.hpp"

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

namespace {

struct probe_object {
    int64_t value = 0;
    int64_t doubled() const { return value * 2; }
};

size_t error_count(const check_report& r) { return r.error_count(); }
size_t warning_count(const check_report& r) {
    return r.diagnostics.size() - r.error_count();
}

} // namespace

class static_check_tests : public suite {
public:
    static_check_tests() : suite("Static Type Checking") {}

    void forge_tests() override {
        // ------------------------------------------------------------ mode API

        test("off_by_default_and_mode_roundtrip", [this]() {
            auto e = make_engine();
            check_true(e->static_checking() == check_mode::off);
            e->static_checking(check_mode::warn);
            check_true(e->static_checking() == check_mode::warn);
            e->static_checking(check_mode::strict);
            check_true(e->static_checking() == check_mode::strict);
            e->static_checking(check_mode::off);
        });

        test("off_mode_zero_behavior_change_battery", [this]() {
            // Scripts that the checker WOULD flag must run byte-identically under off.
            auto e = make_engine();
            check_true(e->static_checking() == check_mode::off);
            check_eq((int64_t)4, e->execute("int x = 4.7; x").as_int());
            check_eq(std::string("42"), e->execute("string s = 42; s").as<std::string>());
            check_eq(true, e->execute("bool b = \"x\"; b").as<bool>());
            check_eq((int64_t)7, e->execute("var v = 5; v = \"s\"; 7").as_int());
            check_eq((int64_t)10, e->execute("function ten() { return 10; } ten()").as_int());
            // and no diagnostics were collected anywhere
            check_eq((size_t)0, e->last_check_diagnostics().diagnostics.size());
        });

        test("check_is_independent_of_mode", [this]() {
            auto e = make_engine();   // off
            auto report = e->check("int x = \"hello\";");
            check_eq((size_t)1, error_count(report));
            check_eq((size_t)1, report.diagnostics[0].line);
        });

        // ------------------------------------------------------------ runtime parity probes
        // The checker's error set must be a subset of runtime failures (plus the
        // documented declared-intent rules). These probes pin the runtime behaviors
        // the rule table mirrors; if one fails, the table drifted.

        test("runtime_parity_probes", [this]() {
            auto e = make_engine();
            // conversions the checker must ACCEPT
            check_eq((int64_t)4, e->execute("int a = 4.7; a").as_int());
            check_eq((int64_t)1, e->execute("int b = true; b").as_int());
            check_eq(std::string("42"), e->execute("string c = 42; c").as<std::string>());
            check_eq(true, e->execute("bool d = \"x\"; d").as<bool>());
            check_near(2.0, e->execute("float f = 2; f").as_float(), 0.0001);
            // violations the checker flags must FAIL at runtime too
            check_throws([&]() { e->execute("int p1 = \"s\";"); });
            check_throws([&]() { e->execute("int p2 = null;"); });
            check_throws([&]() { e->execute("int p3 = 'c';"); });
            check_throws([&]() { e->execute("char p4 = 65;"); });
            check_throws([&]() { e->execute("function tp(int a) { return a; } tp(\"s\");"); });
        });

        // ------------------------------------------------------------ the type ladder

        test("ladder_typed_decls", [this]() {
            auto e = make_engine();
            check_eq((size_t)0, error_count(e->check("int x = 5;")));
            check_eq((size_t)0, error_count(e->check("int x = 4.7;")));      // converts
            check_eq((size_t)0, error_count(e->check("int x = true;")));     // converts
            check_eq((size_t)0, error_count(e->check("float f = 2;")));
            check_eq((size_t)0, error_count(e->check("string s = 42;")));    // to_string
            check_eq((size_t)0, error_count(e->check("bool b = \"x\";")));   // truthy
            check_eq((size_t)0, error_count(e->check("int x;")));            // null init is fine
            check_eq((size_t)1, error_count(e->check("int x = \"s\";")));
            check_eq((size_t)1, error_count(e->check("int x = null;")));
            check_eq((size_t)1, error_count(e->check("int x = 'c';")));
            check_eq((size_t)1, error_count(e->check("char c = 65;")));
            check_eq((size_t)1, error_count(e->check("int x = [1, 2];")));
        });

        test("ladder_assignment_to_typed", [this]() {
            auto e = make_engine();
            check_eq((size_t)0, error_count(e->check("int x = 1; x = 2.5;")));
            check_eq((size_t)1, error_count(e->check("int x = 1; x = \"s\";")));
            auto r = e->check("int declared_here = 1;\ndeclared_here = \"s\";");
            check_eq((size_t)1, error_count(r));
            check_eq((size_t)2, r.diagnostics[0].line);
            check_eq((size_t)1, r.diagnostics[0].related_line);   // points at the declaration
        });

        test("ladder_auto_infers_then_enforces", [this]() {
            auto e = make_engine();
            check_eq((size_t)0, error_count(e->check("auto x = 5; x = 7;")));
            check_eq((size_t)1, error_count(e->check("auto x = 5; x = \"s\";")));
            // auto without initializer locks at first runtime assignment: the checker
            // can't see branch order, so it stays dynamic (documented leniency)
            check_eq((size_t)0, error_count(e->check("auto x; x = 5; x = \"s\";")));
        });

        test("ladder_var_is_the_opt_out", [this]() {
            auto e = make_engine();
            check_eq((size_t)0, error_count(e->check("var v = 5; v = \"s\";")));
            // and the opt-out propagates: expressions touching var become unknown
            check_eq((size_t)0, error_count(e->check("var v = \"s\"; int x = v;")));
            check_eq((size_t)0, error_count(e->check("var v = 1; int x = v + 1;")));
            check_eq((size_t)0, error_count(e->check("var v = 5; v += \"s\";")));
        });

        test("compound_assign_follows_cpp_rules", [this]() {
            // Dev ruling: `x op= rhs` on a typed target is `x = T(x op rhs)` — numeric
            // conversions are fine, a string result into an int target is not.
            auto e = make_engine();
            check_eq((size_t)0, error_count(e->check("int x = 1; x += 2.5;")));
            check_eq((size_t)0, error_count(e->check("int x = 4; x /= 2;")));
            check_eq((size_t)0, error_count(e->check("float f = 1.0; f *= 2;")));
            check_eq((size_t)0, error_count(e->check("string s = \"a\"; s += 5;")));
            check_eq((size_t)1, error_count(e->check("int x = 1; x += \"s\";")));
        });

        // ------------------------------------------------------------ identifiers

        test("undeclared_identifier_is_an_error", [this]() {
            auto e = make_engine();
            check_eq((size_t)1, error_count(e->check("zzz_nope + 1;")));
            check_eq((size_t)1, error_count(e->check("zzz_nope();")));
            check_eq((size_t)1, error_count(e->check("zzz_nope = 5;")));
            check_eq((size_t)0, error_count(e->check("print(\"hi\");")));   // registered surface visible
        });

        test("includes_downgrade_undeclared_to_warning", [this]() {
            auto e = make_engine();
            auto r = e->check("include \"somewhere.jai\"\nzzz_maybe_included();");
            check_eq((size_t)0, error_count(r));
            check_ge(warning_count(r), (size_t)1);
        });

        test("forward_references_resolve", [this]() {
            auto e = make_engine();
            check_eq((size_t)0, error_count(e->check(
                "function a() { return b(); }\nfunction b() { return 41; }\na();")));
            check_eq((size_t)0, error_count(e->check(
                "function make_it() { return Later(); }\nclass Later { int v = 7; }\nmake_it();")));
            // later-declared locals referenced from nested function bodies stay silent
            check_eq((size_t)0, error_count(e->check(
                "function outer() { function inner() { return late_var; } int late_var = 5; return inner(); }")));
        });

        // ------------------------------------------------------------ script function calls

        test("script_function_arity_and_types", [this]() {
            auto e = make_engine();
            const char* fn = "function add(int a, int b) { return a + b; }\n";
            check_eq((size_t)0, error_count(e->check(std::string(fn) + "add(1, 2);")));
            check_eq((size_t)0, error_count(e->check(std::string(fn) + "add(1.5, 2);")));   // converts
            check_eq((size_t)1, error_count(e->check(std::string(fn) + "add(1);")));
            check_eq((size_t)1, error_count(e->check(std::string(fn) + "add(1, 2, 3);")));
            check_eq((size_t)1, error_count(e->check(std::string(fn) + "add(\"x\", 2);")));
            // var/unknown args match anything
            check_eq((size_t)0, error_count(e->check(std::string(fn) + "var v = \"x\"; add(v, 2);")));
        });

        test("script_function_defaults_and_overloads", [this]() {
            auto e = make_engine();
            const char* fn = "function d(int a, int b = 2) { return a + b; }\n";
            check_eq((size_t)0, error_count(e->check(std::string(fn) + "d(1);")));
            check_eq((size_t)0, error_count(e->check(std::string(fn) + "d(1, 5);")));
            check_eq((size_t)1, error_count(e->check(std::string(fn) + "d();")));
            // overloads: error only when NO overload matches (string params take anything
            // via to_string, so pair types that both reject a string argument)
            const char* ov = "function pick(int a) { return 1; }\nfunction pick(char c) { return 2; }\n";
            check_eq((size_t)0, error_count(e->check(std::string(ov) + "pick(1);")));
            check_eq((size_t)0, error_count(e->check(std::string(ov) + "pick('c');")));
            check_eq((size_t)1, error_count(e->check(std::string(ov) + "pick(\"s\");")));
            // a string overload makes anything viable
            const char* ov2 = "function show(int a) { return 1; }\nfunction show(string s) { return 2; }\n";
            check_eq((size_t)0, error_count(e->check(std::string(ov2) + "show([1]);")));
        });

        test("return_type_checking", [this]() {
            auto e = make_engine();
            check_eq((size_t)0, error_count(e->check("int f() { return 5; } f();")));
            check_eq((size_t)0, error_count(e->check("int f() { return 2.5; } f();")));    // converts
            check_eq((size_t)0, error_count(e->check("string f() { return 5; } f();")));   // to_string
            check_eq((size_t)1, error_count(e->check("int f() { return \"s\"; } f();")));
            check_eq((size_t)1, error_count(e->check("void f() { return 5; } f();")));
            // typed fn falling off the end: warning severity in v1
            auto falls = e->check("int f() { if (false) { return 1; } } f();");
            check_eq((size_t)0, error_count(falls));
            check_ge(warning_count(falls), (size_t)1);
            auto clean = e->check("int f() { if (false) { return 1; } return 2; } f();");
            check_eq((size_t)0, clean.diagnostics.size());
            // while(true) loops that can only exit via return don't warn
            auto spin = e->check("int f() { while (true) { return 1; } } f();");
            check_eq((size_t)0, spin.diagnostics.size());
        });

        test("coroutines_stay_lenient", [this]() {
            auto e = make_engine();
            // a coroutine's returns/yields aren't held to the declared type in v1, and
            // calling one yields a handle, not the declared type
            check_eq((size_t)0, error_count(e->check(
                "coroutine int gen() { yield 1; yield 2; }\nauto g = gen();")));
        });

        // ------------------------------------------------------------ host surface

        test("host_function_signature_checking", [this]() {
            auto e = make_engine();
            e->add_function("square_probe", [](script_int n) -> script_int { return n * n; });
            check_eq((size_t)0, error_count(e->check("square_probe(4);")));
            check_eq((size_t)0, error_count(e->check("square_probe(2.5);")));   // numeric conversion
            check_eq((size_t)1, error_count(e->check("square_probe(\"s\");")));
            check_eq((size_t)1, error_count(e->check("square_probe(1, 2);")));
            check_eq((size_t)1, error_count(e->check("square_probe();")));
            // variadics accept anything
            check_eq((size_t)0, error_count(e->check("print(1, \"x\", [2]);")));
            // and the runtime agrees with the rejections
            check_throws([&]() { e->execute("square_probe(\"s\");"); });
        });

        test("host_class_member_checking", [this]() {
            auto e = make_engine();
            dynamic_binder<probe_object>(*e, "ProbeObject")
                .constructor<>()
                .method("doubled", &probe_object::doubled)
                .property("value", &probe_object::value)
                .build();
            const char* mk = "ProbeObject p = ProbeObject();\n";
            check_eq((size_t)0, error_count(e->check(std::string(mk) + "p.doubled();")));
            check_eq((size_t)0, error_count(e->check(std::string(mk) + "p.value;")));
            check_eq((size_t)0, error_count(e->check(std::string(mk) + "p.value = 5;")));
            check_eq((size_t)1, error_count(e->check(std::string(mk) + "p.zzz_nope();")));
            check_eq((size_t)1, error_count(e->check(std::string(mk) + "p.zzz_nope;")));
        });

        // ------------------------------------------------------------ script classes

        test("script_class_members_and_field_types", [this]() {
            auto e = make_engine();
            const char* cls =
                "class Cat {\n"
                "    int lives = 9;\n"
                "    void meow() {}\n"
                "    int fed(int amount) { return lives + amount; }\n"
                "}\n"
                "Cat c = Cat();\n";
            check_eq((size_t)0, error_count(e->check(std::string(cls) + "c.lives = 5; c.meow(); c.fed(2);")));
            check_eq((size_t)1, error_count(e->check(std::string(cls) + "c.zzz_nope();")));
            check_eq((size_t)1, error_count(e->check(std::string(cls) + "c.zzz_nope = 1;")));
            // typed fields enforce like locals (Dev ruling: declared field types are real)
            check_eq((size_t)1, error_count(e->check(std::string(cls) + "c.lives = \"s\";")));
            check_eq((size_t)1, error_count(e->check(std::string(cls) + "c.fed(\"s\");")));
            check_eq((size_t)1, error_count(e->check(std::string(cls) + "c.meow(1);")));
            check_eq((size_t)1, error_count(e->check(
                "class Dog { int teeth = \"many\"; }")));   // field initializer violates declared type
            // methods see fields bare, and typed-field stores inside methods check too
            check_eq((size_t)0, error_count(e->check(
                "class A { int v = 0; void set(int nv) { v = nv; } }")));
            check_eq((size_t)1, error_count(e->check(
                "class A { int v = 0; void bad() { v = \"s\"; } }")));
        });

        test("script_class_constructors", [this]() {
            auto e = make_engine();
            const char* cls = "class Pt { int x = 0; Pt(int nx) { x = nx; } }\n";
            check_eq((size_t)0, error_count(e->check(std::string(cls) + "Pt p = Pt(3);")));
            check_eq((size_t)1, error_count(e->check(std::string(cls) + "Pt p = Pt();")));
            check_eq((size_t)1, error_count(e->check(std::string(cls) + "Pt p = Pt(\"s\");")));
            // no declared ctor: default + copy only
            const char* plain = "class Bare { int v = 1; }\n";
            check_eq((size_t)0, error_count(e->check(std::string(plain) + "Bare b = Bare();")));
            check_eq((size_t)1, error_count(e->check(std::string(plain) + "Bare b = Bare(1, 2);")));
        });

        test("inheritance_members_and_assignability", [this]() {
            auto e = make_engine();
            const char* hier =
                "class Animal { int hp = 10; void heal() { hp = hp + 1; } }\n"
                "class Tiger : Animal { void roar() {} }\n";
            check_eq((size_t)0, error_count(e->check(std::string(hier) +
                "Tiger t = Tiger(); t.roar(); t.heal(); t.hp = 5;")));
            check_eq((size_t)1, error_count(e->check(std::string(hier) +
                "Tiger t = Tiger(); t.zzz_nope();")));
            check_eq((size_t)1, error_count(e->check(std::string(hier) +
                "Tiger t = Tiger(); t.hp = \"s\";")));   // inherited typed field
            // derived -> base ok; base -> derived is not
            check_eq((size_t)0, error_count(e->check(std::string(hier) + "Animal a = Tiger();")));
            check_eq((size_t)1, error_count(e->check(std::string(hier) + "Tiger t = Animal();")));
            // unknown base class opens the hierarchy: no member errors
            check_eq((size_t)0, error_count(e->check(
                "include \"base.jai\"\nclass Sub : MysteryBase {}\nSub s = Sub();\ns.whatever();")));
        });

        // ------------------------------------------------------------ accumulation / recovery / cap

        test("accumulates_all_errors_sorted", [this]() {
            auto e = make_engine();
            auto r = e->check(
                "var ok = 0;\n"          // 1: clean
                "int b = \"x\";\n"       // 2: error
                "int c = \"y\";\n"       // 3: error
                "zzz_undefined();\n");   // 4: error
            check_eq((size_t)3, error_count(r));
            check_eq((size_t)2, r.diagnostics[0].line);
            check_eq((size_t)3, r.diagnostics[1].line);
            check_eq((size_t)4, r.diagnostics[2].line);
            check_gt(r.diagnostics[0].column, (size_t)0);
            // formatted output is compiler-style with source line + caret
            std::string formatted = r.format("var ok = 0;\nint b = \"x\";\nint c = \"y\";\nzzz_undefined();\n");
            check_true(formatted.find("script:2:") != std::string::npos);
            check_true(formatted.find("error:") != std::string::npos);
            check_true(formatted.find("int b = \"x\";") != std::string::npos);
            check_true(formatted.find("^") != std::string::npos);
        });

        test("recovery_no_cascades_and_dedupe", [this]() {
            auto e = make_engine();
            // after the bad init, x keeps its declared type: downstream uses stay clean
            auto r = e->check("int x = \"s\";\nint y = x + 1;\nint z = x * 2;");
            check_eq((size_t)1, error_count(r));
        });

        test("diagnostic_cap_at_100", [this]() {
            auto e = make_engine();
            std::string src;
            for (int i = 0; i < 120; ++i) {
                src += "int v" + std::to_string(i) + " = \"s\";\n";
            }
            auto r = e->check(src);
            check_eq(detail::k_check_diagnostic_cap, r.diagnostics.size());
            check_true(r.capped);
        });

        // ------------------------------------------------------------ strict / warn gating

        test("strict_throws_before_anything_runs", [this]() {
            auto e = make_engine();
            e->execute("var sentinel = 0;");
            e->static_checking(check_mode::strict);
            bool threw = false;
            std::string message;
            try {
                // the assignment line is checker-clean; the error is later — nothing
                // may run anyway (side-effect-free proof)
                e->execute("sentinel = 5;\nint oops = \"s\";");
            } catch (const static_check_error& err) {
                threw = true;
                message = err.what();
            }
            check_true(threw);
            check_true(message.find("error:") != std::string::npos);
            check_true(message.find("oops") != std::string::npos);
            e->static_checking(check_mode::off);
            check_eq((int64_t)0, e->execute("sentinel").as_int());
        });

        test("strict_allows_clean_scripts", [this]() {
            auto e = make_engine();
            e->static_checking(check_mode::strict);
            check_eq((int64_t)9, e->execute("int f(int v) { return v * 3; } f(3)").as_int());
        });

        test("warn_mode_executes_and_diagnostics_are_retrievable", [this]() {
            auto e = make_engine();
            e->static_checking(check_mode::warn);
            // checker-error in dead code: runtime-clean, still diagnosed
            auto result = e->execute("var flag = false;\nif (flag) { zzz_nope(); }\n7");
            check_eq((int64_t)7, result.as_int());
            check_true(e->last_check_diagnostics().has_errors());
            check_eq((size_t)2, e->last_check_diagnostics().diagnostics[0].line);
        });

        // ------------------------------------------------------------ hot reload / epoch

        test("registration_epoch_recheck", [this]() {
            auto e = make_engine();
            e->static_checking(check_mode::strict);
            bool threw = false;
            try { e->execute("mystery_fn_zz(1);"); } catch (const static_check_error&) { threw = true; }
            check_true(threw);
            // registering the function bumps the surface epoch: the SAME cached source
            // re-checks clean and runs
            e->add_function("mystery_fn_zz", [](script_int v) -> script_int { return v + 1; });
            check_eq((int64_t)2, e->execute("mystery_fn_zz(1)").as_int());
        });

        test("hot_reload_new_source_is_rechecked", [this]() {
            auto e = make_engine();
            e->static_checking(check_mode::strict);
            e->execute("class Hot { int v = 1; }\nHot h = Hot();\nh.v;");
            // reload with a changed class body (new source = new cache entry = checked):
            // the stale member access is caught against the class defined IN that source
            bool threw = false;
            try {
                e->execute("class Hot { int w = 2; }\nHot h = Hot();\nh.v;");
            } catch (const static_check_error&) {
                threw = true;
            }
            check_true(threw);
        });

        // ------------------------------------------------------------ jaibite

        test("jaibite_strict_gates_creation", [this]() {
            auto e = make_engine();
            e->static_checking(check_mode::strict);
            auto bite = e->jaibite("int a = 20; a * 2");
            check_true(bite.checked_clean());
            check_eq((int64_t)40, bite.execute().as_int());
            check_throws([&]() { e->jaibite("int bad = \"s\";"); });
        });

        test("jaibite_warn_carries_diagnostics", [this]() {
            auto e = make_engine();
            e->static_checking(check_mode::warn);
            auto bite = e->jaibite("int bad = \"s\";");
            check_false(bite.checked_clean());
            check_true(bite.check_diagnostics().has_errors());
            check_true(e->last_check_diagnostics().has_errors());
        });

        test("jaibite_save_load_stamp", [this]() {
            auto e = make_engine();
            e->static_checking(check_mode::strict);
            auto bite = e->jaibite("int a = 5; a + 1");
            check_true(bite.checked_clean());
            auto bytes = bite.save_bytes();

            // same engine: fingerprint matches, the checked-clean stamp is trusted
            auto loaded = e->jaibite_load_bytes(bytes);
            check_false(loaded.registration_mismatch());
            check_true(loaded.checked_clean());
            check_eq((int64_t)6, loaded.execute().as_int());

            // unchecked save (off mode) loads unchecked, then strict re-checks on load
            e->static_checking(check_mode::off);
            auto unchecked_bite = e->jaibite("int b = 7; b");
            check_false(unchecked_bite.checked_clean());
            auto unchecked_bytes = unchecked_bite.save_bytes();
            e->static_checking(check_mode::strict);
            auto reloaded = e->jaibite_load_bytes(unchecked_bytes);
            check_true(reloaded.checked_clean());   // re-checked at load

            // mismatched registration surface: stamp NOT trusted (off mode: stays unchecked)
            auto e2 = make_engine();
            e2->add_function("extra_reg_fn_zz", [](script_int v) -> script_int { return v; });
            auto loaded2 = e2->jaibite_load_bytes(bytes);
            check_true(loaded2.registration_mismatch());
            check_false(loaded2.checked_clean());

            // ...and a strict engine with a mismatch re-checks instead of trusting
            auto e3 = make_engine();
            e3->add_function("extra_reg_fn_zz", [](script_int v) -> script_int { return v; });
            e3->static_checking(check_mode::strict);
            auto loaded3 = e3->jaibite_load_bytes(bytes);
            check_true(loaded3.registration_mismatch());
            check_true(loaded3.checked_clean());
            check_eq((int64_t)6, loaded3.execute().as_int());

            // strict load of a bad bite throws at load, before anything runs
            e3->static_checking(check_mode::off);
            auto bad_bite = e3->jaibite("int bad = \"s\";");
            auto bad_bytes = bad_bite.save_bytes();
            e3->static_checking(check_mode::strict);
            check_throws([&]() { e3->jaibite_load_bytes(bad_bytes); });
        });

        test("jaibite_execute_checks_lazily", [this]() {
            auto e = make_engine();
            auto bite = e->jaibite("int bad = \"s\";");   // off: created unchecked
            check_false(bite.checked_clean());
            e->static_checking(check_mode::strict);
            check_throws([&]() { e->execute(bite); });    // checked at first strict execute
        });

        // ------------------------------------------------------------ misc surface

        test("lenient_surfaces_stay_silent", [this]() {
            auto e = make_engine();
            // builtin container/string methods, subscripts, maps, enums, destructuring
            check_eq((size_t)0, error_count(e->check(
                "array<int> a = [1, 2, 3];\na.push(4);\na[0] = 9;\nint n = a.size();")));
            check_eq((size_t)0, error_count(e->check(
                "map<string, int> m = {\"a\": 1};\nm[\"b\"] = 2;")));
            check_eq((size_t)0, error_count(e->check(
                "string s = \"abc\";\nint n = s.length();")));
            check_eq((size_t)0, error_count(e->check(
                "enum Direction { north, south }\nauto d = Direction.north;")));
            check_eq((size_t)1, error_count(e->check(
                "enum Direction { north, south }\nauto d = Direction.zzz_west;")));
            check_eq((size_t)0, error_count(e->check(
                "auto [x, y] = [1, 2];\nx + y;")));
            // lambdas: params + captures resolve, bodies get checked
            check_eq((size_t)0, error_count(e->check(
                "int base = 2;\nauto f = [=](int v) { return v + base; };\nf(3);")));
            check_eq((size_t)1, error_count(e->check(
                "auto f = [](int v) { int q = \"s\"; return v; };")));
        });

        test("ternary_and_operator_inference", [this]() {
            auto e = make_engine();
            check_eq((size_t)0, error_count(e->check("bool c = true; int x = c ? 1 : 2;")));
            check_eq((size_t)1, error_count(e->check("bool c = true; int x = c ? \"a\" : \"b\";")));
            check_eq((size_t)1, error_count(e->check("int x = 1 < 2;\nchar c = x;")));   // bool -> char? no: int decl from bool ok; char from int errors
            check_eq((size_t)1, error_count(e->check("char c = \"a\" + \"b\";")));       // string into char
            check_eq((size_t)0, error_count(e->check("string s = 1 + \"b\";")));
        });

        // ------------------------------------------------------------ shadowing warnings
        // Always WARNING severity (shadowing is legal; strict must not throw on it).

        test("shadow_local_over_field", [this]() {
            auto e = make_engine();
            auto r = e->check("class Creature {\n  int hp = 10;\n  function hit() -> void { var hp = 3; }\n}");
            check_eq((size_t)0, error_count(r));
            check_eq((size_t)1, warning_count(r));
            check_true(r.diagnostics[0].message.find(
                "local 'hp' shadows field 'hp' of class 'Creature' (field declared at 2:") != std::string::npos);
        });

        test("shadow_param_over_field_but_not_ctor_params", [this]() {
            auto e = make_engine();
            auto r = e->check("class C {\n  int hp = 1;\n  function heal(int hp) -> void { this.hp += hp; }\n}");
            check_eq((size_t)1, warning_count(r));
            check_true(r.diagnostics[0].message.find("parameter 'hp' shadows field 'hp'") != std::string::npos);
            // C(int hp) { this.hp = hp; } is the established idiom - silent
            check_eq((size_t)0, e->check(
                "class C {\n  int hp = 1;\n  C(int hp) { this.hp = hp; }\n}").diagnostics.size());
        });

        test("shadow_rangefor_and_catch_over_field", [this]() {
            auto e = make_engine();
            auto r = e->check(
                "class C {\n  var items = [1];\n  function each() -> void {\n"
                "    for (var items : [2]) { var y = items; }\n"
                "    try { throw \"e\"; } catch (items) { }\n  }\n}");
            check_eq((size_t)2, warning_count(r));
            check_true(r.diagnostics[0].message.find("range-for variable 'items' shadows field") != std::string::npos);
            check_true(r.diagnostics[1].message.find("catch variable 'items' shadows field") != std::string::npos);
        });

        test("shadow_inherited_and_static_fields", [this]() {
            auto e = make_engine();
            auto r = e->check(
                "class Base { int bv = 5; }\n"
                "class D : Base { function m() -> void { var bv = 1; } }");
            check_eq((size_t)1, warning_count(r));
            check_true(r.diagnostics[0].message.find("shadows field 'bv' of class 'Base'") != std::string::npos);
            auto r2 = e->check(
                "class S {\n  static int count = 0;\n  function m() -> void { var count = 1; }\n}");
            check_eq((size_t)1, warning_count(r2));
            check_true(r2.diagnostics[0].message.find("shadows static field 'count'") != std::string::npos);
        });

        test("shadow_outer_local", [this]() {
            auto e = make_engine();
            auto r = e->check(
                "function f() -> void {\n  var x = 1;\n  if (x > 0) { var x = 2; }\n}");
            check_eq((size_t)1, warning_count(r));
            check_true(r.diagnostics[0].message.find(
                "local 'x' shadows a declaration in an enclosing scope (declared at 2:") != std::string::npos);
        });

        test("shadow_global_only_when_used_earlier", [this]() {
            auto e = make_engine();
            // read the global, then declare a local with the same spelling: warn
            auto r = e->check(
                "var total = 0;\nfunction f() -> void {\n  total += 1;\n  var total = 5;\n}");
            check_eq((size_t)1, warning_count(r));
            check_true(r.diagnostics[0].message.find(
                "local 'total' shadows global 'total' used earlier in this function") != std::string::npos);
            // plain local-over-global with no earlier use: silent (too common to flag)
            check_eq((size_t)0, e->check(
                "var total = 0;\nfunction f() -> void { var total = 5; }").diagnostics.size());
        });

        test("shadow_warnings_never_gate_strict", [this]() {
            auto e = make_engine();
            e->static_checking(check_mode::strict);
            // strict throws on errors only; a shadow warning still executes
            check_eq((int64_t)3, e->execute(
                "class K { int hp = 10; function m() -> int { var hp = 3; return hp; } }\n"
                "K().m()").as_int());
            check_eq((size_t)1, e->last_check_diagnostics().diagnostics.size());
            e->static_checking(check_mode::off);
        });

        test("shadow_lambda_field_visibility", [this]() {
            auto e = make_engine();
            // [=] lambdas cannot reach fields (audited): a same-named lambda local is
            // NOT a field shadow. [this] lambdas can: warn.
            check_eq((size_t)0, e->check(
                "class C {\n  int v = 1;\n  function m() -> void { var f = [=]() { var v = 2; return v; }; }\n}").diagnostics.size());
            auto r = e->check(
                "class C {\n  int v = 1;\n  function m() -> void { var f = [this]() { var v = 2; return v; }; }\n}");
            check_eq((size_t)1, warning_count(r));
        });

        // ------------------------------------------------------- field/global ambiguity
        // Runtime bare-name resolution reaches GLOBALS before this-fields (audited,
        // docs/site/guide/07-classes.html): a name that is both is flow-order-ambiguous
        // to the static walk, so member hits on it degrade to unknown. Pinned here
        // because the old field-first inference produced a strict-mode false positive.

        test("field_global_ambiguity_is_lenient", [this]() {
            auto e = make_engine();
            // global declared AFTER the class: runtime reads the (int) global - the old
            // checker trusted the string field and errored on a program that runs clean
            check_eq((size_t)0, error_count(e->check(
                "class C { string count = \"s\"; function probe() -> void { int x = count; } }\n"
                "var count = 5;\nC().probe();")));
            // writes through the same ambiguity: lenient too
            check_eq((size_t)0, error_count(e->check(
                "class C { string count = \"s\"; function poke() -> void { count = 7; } }\n"
                "var count = 5;\nC().poke();")));
            // unambiguous field names keep their precise checking
            check_eq((size_t)1, error_count(e->check(
                "class C { string only_field = \"s\"; function probe() -> void { int x = only_field; } }")));
        });

        test("shared_ptr_decl_pointee_checking", [this]() {
            // Runtime enforces typed shared_ptr decl pointees (Dev ruling 2026-07);
            // the checker models the same rule through its object-assignability
            // lattice: wrong class errors, derived->base stays clean.
            auto e = make_engine();
            check_eq((size_t)1, error_count(e->check(
                "class Foo { int x = 1; } class Bar { int y = 2; } shared_ptr<Bar> wrong = Foo();")));
            check_eq((size_t)0, error_count(e->check(
                "class Base { int b = 1; } class D : Base { int d = 2; } shared_ptr<Base> ok = D();")));
            check_eq((size_t)0, error_count(e->check(
                "class Foo2 { int x = 1; } shared_ptr<Foo2> n = null;")));
        });

        test("shared_ptr_auto_decl_infer_then_enforce", [this]() {
            // shared_ptr<auto> models runtime's infer-then-enforce (Dev ruling 2026-07):
            // the pointee comes from the initializer's class and behaves like the
            // explicit spelling thereafter. Nothing to infer (missing/null/non-class
            // initializer) errors, matching runtime.
            auto e = make_engine();
            check_eq((size_t)1, error_count(e->check(
                "class Foo { int x = 1; } shared_ptr<auto> p;")), "no init");
            check_eq((size_t)1, error_count(e->check(
                "class Foo { int x = 1; } shared_ptr<auto> p = null;")), "null init");
            check_eq((size_t)1, error_count(e->check(
                "class Foo { int x = 1; } shared_ptr<auto> p = 5;")), "int init");
            // infer-then-enforce: wrong-class reassign errors, upcast + null stay clean
            check_eq((size_t)1, error_count(e->check(
                "class Foo { int x = 1; } class Bar { int y = 2; } shared_ptr<auto> p = Foo(); p = Bar();")), "wrong-class reassign");
            check_eq((size_t)0, error_count(e->check(
                "class Base { int b = 1; } class D : Base { int d = 2; } shared_ptr<auto> p = Base(); p = new D();")), "upcast reassign");
            check_eq((size_t)0, error_count(e->check(
                "class Foo { int x = 1; } shared_ptr<auto> p = Foo(); p = null; int x = p.x;")), "null reassign + member");
            // inferred tag models member usage exactly like the explicit spelling
            // (parity-equality: whatever the explicit twin reports, inferred matches)
            for (const char* tail : { "shared_ptr<%s> p = new Foo(); string s = p.x;",
                                      "shared_ptr<%s> p = new Foo(); p.nope();" }) {
                char explicit_src[256], inferred_src[256];
                std::snprintf(explicit_src, sizeof(explicit_src),
                    (std::string("class Foo { int x = 1; } ") + tail).c_str(), "Foo");
                std::snprintf(inferred_src, sizeof(inferred_src),
                    (std::string("class Foo { int x = 1; } ") + tail).c_str(), "auto");
                check_eq(error_count(e->check(explicit_src)), error_count(e->check(inferred_src)),
                         std::string("explicit/inferred parity: ") + tail);
            }
        });

        // ------------------------------------------------------------ fuzz corpus FP gate

        test("fuzz_corpus_false_positive_gate", [this]() {
            // 200 generator seeds: any program that runs clean must not produce strict
            // errors unless it genuinely violates declared types. The generator emits
            // valid typing, so the expected FP count is zero.
            constexpr uint64_t k_seed_count = 200;
            constexpr double k_budget_seconds = 0.1;
            int false_positives = 0;
            int clean_programs = 0;
            std::string first_report;
            for (uint64_t seed = 0; seed < k_seed_count; ++seed) {
                fuzz::program p = fuzz::generate_program(seed, 3);
                auto eng = make_engine();
                eng->execution_budget(k_budget_seconds);
                eng->memory_cap(64 * 1024 * 1024);
                jai::engine* raw = eng.get();
                // the corpus presumes the host surface every real host provides
                eng->add_variadic_function("print", [raw](const std::vector<script_value>& args) -> script_value {
                    (void)args;
                    return script_value(std::monostate{}, raw);
                });
                eng->add_variadic_function("to_string", [raw](const std::vector<script_value>& args) -> script_value {
                    return script_value(args.empty() ? std::string() : args[0].to_string(), raw);
                });
                bool ran_clean = true;
                std::vector<std::string> flagged;
                for (const auto& seg : p.segments) {
                    check_report report;
                    try {
                        report = eng->check(seg);
                    } catch (...) {
                        ran_clean = false;   // parse-level issue; not a checker finding
                        break;
                    }
                    if (report.has_errors()) {
                        for (const auto& d : report.diagnostics) {
                            if (d.severity == check_diagnostic::level::error) {
                                flagged.push_back(std::to_string(d.line) + ":" + std::to_string(d.column) +
                                                  " " + d.message);
                            }
                        }
                    }
                    try {
                        eng->execute(seg);
                    } catch (...) {
                        ran_clean = false;   // runtime error: errors on it are legitimate
                        break;
                    }
                }
                if (!ran_clean) { continue; }
                ++clean_programs;
                if (!flagged.empty()) {
                    ++false_positives;
                    if (first_report.empty()) {
                        first_report = "seed " + std::to_string(seed) + " ran clean but was flagged:\n";
                        for (const auto& f : flagged) { first_report += "  " + f + "\n"; }
                        first_report += p.joined();
                    }
                }
            }
            check_gt(clean_programs, 0);
            check_eq(0, false_positives, first_report.empty() ? "no false positives" : first_report);
        });
    }
};

} // namespace jai::foundry::tests

using static_check_tests = jai::foundry::tests::static_check_tests;
FOUNDRY_REGISTER(static_check_tests)
