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
    }
};

FOUNDRY_REGISTER(jai::foundry::tests::method_overload_tests)

} // namespace jai::foundry::tests
