#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

namespace jai::foundry::tests {

class array_methods_tests : public suite {
public:
    array_methods_tests() : suite("Array Methods") {}

    void forge_tests() override {
        test("index_of_found", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("var arr = [1, 2, 3, 4, 5]; arr.index_of(3);");
            check_eq(result.as<int>(), 2);
        });

        test("index_of_not_found", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("var arr = [1, 2, 3]; arr.index_of(10);");
            check_eq(result.as<int>(), -1);
        });

        test("has_true", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("var arr = [1, 2, 3]; arr.has(2);");
            check_eq(result.as<bool>(), true);
        });

        test("has_false", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("var arr = [1, 2, 3]; arr.has(10);");
            check_eq(result.as<bool>(), false);
        });

        test("contains_alias", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("var arr = [1, 2, 3]; arr.contains(2);");
            check_eq(result.as<bool>(), true);
        });

        test("first", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("var arr = [10, 20, 30]; arr.first();");
            check_eq(result.as<int>(), 10);
        });

        test("last", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("var arr = [10, 20, 30]; arr.last();");
            check_eq(result.as<int>(), 30);
        });

        test("length", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("var arr = [1, 2, 3, 4, 5]; arr.length();");
            check_eq(result.as<int>(), 5);
        });

        test("slice_positive", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("var arr = [1, 2, 3, 4, 5]; arr.slice(1, 3);");
            const auto& arr = result.as_array();
            check_eq(arr.size(), 2);
            check_eq(arr[0].as<int>(), 2);
            check_eq(arr[1].as<int>(), 3);
        });

        test("slice_negative", [this]() {
            auto eng = make_engine();
            auto result = eng->execute("var arr = [1, 2, 3, 4, 5]; arr.slice(-3, -1);");
            const auto& arr = result.as_array();
            check_eq(arr.size(), 2);
            check_eq(arr[0].as<int>(), 3);
            check_eq(arr[1].as<int>(), 4);
        });

        test("filter", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var arr = [1, 2, 3, 4, 5, 6];
                arr.filter([](auto x) -> auto { return x % 2 == 0; });
            )");
            const auto& arr = result.as_array();
            check_eq(arr.size(), 3);
            check_eq(arr[0].as<int>(), 2);
            check_eq(arr[1].as<int>(), 4);
            check_eq(arr[2].as<int>(), 6);
        });

        test("sort", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var arr = [5, 2, 8, 1, 9];
                arr.sort();
                arr;
            )");
            const auto& arr = result.as_array();
            check_eq(arr.size(), 5);
            check_eq(arr[0].as<int>(), 1);
            check_eq(arr[1].as<int>(), 2);
            check_eq(arr[2].as<int>(), 5);
            check_eq(arr[3].as<int>(), 8);
            check_eq(arr[4].as<int>(), 9);
        });

        test("sort_with_custom_comparator", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var arr = [1, 2, 3, 4, 5];
                arr.sort([](auto a, auto b) -> auto { return a > b; });
                arr;
            )");
            const auto& arr = result.as_array();
            check_eq(arr.size(), 5);
            check_eq(arr[0].as<int>(), 5);
            check_eq(arr[1].as<int>(), 4);
            check_eq(arr[2].as<int>(), 3);
            check_eq(arr[3].as<int>(), 2);
            check_eq(arr[4].as<int>(), 1);
        });

        test("reverse", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var arr = [1, 2, 3, 4, 5];
                arr.reverse();
                arr;
            )");
            const auto& arr = result.as_array();
            check_eq(arr.size(), 5);
            check_eq(arr[0].as<int>(), 5);
            check_eq(arr[1].as<int>(), 4);
            check_eq(arr[2].as<int>(), 3);
            check_eq(arr[3].as<int>(), 2);
            check_eq(arr[4].as<int>(), 1);
        });

        test("remove_by_index", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var arr = [1, 2, 3, 4, 5];
                var removed = arr.remove(2);
                removed;
            )");
            check_eq(result.as<bool>(), true);

            result = eng->execute("arr.length();");
            check_eq(result.as<int>(), 4);
        });

        test("remove_if", [this]() {
            auto eng = make_engine();
            auto result = eng->execute(R"(
                var arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
                arr.remove_if([](auto x) -> auto { return x % 2 == 0; });
            )");
            check_eq(result.as<int>(), 5);

            result = eng->execute("arr.length();");
            check_eq(result.as<int>(), 5);
        });
    }
};

} // namespace jai::foundry::tests

using array_methods_tests = jai::foundry::tests::array_methods_tests;
FOUNDRY_REGISTER(array_methods_tests)
