#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

namespace jai::foundry::tests {

class map_methods_tests : public suite {
public:
    map_methods_tests() : suite("Map Methods") {}

    void forge_tests() override {
        test("has_true", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(var m = {"a": 1, "b": 2}; m.has("a");)");
            check_eq(result.as<bool>(), true);
        });

        test("has_false", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(var m = {"a": 1, "b": 2}; m.has("z");)");
            check_eq(result.as<bool>(), false);
        });

        test("get_existing", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(var m = {"a": 10, "b": 20}; m.get("a", 0);)");
            check_eq(result.as<int>(), 10);
        });

        test("get_default", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(var m = {"a": 10, "b": 20}; m.get("z", 99);)");
            check_eq(result.as<int>(), 99);
        });

        test("length", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(var m = {"a": 1, "b": 2, "c": 3}; m.length();)");
            check_eq(result.as<int>(), 3);
        });

        test("remove_existing", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var m = {"a": 1, "b": 2, "c": 3};
                var removed = m.remove("b");
                removed;
            )");
            check_eq(result.as<bool>(), true);

            result = eng->execute("m.length();");
            check_eq(result.as<int>(), 2);
        });

        test("remove_nonexistent", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var m = {"a": 1, "b": 2};
                m.remove("z");
            )");
            check_eq(result.as<bool>(), false);
        });

        test("remove_if", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var m = {"a": 1, "b": 2, "c": 3, "d": 4};
                m.remove_if([](auto k, auto v) -> auto { return v % 2 == 0; });
            )");
            check_eq(result.as<int>(), 2);

            result = eng->execute("m.length();");
            check_eq(result.as<int>(), 2);
        });

        test("filter", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var m = {"a": 1, "b": 2, "c": 3, "d": 4};
                var filtered = m.filter([](auto k, auto v) -> auto { return v > 2; });
                filtered.length();
            )");
            check_eq(result.as<int>(), 2);
        });

        test("to_array", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var m = {"x": 10, "y": 20};
                var pairs = m.to_array();
                pairs.length();
            )");
            check_eq(result.as<int>(), 2);

            // Check that each element is a 2-element array
            result = eng->execute("pairs[0].length();");
            check_eq(result.as<int>(), 2);
        });
    }
};

} // namespace jai::foundry::tests

using map_methods_tests = jai::foundry::tests::map_methods_tests;
FOUNDRY_REGISTER(map_methods_tests)
