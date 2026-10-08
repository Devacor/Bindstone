// const (docs/const.md): declarations, parameters, class constants, C++'s top-level rule (deep through values, stopping at
// handles), the load-time diagnostics and the run-time texts. Every test runs on whichever backend the runner selected
// (--backend=vm for the vm); the interpreter is the oracle.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

#include <memory>
#include <sstream>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

namespace {

struct run_result {
    std::string output;
    std::string error;
};

run_result run(const std::string& source, check_mode mode = check_mode::strict) {
    auto e = make_engine();
    stdlib::register_all(*e);
    e->static_checking(mode);
    auto out = std::make_shared<std::ostringstream>();
    e->set_output_stream(out);
    run_result r;
    try {
        e->execute(source);
    } catch (const std::exception& ex) {
        r.error = ex.what();
    }
    r.output = out->str();
    return r;
}

bool contains(const std::string& haystack, std::string_view needle) { return haystack.find(needle) != std::string::npos; }

#define CHECK_ERROR(source, needle) do { const auto r_ = run(source); check_true(contains(r_.error, needle), std::string(source) + " -> " + r_.error); } while (0)

}   // namespace

class const_tests : public suite {
public:
    const_tests() : suite("Const") {}

    void forge_tests() override {
        // ------------------------------------------------------------------ declarations
        test("const_reads_like_a_variable", [this]() {
            const auto r = run(R"JS(
                const int a = 4;
                const b = a * 2;
                const string s = "hi";
                function f() { const int x = 1; return x + a; }
                print(a, " ", b, " ", s, " ", f());
            )JS");
            check_eq(std::string(""), r.error);
            check_eq(std::string("4 8 hi 5\n"), r.output);
        });

        test("store_to_a_const_is_a_load_error", [this]() {
            CHECK_ERROR("const int a = 1; a = 2;", "cannot assign to const 'a'");
            CHECK_ERROR("function f() { const int a = 1; a += 2; return a; } f();", "cannot assign to const 'a'");
            CHECK_ERROR("function f() { const int a = 1; a++; return a; } f();", "cannot assign to const 'a'");
            CHECK_ERROR("function f() { const int a = 1; --a; return a; } f();", "cannot assign to const 'a'");
        });

        test("store_to_a_const_raises_at_run_time", [this]() {
            for (check_mode mode : {check_mode::warn, check_mode::off}) {
                const auto r = run(R"JS(
                    function f() { const int a = 1; try { a = 2; } catch (e) { print("caught: ", e); } return a; }
                    print(f());
                )JS", mode);
                check_eq(std::string(""), r.error);
                check_eq(std::string("caught: Cannot assign to 'a': it is read-only\n1\n"), r.output);
            }
        });

        test("const_needs_an_initializer", [this]() {
            CHECK_ERROR("const int a;", "const 'a' needs an initializer");
            CHECK_ERROR("function f() { const x; }", "const 'x' needs an initializer");
            check_eq(std::string(""), run("class P { int x = 3; } const P p; print(p.x);").error);
        });

        test("javascript_spelling_is_const_auto", [this]() {
            const auto r = run("const xs = [1, 2]; xs.push(3);");
            check_true(contains(r.error, "cannot call 'push' on 'xs': 'xs' is const and 'push' changes it"), r.error);
        });

        // ------------------------------------------------------------------ deep through values
        test("a_const_value_is_deeply_read_only", [this]() {
            CHECK_ERROR("const array<int> a = [1, 2]; a[0] = 5;", "cannot assign to 'a[0]': 'a' is const");
            CHECK_ERROR("const map<string, int> m = {\"k\": 1}; m[\"k\"] = 2;", "'m' is const");
            CHECK_ERROR("class P { int x = 0; } const P p; p.x = 1;", "cannot assign to 'p.x': 'p' is const");
            CHECK_ERROR("class P { array<int> xs = []; } const P p; p.xs.push(1);", "'p' is const and 'push' changes it");
            CHECK_ERROR("const string s = \"ab\"; s += \"c\";", "cannot assign to const 's'");
        });

        test("methods_that_write_their_receiver", [this]() {
            const char* cls = R"JS(
                class Counter {
                    int n = 0;
                    void bump() { n += 1; }
                    void bump_twice() { bump(); bump(); }
                    int read() { return n; }
                }
            )JS";
            check_eq(std::string("0\n"), run(std::string(cls) + "const Counter c; print(c.read());").output);
            CHECK_ERROR(std::string(cls) + "const Counter c; c.bump();", "'c' is const and 'bump' writes its receiver");
            CHECK_ERROR(std::string(cls) + "const Counter c; c.bump_twice();", "'bump_twice' writes its receiver");
        });

        test("a_const_handle_keeps_its_pointee_writable", [this]() {
            const auto r = run(R"JS(
                class P { int hp = 1; void hit() { hp -= 1; } }
                const shared_ptr<P> h = new P();
                h.hp = 10;
                h.hit();
                print(h.hp);
            )JS");
            check_eq(std::string(""), r.error);
            check_eq(std::string("9\n"), r.output);
            CHECK_ERROR("class P { } const shared_ptr<P> h = new P(); h = new P();", "cannot assign to const 'h'");
        });

        test("const_var_decides_by_the_held_kind", [this]() {
            const auto handle = run(R"JS(
                class P { int hp = 1; }
                const var h = new P();
                h.hp = 5;
                print(h.hp);
            )JS");
            check_eq(std::string(""), handle.error);
            check_eq(std::string("5\n"), handle.output);
            const auto value = run(R"JS(
                const var a = [1, 2];
                try { a[0] = 5; } catch (e) { print("caught: ", e); }
                print(a[0]);
            )JS");
            check_eq(std::string(""), value.error);
            check_eq(std::string("caught: Cannot assign to 'a[0]': it is read-only ('a' is const)\n1\n"), value.output);
        });

        // ------------------------------------------------------------------ references, casts, returns
        test("references_to_a_const", [this]() {
            check_eq(std::string("3\n"), run("const int a = 3; const int& r = a; print(r);").output);
            CHECK_ERROR("function f() { const int a = 3; int& r = a; }", "cannot bind 'r' (declared int&) to const 'a'");
            CHECK_ERROR("function f() { const int a = 3; auto& r = a; r = 4; }", "cannot assign to const 'r'");
            CHECK_ERROR("function f() { const int a = 3; var x = cast<int&>(a); }", "cannot cast const 'a' to int&");
            CHECK_ERROR("const int a = 3; int& f() { return a; }", "cannot return const 'a' from 'f' by reference");
        });

        test("copies_of_a_const_are_writable", [this]() {
            const auto r = run(R"JS(
                const array<int> a = [1, 2];
                auto b = a;
                b.push(3);
                var c = a;
                c[0] = 9;
                print(to_json(a), " ", to_json(b), " ", to_json(c));
            )JS");
            check_eq(std::string(""), r.error);
            check_eq(std::string("[1,2] [1,2,3] [9,2]\n"), r.output);
        });

        // ------------------------------------------------------------------ parameters
        test("const_reference_parameter_takes_literals", [this]() {
            const auto r = run(R"JS(
                int twice(const int& x) { return x * 2; }
                auto plus = (const int& x) => x + 1;
                print(twice(21), " ", plus(1));
            )JS");
            check_eq(std::string(""), r.error);
            check_eq(std::string("42 2\n"), r.output);
            CHECK_ERROR("void f(const int& x) { x = 2; }", "cannot assign to const 'x'");
            CHECK_ERROR("void f(const int x) { x = 2; }", "cannot assign to const 'x'");
        });

        test("a_const_into_a_writing_reference_parameter", [this]() {
            const auto r = run("void inc(int& x) { x += 1; } const int a = 1; inc(a);");
            check_true(contains(r.error, "'inc' writes through parameter 'x'"), r.error);
            check_true(contains(r.error, "'a' is const"), r.error);
            check_eq(std::string("1\n"), run("int get(int& x) { return x; } const int a = 1; print(get(a));").output);
        });

        test("const_is_not_part_of_a_signature", [this]() {
            const auto r = run("class A { void m(int& x) { } void m(const int& x) { } }");
            check_true(contains(r.error, "'m' is already defined with parameters (int&)"), r.error);
            CHECK_ERROR("function f() { } function f() { }", "'f' is already defined");
        });

        // ------------------------------------------------------------------ loops, bindings, captures
        test("range_for_over_a_const", [this]() {
            CHECK_ERROR("function f(array<int> xs) { for (const auto& x : xs) { x = 1; } }", "cannot assign to const 'x'");
            CHECK_ERROR("const array<int> xs = [1]; for (auto& x : xs) { x = 1; }", "cannot assign to const 'x'");
            CHECK_ERROR("const array<int> xs = [1]; for (int& x : xs) { }", "cannot bind 'x' (declared int&) to an element of const 'xs'");
            check_eq(std::string("3\n"), run("const array<int> xs = [1, 2]; int t = 0; for (auto x : xs) { x += 1; t += 0; } for (const auto& x : xs) { t += x; } print(t);").output);
            CHECK_ERROR("const map<string, int> m = {\"a\": 1}; for (auto& [k, v] : m) { v = 2; }", "cannot assign to const 'v'");
        });

        test("captures_keep_constness_like_cpp", [this]() {
            CHECK_ERROR("function f() { const int a = 1; auto g = [a]() { a = 2; }; }", "cannot assign to const 'a'");
            CHECK_ERROR("function f() { const int a = 1; auto g = [&a]() { a = 2; }; }", "cannot assign to const 'a'");
            const auto r = run(R"JS(
                function f() { const int a = 1; auto g = [b = a]() { b += 1; return b; }; return g(); }
                print(f());
            )JS");
            check_eq(std::string(""), r.error);
            check_eq(std::string("2\n"), r.output);
        });

        test("destructuring_and_for_init", [this]() {
            CHECK_ERROR("function f() { const auto [a, b] = [1, 2]; a = 3; }", "cannot assign to const 'a'");
            CHECK_ERROR("function f() { for (const int i = 0; i < 3; i++) { } }", "cannot assign to const 'i'");
        });

        // ------------------------------------------------------------------ class constants
        test("class_constants_are_static", [this]() {
            const auto r = run(R"JS(
                class Hero {
                    const int MAX_HP = 100;
                    static const float SPEED = 2.5;
                    int hp = 0;
                    void heal() { hp = MAX_HP; }
                }
                Hero h;
                h.heal();
                print(h.hp, " ", Hero::MAX_HP, " ", Hero::SPEED);
            )JS");
            check_eq(std::string(""), r.error);
            check_eq(std::string("100 100 2.5\n"), r.output);
            CHECK_ERROR("class C { const int K = 1; } C::K = 2;", "cannot assign to const 'C::K'");
            CHECK_ERROR("class C { const int K = 1; void f() { K = 2; } }", "cannot assign to const 'K'");
            CHECK_ERROR("class C { const int K; }", "const 'K' needs an initializer");
        });

        // ------------------------------------------------------------------ program scope
        test("a_rerun_initializes_again", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            auto out = std::make_shared<std::ostringstream>();
            e->set_output_stream(out);
            const std::string src = "const int LIMIT = 7; print(LIMIT);";
            e->execute(src);
            e->execute(src);
            check_eq(std::string("7\n7\n"), out->str());
        });

        test("another_unit_cannot_store_to_a_const", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            e->execute("const int LIMIT = 7;");
            std::string err;
            try { e->execute("LIMIT = 8;"); } catch (const std::exception& ex) { err = ex.what(); }
            check_true(contains(err, "cannot assign to const 'LIMIT'"), err);
            err.clear();
            try { e->add_global("LIMIT", script_value(script_int(9), e.get())); } catch (const std::exception& ex) { err = ex.what(); }
            check_true(contains(err, "Cannot assign to 'LIMIT': it is read-only"), err);
        });

        // ------------------------------------------------------------------ folding and hot reload (docs/const.md 10)
        test("a_redefined_class_constant_reaches_every_use", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            e->execute("class C { const int K = 1; } int readk() { return C::K; }");
            check_eq(script_int(1), e->execute("readk();").as_int());
            check_eq(script_int(1), e->execute("readk();").as_int());
            e->execute("class C { const int K = 2; }");
            check_eq(script_int(2), e->execute("readk();").as_int());
            e->execute("class C { static int K = 3; }");   // no longer a const: a mutable static keeps its value, the read is general again
            check_eq(script_int(2), e->execute("readk();").as_int());
            e->execute("C::K = 5;");
            check_eq(script_int(5), e->execute("readk();").as_int());
        });

        test("a_running_frame_sees_the_new_value", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            engine* raw = e.get();
            e->add_function("redefine", [raw](script_int k) {
                raw->execute("class C { const int K = " + std::to_string(k) + "; }");
                return k;
            });
            e->execute(R"JS(
                class C { const int K = 1; }
                int run() { int t = 0; for (int i = 0; i < 4; i++) { t += C::K; if (i == 1) { redefine(10); } } return t; }
            )JS");
            check_eq(script_int(22), e->execute("run();").as_int());
        });

        test("a_reopened_namespace_constant", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            e->execute("namespace cfg { const int W = 3; } int w() { return cfg::W; }");
            check_eq(script_int(3), e->execute("w();").as_int());
            check_eq(script_int(3), e->execute("w();").as_int());
            e->execute("namespace cfg { const int W = 4; }");
            check_eq(script_int(4), e->execute("w();").as_int());
        });

        // ------------------------------------------------------------------ enums (docs/const.md 11)
        test("an_enum_name_is_a_const_map", [this]() {
            check_true(contains(run("enum Dir { n, e } Dir = 5;").error, "cannot assign to const 'Dir'"));
            check_true(contains(run("enum Dir { n, e } Dir[\"s\"] = 2;").error, "'Dir' is const"));
            const auto r = run("enum Dir { n, e } int d = Dir::e; d = Dir::n; print(d, \" \", Dir.e, \" \", Dir.size());");
            check_eq(std::string(""), r.error);
            check_eq(std::string("0 1 2\n"), r.output);
        });

        test("a_redefined_enum_reaches_every_use", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            e->execute("enum Fx { fire, poison } int p() { return Fx::poison; } int q() { int t = 0; for (int i = 0; i < 3; i++) { t += Fx.poison; } return t; }");
            check_eq(script_int(1), e->execute("p();").as_int());
            check_eq(script_int(1), e->execute("p();").as_int());
            check_eq(script_int(3), e->execute("q();").as_int());
            e->execute("enum Fx { poison, fire }");
            check_eq(script_int(0), e->execute("p();").as_int());
            check_eq(script_int(0), e->execute("q();").as_int());
        });

        test("namespace_constants", [this]() {
            check_eq(std::string("3\n"), run("namespace cfg { const int W = 3; } print(cfg::W);").output);
            CHECK_ERROR("namespace cfg { const int W = 3; } cfg::W = 4;", "cannot assign to const 'cfg::W'");
        });
    }
};

}   // namespace jai::foundry::tests

using const_tests = jai::foundry::tests::const_tests;
FOUNDRY_REGISTER(const_tests)
