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

        // Builtin args arrive BY-VALUE: a subscript read in argument position mints an
        // element/map-entry reference, and map key lookups compare through operator<=>
        // (which never derefs) - so m.has(arr[i]) silently missed while the hoisted-temp
        // spelling hit (the jaidoom MINFO.has(MO_TYPE[i]) workaround). These pin
        // hoisted-temp equivalence through deref_builtin_args (builtin_methods.hpp).
        test("has_subscript_arg_int_keys", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var minfo = {};
                minfo[3004] = "trooper";
                minfo[9] = "shotgun";
                var types = [3004, 9, 42];
                minfo.has(types[0]) && minfo.has(types[1]) && !minfo.has(types[2]);
            )");
            check_eq(result.as<bool>(), true);
        });

        test("has_subscript_arg_string_keys", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var m = {"alpha": 1, "beta": 2};
                var keys = ["alpha", "gamma"];
                m.has(keys[0]) && m.contains(keys[0]) && !m.has(keys[1]);
            )");
            check_eq(result.as<bool>(), true);
        });

        test("get_remove_erase_subscript_arg", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var m = {"a": 10, "b": 20, "c": 30};
                var keys = ["a", "b", "c", "z"];
                int got = m.get(keys[0], -1);
                var removed = m.remove(keys[1]);
                m.erase(keys[2]);
                got == 10 && removed && m.length() == 1 && m.get(keys[3], 99) == 99;
            )");
            check_eq(result.as<bool>(), true);
        });

        test("has_nested_and_map_read_args", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var mi = {};
                mi[7] = true;
                var grid = [[7, 8], [9, 10]];
                var look = {"x": 7};
                mi.has(grid[0][0]) && mi.has(look["x"]) && !mi.has(grid[1][0]);
            )");
            check_eq(result.as<bool>(), true);
        });

        // Literal JSON-style map keys (jaibite v4): {1: "a"} was "Expected expression"
        // while {"s": 0, 1: "a"} parsed - detection only gated on the FIRST entry.
        // Keys keep their runtime type: has(1) hits, has("1") misses.
        test("literal_int_keys_in_map_literal", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var m = {3004: "trooper", 9: "shotgun"};
                m.size() == 2 && m[3004] == "trooper" && m.has(9) && !m.has("9");
            )");
            check_eq(result.as<bool>(), true);
        });

        test("literal_key_types_and_mixing", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var a = {"s": 0, 1: "a", 2.5: "f", true: "b", 'c': "ch"};
                var b = {1: "a", "s": 0};
                a.size() == 5 && a[1] == "a" && a[2.5] == "f" && a[true] == "b" && a['c'] == "ch" &&
                b.size() == 2 && b[1] == "a" && b["s"] == 0;
            )");
            check_eq(result.as<bool>(), true);
        });

        test("bare_identifier_key_still_stringifies", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                int x = 42;
                var m = {x: 1};
                m.has("x") && !m.has(42);
            )");
            check_eq(result.as<bool>(), true);
        });

        test("statement_block_disambiguation_holds", [this]() {
            auto eng = make_engine();
            // A brace starting `<literal> ?` is still a block (ternary statement), not a map
            auto result = eng->execute(R"(
                int r = 0;
                { 1 ? (r = 7) : (r = 9); }
                r;
            )");
            check_eq(result.as<int>(), 7);
            // Negative and computed keys stay out of JSON style (C++ style covers them)
            check_throws([&]() { eng->execute("var m = {-1: \"neg\"}; m;"); });
            check_throws([&]() { eng->execute("var m = {1 + 2: \"sum\"}; m;"); });
            // ...and the C++ style really does cover the negative-key case
            auto neg = eng->execute(R"(var m = {{-1, "neg"}}; m.has(-1);)");
            check_eq(neg.as<bool>(), true);
        });
    }
};

} // namespace jai::foundry::tests

using map_methods_tests = jai::foundry::tests::map_methods_tests;
FOUNDRY_REGISTER(map_methods_tests)
