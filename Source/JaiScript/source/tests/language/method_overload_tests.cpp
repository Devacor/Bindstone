// Tests for script-method overloading by arity and parameter type (mirrors the resolution
// JaiScript already does for constructors).

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class method_overload_tests : public suite {
public:
    method_overload_tests() : suite("Method Overload Tests") {}

    void forge_tests() override {

        test("overload_by_arity", [&]() {
            auto eng = make_engine();
            eng->execute(R"(
                class Calc {
                    int add(int a) { return a + 100; }
                    int add(int a, int b) { return a + b; }
                }
                auto c = Calc();
            )");
            check_eq(eng->execute("c.add(5)").as<int>(), 105);
            check_eq(eng->execute("c.add(5, 7)").as<int>(), 12);
        });

        test("overload_by_type", [&]() {
            auto eng = make_engine();
            eng->execute(R"(
                class Printer {
                    string render(int x) { return "int"; }
                    string render(string s) { return "string"; }
                }
                auto p = Printer();
            )");
            check_eq(eng->execute("p.render(5)").as<std::string>(), std::string("int"));
            check_eq(eng->execute("p.render(\"hi\")").as<std::string>(), std::string("string"));
        });

        // Overload sets must survive a reload (rebuild, not collapse to last-declared).
        test("overload_set_survives_reload", [&]() {
            auto eng = make_engine();
            const char* def = R"(
                class Calc {
                    int add(int a) { return a + 100; }
                    int add(int a, int b) { return a + b; }
                }
            )";
            eng->execute(def);
            eng->execute("auto c = Calc();");
            check_eq(eng->execute("c.add(5)").as<int>(), 105);
            eng->execute(def); // reload, identical
            check_eq(eng->execute("c.add(5)").as<int>(), 105);
            check_eq(eng->execute("c.add(5, 7)").as<int>(), 12);
        });

        // ------------------------------------------------------- parameter defaults
        // Dev ruling (2026-07): a callable with N params of which K have trailing
        // defaults is callable with N-K..N args UNIFORMLY - instance methods, ctors
        // (incl. this()/super() delegation), static methods, namespace functions -
        // matching what free functions always did. Resolution gates share ONE kernel
        // (arity_accepts, core/overload_resolution.hpp); binding already evaluated
        // defaults on both backends.

        test("defaults_instance_method", [&]() {
            auto eng = make_engine();
            eng->execute(R"(
                class C { int f(int a, int b = 10, int c = 100) { return a + b + c; } }
                auto c = C();
            )");
            check_eq(eng->execute("c.f(1)").as<int>(), 111);        // omit all defaults
            check_eq(eng->execute("c.f(1, 2)").as<int>(), 103);     // omit one
            check_eq(eng->execute("c.f(1, 2, 3)").as<int>(), 6);    // supply all
        });

        test("defaults_constructor_and_delegation", [&]() {
            auto eng = make_engine();
            eng->execute(R"(
                class C {
                    int v = 0;
                    C(int a, int b = 10) { v = a + b; }
                    C() : this(5) {}
                }
            )");
            check_eq(eng->execute("C(1).v").as<int>(), 11);        // ctor omits default
            check_eq(eng->execute("C(1, 2).v").as<int>(), 3);      // ctor supplies all
            check_eq(eng->execute("C().v").as<int>(), 15);         // this(5) fills b=10
        });

        test("defaults_super_chain", [&]() {
            auto eng = make_engine();
            eng->execute(R"(
                class Base { int bv = 0; Base(int a, int b = 7) { bv = a + b; } }
                class D : Base { D(int x) : super(x) {} }
            )");
            check_eq(eng->execute("D(1).bv").as<int>(), 8);        // super(x) fills b=7
        });

        test("defaults_static_method_and_free_fn", [&]() {
            auto eng = make_engine();
            eng->execute(R"(
                class S { static int f(int a, int b = 10) { return a + b; } }
                function g(int a, int b = 10) -> int { return a + b; }
            )");
            check_eq(eng->execute("S::f(1)").as<int>(), 11);
            check_eq(eng->execute("S::f(1, 2)").as<int>(), 3);
            check_eq(eng->execute("g(1)").as<int>(), 11);
            check_eq(eng->execute("g(1, 2)").as<int>(), 3);
        });

        test("defaults_namespace_function", [&]() {
            auto eng = make_engine();
            eng->execute(R"(
                namespace N { function f(int a, int b = 10) -> int { return a + b; } }
            )");
            check_eq(eng->execute("N::f(1)").as<int>(), 11);
            check_eq(eng->execute("N::f(1, 2)").as<int>(), 3);
        });

        test("defaults_coroutine_method", [&]() {
            auto eng = make_engine();
            eng->execute(R"(
                class G { coroutine int gen(int start, int step = 5) { yield start; yield start + step; } }
                auto g = G();
                var h = g.gen(1);
            )");
            check_eq(eng->execute("h.resume()").as<int>(), 1);
            check_eq(eng->execute("h.resume()").as<int>(), 6);
        });

        test("defaults_overload_ambiguity_exact_arity_wins", [&]() {
            auto eng = make_engine();
            // Same-score ambiguity between an exact-arity overload and one that would
            // fill defaults: the candidate using FEWER defaults wins (pinned tie rule).
            eng->execute(R"(
                class A {
                    string f(int a) { return "one"; }
                    string f(int a, int b = 9) { return "two"; }
                }
                auto a = A();
            )");
            check_eq(eng->execute("a.f(1)").as<std::string>(), std::string("one"));
            check_eq(eng->execute("a.f(1, 2)").as<std::string>(), std::string("two"));
            // untyped tie shape: fewer defaults still wins
            eng->execute(R"(
                class B {
                    string g(var a) { return "one"; }
                    string g(var a, var b = 9) { return "two"; }
                }
                auto b = B();
            )");
            check_eq(eng->execute("b.g(1)").as<std::string>(), std::string("one"));
            check_eq(eng->execute("b.g(1, 2)").as<std::string>(), std::string("two"));
        });

        test("defaults_arity_errors_still_fire", [&]() {
            auto eng = make_engine();
            eng->execute(R"(
                class E { int f(int a, int b = 10) { return a + b; } }
                auto e = E();
            )");
            check_throws([&]() { eng->execute("e.f();"); });          // below the window
            check_throws([&]() { eng->execute("e.f(1, 2, 3);"); });   // above the window
        });
    }
};

FOUNDRY_REGISTER(jai::foundry::tests::method_overload_tests)

} // namespace jai::foundry::tests
