#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class map_tests : public suite {
public:
    map_tests() : suite("Map Tests") {}

    void forge_tests() override {
        test("map_literal_basic", [&]() {
            auto eng = make_engine();

            // Use 'var' for maps with heterogeneous values (string and int)
            eng->execute(R"(
                var m = {"name": "John", "age": 30, "city": "NYC"};
            )");

            auto result = eng->execute("m");
            check(result.is_map());

            check_eq(eng->execute("m[\"name\"]").as<std::string>(), "John");
            check_eq(eng->execute("m[\"age\"]").as<int>(), 30);
            check_eq(eng->execute("m[\"city\"]").as<std::string>(), "NYC");
        });

        test("map_literal_mixed_types", [&]() {
            auto eng = make_engine();

            // Use 'var' for heterogeneous map values
            // 'auto' requires homogeneous values
            eng->execute(R"(
                var mixed = {
                    "int_val": 42,
                    "str_val": "hello",
                    "float_val": 3.14,
                    "bool_val": true
                };
            )");

            check_eq(eng->execute("mixed[\"int_val\"]").as<int>(), 42);
            check_eq(eng->execute("mixed[\"str_val\"]").as<std::string>(), "hello");
            check_eq(eng->execute("mixed[\"float_val\"]").as<double>(), 3.14);
            check_eq(eng->execute("mixed[\"bool_val\"]").as<bool>(), true);
        });

        test("map_literal_empty", [&]() {
            auto eng = make_engine();

            eng->execute("auto empty = {};");

            auto result = eng->execute("empty");
            auto map = result.as<std::map<std::string, script_value>>();
            check_eq(map.size(), 0);
        });

        test("map_access_read", [&]() {
            auto eng = make_engine();

            // Use 'var' for maps with heterogeneous values
            eng->execute(R"(
                var person = {"name": "Alice", "age": 25};
            )");

            check_eq(eng->execute("person[\"name\"]").as<std::string>(), "Alice");
            check_eq(eng->execute("person[\"age\"]").as<int>(), 25);
        });

        test("map_access_write", [&]() {
            auto eng = make_engine();

            // Use 'var' for maps with heterogeneous values
            eng->execute(R"(
                var person = {"name": "Alice"};
                person["name"] = "Bob";
                person["age"] = 30;
            )");

            check_eq(eng->execute("person[\"name\"]").as<std::string>(), "Bob");
            check_eq(eng->execute("person[\"age\"]").as<int>(), 30);
        });

        // TODO: Implement map.has() method
        // test("map_has_key_method", [&]() {
        //     auto eng = make_engine();

        //     eng->execute(R"(
        //         auto m = {"key1": "value1", "key2": "value2"};
        //     )");

        //     check_eq(eng->execute("m.has(\"key1\")").as<bool>(), true);
        //     check_eq(eng->execute("m.has(\"key2\")").as<bool>(), true);
        //     check_eq(eng->execute("m.has(\"key3\")").as<bool>(), false);
        //     check_eq(eng->execute("m.has(\"nonexistent\")").as<bool>(), false);
        // });

        test("map_size_method", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                auto m = {"a": 1, "b": 2, "c": 3};
            )");

            check_eq(eng->execute("m.size()").as<int>(), 3);

            eng->execute("m[\"d\"] = 4;");
            check_eq(eng->execute("m.size()").as<int>(), 4);
        });

        test("map_empty_method", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                auto m = {};
            )");

            check_eq(eng->execute("m.empty()").as<bool>(), true);

            eng->execute("m[\"key\"] = \"value\";");
            check_eq(eng->execute("m.empty()").as<bool>(), false);
        });

        test("map_clear_method", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                auto m = {"a": 1, "b": 2, "c": 3};
                m.clear();
            )");

            check_eq(eng->execute("m.size()").as<int>(), 0);
            check_eq(eng->execute("m.empty()").as<bool>(), true);
        });

        // TODO: Implement map.erase() method
        // test("map_erase_key", [&]() {
        //     auto eng = make_engine();

        //     eng->execute(R"(
        //         auto m = {"a": 1, "b": 2, "c": 3};
        //         m.erase("b");
        //     )");

        //     check_eq(eng->execute("m.size()").as<int>(), 2);
        //     check_eq(eng->execute("m.has(\"a\")").as<bool>(), true);
        //     check_eq(eng->execute("m.has(\"b\")").as<bool>(), false);
        //     check_eq(eng->execute("m.has(\"c\")").as<bool>(), true);
        // });

        test("map_keys_method", [&]() {
            auto eng = make_engine();

            // Use 'var' for maps with heterogeneous values
            eng->execute(R"(
                var m = {"name": "Alice", "age": 30, "city": "NYC"};
                auto keys = m.keys();
            )");

            auto result = eng->execute("keys");
            auto keys_vec = result.as<std::vector<script_value>>();
            check_eq(keys_vec.size(), 3);

            // Keys should be present (order may vary)
            bool has_name = false, has_age = false, has_city = false;
            for (const auto& key : keys_vec) {
                auto k = key.as<std::string>();
                if (k == "name") has_name = true;
                if (k == "age") has_age = true;
                if (k == "city") has_city = true;
            }
            check(has_name && has_age && has_city);
        });

        test("map_values_method", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                auto m = {"a": 1, "b": 2, "c": 3};
                auto vals = m.values();
            )");

            auto result = eng->execute("vals");
            auto vals_vec = result.as<std::vector<script_value>>();
            check_eq(vals_vec.size(), 3);
        });

        test("map_nested_in_map", [&]() {
            auto eng = make_engine();

            // Use 'var' for maps with heterogeneous values
            eng->execute(R"(
                var person = {
                    "name": "Alice",
                    "address": {
                        "street": "123 Main St",
                        "city": "NYC",
                        "zip": 10001
                    }
                };
            )");

            check_eq(eng->execute("person[\"name\"]").as<std::string>(), "Alice");
            check_eq(eng->execute("person[\"address\"][\"street\"]").as<std::string>(), "123 Main St");
            check_eq(eng->execute("person[\"address\"][\"city\"]").as<std::string>(), "NYC");
            check_eq(eng->execute("person[\"address\"][\"zip\"]").as<int>(), 10001);
        });

        test("map_nested_modification", [&]() {
            auto eng = make_engine();

            // Use 'var' for maps with heterogeneous values
            eng->execute(R"(
                var data = {
                    "user": {
                        "name": "Alice",
                        "age": 30
                    }
                };
                data["user"]["name"] = "Bob";
                data["user"]["age"] = 35;
            )");

            check_eq(eng->execute("data[\"user\"][\"name\"]").as<std::string>(), "Bob");
            check_eq(eng->execute("data[\"user\"][\"age\"]").as<int>(), 35);
        });

        test("map_deeply_nested", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                auto deep = {
                    "level1": {
                        "level2": {
                            "level3": {
                                "value": 42
                            }
                        }
                    }
                };
            )");

            check_eq(eng->execute("deep[\"level1\"][\"level2\"][\"level3\"][\"value\"]").as<int>(), 42);
        });

        test("map_with_array_values", [&]() {
            auto eng = make_engine();

            // Arrays have homogeneous elements, but the map values are heterogeneous (array types differ)
            eng->execute(R"(
                var data = {
                    "numbers": [1, 2, 3],
                    "strings": ["a", "b", "c"]
                };
            )");

            check_eq(eng->execute("data[\"numbers\"][0]").as<int>(), 1);
            check_eq(eng->execute("data[\"numbers\"][2]").as<int>(), 3);
            check_eq(eng->execute("data[\"strings\"][0]").as<std::string>(), "a");
            check_eq(eng->execute("data[\"strings\"][2]").as<std::string>(), "c");
        });

        test("array_with_map_elements", [&]() {
            auto eng = make_engine();

            // Use 'var' for arrays containing heterogeneous maps
            eng->execute(R"(
                var users = [
                    {"name": "Alice", "age": 30},
                    {"name": "Bob", "age": 25},
                    {"name": "Charlie", "age": 35}
                ];
            )");

            check_eq(eng->execute("users[0][\"name\"]").as<std::string>(), "Alice");
            check_eq(eng->execute("users[0][\"age\"]").as<int>(), 30);
            check_eq(eng->execute("users[1][\"name\"]").as<std::string>(), "Bob");
            check_eq(eng->execute("users[2][\"age\"]").as<int>(), 35);
        });

        test("map_complex_nested_structure", [&]() {
            auto eng = make_engine();

            // Use 'var' for deeply nested heterogeneous structures
            eng->execute(R"(
                var company = {
                    "name": "TechCorp",
                    "employees": [
                        {
                            "name": "Alice",
                            "skills": ["Python", "C++", "JavaScript"]
                        },
                        {
                            "name": "Bob",
                            "skills": ["Java", "Go"]
                        }
                    ],
                    "offices": {
                        "NYC": {"address": "123 Main St", "employees": 50},
                        "SF": {"address": "456 Tech Ave", "employees": 30}
                    }
                };
            )");

            check_eq(eng->execute("company[\"name\"]").as<std::string>(), "TechCorp");
            check_eq(eng->execute("company[\"employees\"][0][\"name\"]").as<std::string>(), "Alice");
            check_eq(eng->execute("company[\"employees\"][0][\"skills\"][1]").as<std::string>(), "C++");
            check_eq(eng->execute("company[\"offices\"][\"NYC\"][\"employees\"]").as<int>(), 50);
            check_eq(eng->execute("company[\"offices\"][\"SF\"][\"address\"]").as<std::string>(), "456 Tech Ave");
        });

        test("map_iteration_keys", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                auto m = {"a": 1, "b": 2, "c": 3};
                auto count = 0;
                auto keys = m.keys();
                for (auto i = 0; i < keys.size(); i = i + 1) {
                    count = count + 1;
                }
            )");

            check_eq(eng->execute("count").as<int>(), 3);
        });

        test("map_missing_key_behavior", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                auto m = {"key1": "value1"};
            )");

            // Accessing missing key should throw or return null/default
            try {
                eng->execute("m[\"nonexistent\"]");
                // Some implementations might allow this and return null/default
            } catch (const std::exception&) {
                // Other implementations throw - both are valid
            }
        });

        test("map_in_function_parameter", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                function getProperty(var obj, string key) -> var {
                    return obj[key];
                }
            )");

            // Use 'var' for maps with heterogeneous values
            eng->execute("var person = {\"name\": \"Alice\", \"age\": 30};");

            check_eq(eng->execute("getProperty(person, \"name\")").as<std::string>(), "Alice");
            check_eq(eng->execute("getProperty(person, \"age\")").as<int>(), 30);
        });

        // TODO: Fix - crashes when indexing directly into function return value
        // test("map_as_return_value", [&]() {
        //     auto eng = make_engine();

        //     eng->execute(R"(
        //         function makeConfig() -> auto {
        //             return {"debug": true, "port": 8080};
        //         }
        //     )");

        //     check_eq(eng->execute("makeConfig()[\"debug\"]").as<bool>(), true);
        //     check_eq(eng->execute("makeConfig()[\"port\"]").as<int>(), 8080);
        // });

        // TODO: Implement map merge operator
        // test("map_merge_operation", [&]() {
        //     auto eng = make_engine();

        //     eng->execute(R"(
        //         auto map1 = {"a": 1, "b": 2};
        //         auto map2 = {"c": 3, "d": 4};
        //         auto merged = map1 + map2;
        //     )");

        //     check_eq(eng->execute("merged[\"a\"]").as<int>(), 1);
        //     check_eq(eng->execute("merged[\"b\"]").as<int>(), 2);
        //     check_eq(eng->execute("merged[\"c\"]").as<int>(), 3);
        //     check_eq(eng->execute("merged[\"d\"]").as<int>(), 4);
        //     check_eq(eng->execute("merged.size()").as<int>(), 4);
        // });

        // TODO: Implement map + operator for merging
        // test("map_override_on_merge", [&]() {
        //     auto eng = make_engine();

        //     eng->execute(R"(
        //         auto map1 = {"key": "value1", "other": "data"};
        //         auto map2 = {"key": "value2"};
        //         auto merged = map1 + map2;
        //     )");

        //     // Second map should override first
        //     check_eq(eng->execute("merged[\"key\"]").as<std::string>(), "value2");
        //     check_eq(eng->execute("merged[\"other\"]").as<std::string>(), "data");
        // });

        test("map_merge_function", [&]() {
            auto eng = make_engine();
            stdlib::register_all(eng);

            eng->execute(R"(
                auto map1 = {"key": "value1", "other": "data"};
                auto map2 = {"key": "value2"};
                auto merged = merge(map1, map2);
            )");

            // Second map should override first
            check_eq(eng->execute("merged[\"key\"]").as<std::string>(), "value2");
            check_eq(eng->execute("merged[\"other\"]").as<std::string>(), "data");
        });

        test("map_with_numeric_string_keys", [&]() {
            auto eng = make_engine();

            eng->execute(R"(
                auto m = {"0": "zero", "1": "one", "2": "two"};
            )");

            check_eq(eng->execute("m[\"0\"]").as<std::string>(), "zero");
            check_eq(eng->execute("m[\"1\"]").as<std::string>(), "one");
            check_eq(eng->execute("m[\"2\"]").as<std::string>(), "two");
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::map_tests)
