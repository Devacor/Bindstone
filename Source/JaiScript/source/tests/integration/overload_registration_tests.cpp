#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

#include <string>
#include <vector>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Exercises the overloaded-function registration/dispatch surface across entry points and
// registration orders. These pin the behavior the registration consolidation must preserve, and
// guard the drift bugs the parallel paths have historically hidden (e.g. the dispatcher-into-its-
// own-set recursion that crashed a 0-arg call after a typed overload).
class overload_registration_tests : public suite {
public:
    overload_registration_tests() : suite("Overload Registration") {}

    void forge_tests() override {

        test("free_fn_same_arity_resolves_by_type", [this]() {
            auto eng = make_engine();
            eng->add_function("pick", [](int a, int b) -> std::string { (void)a; (void)b; return "ints"; });
            eng->add_function("pick", [](double a, double b) -> std::string { (void)a; (void)b; return "floats"; });
            check_eq(std::string("ints"), eng->execute("pick(1, 2)").as<std::string>());
            check_eq(std::string("floats"), eng->execute("pick(1.5, 2.5)").as<std::string>());
        });

        test("free_fn_distinct_arity", [this]() {
            auto eng = make_engine();
            eng->add_function("g", [](int a) -> int { return a; });
            eng->add_function("g", [](int a, int b) -> int { return a + b; });
            check_eq((int64_t)5, eng->execute("g(5)").as_int());
            check_eq((int64_t)7, eng->execute("g(3, 4)").as_int());
        });

        // Registration ORDER must not change resolution — order independence is exactly where the
        // parallel registration paths tend to drift.
        test("free_fn_registration_order_invariant", [this]() {
            auto a = make_engine();
            a->add_function("h", [](int x) -> std::string { (void)x; return "i"; });
            a->add_function("h", [](const std::string& x) -> std::string { (void)x; return "s"; });
            check_eq(std::string("i"), a->execute("h(1)").as<std::string>());
            check_eq(std::string("s"), a->execute("h(\"x\")").as<std::string>());

            auto b = make_engine();
            b->add_function("h", [](const std::string& x) -> std::string { (void)x; return "s"; });
            b->add_function("h", [](int x) -> std::string { (void)x; return "i"; });
            check_eq(std::string("i"), b->execute("h(1)").as<std::string>());
            check_eq(std::string("s"), b->execute("h(\"x\")").as<std::string>());
        });

        // Mixed string/int overloads coexist and the engine stays usable across calls (no set
        // corruption from registering the second overload).
        test("free_fn_mixed_types_engine_stays_usable", [this]() {
            auto eng = make_engine();
            eng->add_function("m", [](int x, int y) -> int { return x + y; });
            eng->add_function("m", [](const std::string& x, const std::string& y) -> std::string { return x + y; });
            check_eq((int64_t)3, eng->execute("m(1, 2)").as_int());
            check_eq(std::string("ab"), eng->execute("m(\"a\", \"b\")").as<std::string>());
            check_eq((int64_t)5, eng->execute("m(2, 3)").as_int());
        });

        // A typed function and then a variadic of the same name: the fixed-arity overload wins at
        // its own arity, the variadic catches every other arity.
        test("typed_then_variadic_same_name", [this]() {
            auto eng = make_engine();
            engine* e = eng.get();
            eng->add_function("v", [](int x, int y) -> std::string { (void)x; (void)y; return "two"; });
            eng->add_variadic_function("v", [e](const std::vector<script_value>& args) -> script_value {
                return script_value(std::string("var:") + std::to_string(args.size()), e);
            });
            check_eq(std::string("two"), eng->execute("v(1, 2)").as<std::string>());
            check_eq(std::string("var:1"), eng->execute("v(9)").as<std::string>());
            check_eq(std::string("var:3"), eng->execute("v(1, 2, 3)").as<std::string>());
        });

        test("variadic_then_typed_same_name", [this]() {
            auto eng = make_engine();
            engine* e = eng.get();
            eng->add_variadic_function("w", [e](const std::vector<script_value>& args) -> script_value {
                return script_value(std::string("var:") + std::to_string(args.size()), e);
            });
            eng->add_function("w", [](int x, int y) -> std::string { (void)x; (void)y; return "two"; });
            check_eq(std::string("two"), eng->execute("w(1, 2)").as<std::string>());
            check_eq(std::string("var:1"), eng->execute("w(9)").as<std::string>());
        });

        // The dispatcher-recursion regression, from the registration angle: a 0-arg constructor
        // registered (which immediately installs a dispatcher) followed by a typed N-arg ctor must
        // not migrate the dispatcher into its own set — a 0-arg call must not self-recurse.
        test("ctor_zeroarg_after_typed_no_recursion", [this]() {
            auto eng = make_engine();
            class C { public: int v = 7; C() {} C(int a, int b) { v = a + b; } };
            dynamic_binder<C>(*eng, "C").constructor<>().constructor<int, int>().property("v", &C::v).build();
            check_eq((int64_t)7, eng->execute("auto z = C(); z.v").as_int());
            check_eq((int64_t)5, eng->execute("auto t = C(2, 3); t.v").as_int());
        });
    }
};

} // namespace jai::foundry::tests

using overload_registration_tests = jai::foundry::tests::overload_registration_tests;
FOUNDRY_REGISTER(overload_registration_tests)
