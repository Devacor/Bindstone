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

        test("merge_arity_text_spells_the_count", [&]() {
            auto eng = make_engine();
            stdlib::register_all(eng);
            auto caught = eng->execute(R"(
                var text = "";
                try { merge({"a": 1}); } catch (e) { text = e; }
                text
            )");
            check_eq(std::string("merge() expects exactly 2 arguments, got 1"), caught.as<std::string>());
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

        // A map key is read-only (C++ `const Key`, the ordering invariant), by value AND by
        // reference; `auto& kv` aliases the value only. The pair's `first` stays a SHARED
        // handle of the key (no per-iteration copy - Dev ruling 2026-09-04); the checker
        // rejects the mutation spellings it can see under strict, and the runtime backstop
        // is the string copy-on-write gate (docs/invariants.md 11): whatever the checker
        // cannot see mutates a copy and the map is never touched. Every row runs on both
        // backends and pins byte-identical output.
        auto both_mode = [](const char* src, check_mode mode) {
            std::string out[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                e->set_backend(use_vm ? jai::backend_type::vm : jai::backend_type::interpreter);
                e->execution_budget(0);
                e->static_checking(mode);
                jai::stdlib::register_all(e);
                try { out[idx] = e->execute(src).to_string(); }
                catch (const std::exception& ex) { out[idx] = std::string("ERROR: ") + ex.what(); }
                ++idx;
            }
            return std::make_pair(out[0], out[1]);
        };

        test("map_key_read_only_static_battery", [this, both_mode]() {
            const std::string text = "map keys are read-only";   // the operand spelling follows the pair variable (kv.first, p.first)
            auto rejected = [&](const char* src, const char* what) {
                auto [i_out, v_out] = both_mode(src, check_mode::strict);
                check_eq(i_out, v_out, std::string("backend parity: ") + what);
                check(i_out.find("ERROR: static check failed") == 0, std::string("load error: ") + what + " (got: " + i_out + ")");
                check(i_out.find("cannot mutate ") != std::string::npos && i_out.find(text) != std::string::npos, std::string("text: ") + what + " (got: " + i_out + ")");
            };
            auto accepted = [&](const char* src, const std::string& expected, const char* what) {
                auto [i_out, v_out] = both_mode(src, check_mode::strict);
                check_eq(expected, i_out, std::string("interp: ") + what);
                check_eq(expected, v_out, std::string("vm: ") + what);
            };
            rejected("map<string,int> m = {\"a\": 1}; for (auto kv : m) { kv.first.to_upper(); } 0", "by-value pair, string mutator");
            rejected("map<string,int> m = {\"a\": 1}; for (auto& kv : m) { kv.first.to_upper(); } 0", "by-reference pair, string mutator");
            rejected("map<string,int> m = {\"a\": 1}; for (auto kv : m) { kv.first.insert(0, \"x\"); } 0", "insert");
            rejected("map<string,int> m = {\"a\": 1}; for (auto kv : m) { kv.first.erase(0, 1); } 0", "erase");
            rejected("map<string,int> m = {\"a\": 1}; for (auto kv : m) { kv.first.replace_all(\"a\", \"b\"); } 0", "replace_all");
            rejected("map<string,int> m = {\"a\": 1}; for (auto kv : m) { kv.first = \"z\"; } 0", "store");
            rejected("map<string,int> m = {\"a\": 1}; for (auto kv : m) { kv.first += \"!\"; } 0", "compound store");
            rejected("map<int,int> m = {1: 1}; for (auto kv : m) { kv.first++; } 0", "postfix ++ (desugars to += 1)");
            rejected("map<int,int> m = {1: 1}; for (auto& kv : m) { --kv.first; } 0", "prefix -- by reference");
            rejected("auto m = {\"a\": 1}; for (auto kv : m) { kv.first.to_upper(); } 0", "auto map literal is statically a map");
            rejected("map<string,int> m = {\"a\": 1}; for (auto& kv : m) { kv.first += \"hey\"; } 0", "Dev's spelling: kv.first += \"hey\" by reference");
            rejected("map<string,int> m = {\"a\": 1}; for (auto kv : m) { var p = kv; p.first.to_upper(); } 0", "a copied pair still holds the key (C++ pair<const K, V>)");
            rejected("map<string,int> m = {\"a\": 1}; for (auto& kv : m) { auto& p = kv; p.first.insert(0, \"x\"); } 0", "an alias of the pair still holds the key");
            rejected("map<string,int> m = {\"a\": 1}; for (auto kv : m) { auto f = [&]() { kv.first.to_upper(); }; f(); } 0", "the pair reached through a [&] lambda");
            // Alias spellings: a mutable alias of a key has no C++ equivalent (const Key) - the
            // fix is always the copy, which is copy-on-write at runtime
            auto rejected_alias = [&](const char* src, const char* what) {
                auto [i_out, v_out] = both_mode(src, check_mode::strict);
                check_eq(i_out, v_out, std::string("backend parity: ") + what);
                check(i_out.find("ERROR: static check failed") == 0, std::string("load error: ") + what + " (got: " + i_out + ")");
                check(i_out.find("cannot alias ") != std::string::npos &&
                      i_out.find("map keys are read-only (copy it instead: var k = ") != std::string::npos,
                      std::string("alias text: ") + what + " (got: " + i_out + ")");
            };
            rejected_alias("map<string,int> m = {\"a\": 1}; for (auto& kv : m) { var& cheeky = kv.first; } 0", "Dev's spelling: var& cheeky = kv.first");
            rejected_alias("map<string,int> m = {\"a\": 1}; for (auto kv : m) { auto& k = kv.first; } 0", "auto& alias of the key");
            rejected_alias("map<string,int> m = {\"a\": 1}; for (auto kv : m) { var& k = kv.first; } 0", "var& alias of the key");
            rejected_alias("function up(string& s) { s.to_upper(); } map<string,int> m = {\"a\": 1}; for (auto kv : m) { up(kv.first); } 0", "passed to a string& parameter");
            rejected_alias("map<string,int> m = {\"a\": 1}; for (auto& kv : m) { shared_ptr<auto> cheeky = kv.first; } 0", "Dev's spelling (shared_ptr<var> is not a spelling; shared_ptr<auto> is): shared_ptr<auto> cheeky = kv.first");
            rejected_alias("map<string,int> m = {\"a\": 1}; for (auto kv : m) { shared_ptr<string> c = kv.first; } 0", "shared_ptr<string> from the key");
            rejected_alias("map<string,int> m = {\"a\": 1}; for (auto kv : m) { shared_ptr<auto> c = kv.first; } 0", "shared_ptr<auto> from the key");
            rejected_alias("map<string,int> m = {\"a\": 1}; shared_ptr<string> h = null; for (auto kv : m) { h = kv.first; } 0", "store into a shared_ptr-declared variable");
            rejected_alias("map<string,int> m = {\"a\": 1}; for (auto kv : m) { var& c = cast<string&>(kv.first); } 0", "cast<string&> of the key");
            rejected_alias("function keep(shared_ptr<string> s) { return 0; } map<string,int> m = {\"a\": 1}; for (auto kv : m) { keep(kv.first); } 0", "passed to a shared_ptr<string> parameter");
            rejected_alias("map<string,int> m = {\"a\": 1}; function first_key() -> string& { for (auto& kv : m) { return kv.first; } } first_key(); 0", "returned by reference (a map<K,V>& parameter is dynamic to the checker: a global map here)");
            rejected_alias("map<string,int> m = {\"a\": 1}; function first_key() -> shared_ptr<string> { for (auto kv : m) { return kv.first; } return null; } first_key(); 0", "returned as a shared_ptr");
            rejected_alias("map<string,int> m = {\"a\": 1}; for (auto kv : m) { var p = kv; var& c = p.first; } 0", "alias through a copied pair");
            rejected_alias("map<string,int> m = {\"a\": 1}; string other = \"o\"; bool flag = true; for (auto kv : m) { var& c = flag ? kv.first : other; } 0", "a ternary over the key keeps the marker");
            accepted("map<string,int> m = {\"a\": 1}; string r = \"\"; for (auto& kv : m) { var okay = kv.first; okay.to_upper(); okay += \"!\"; r += okay; } r + to_json(m)", "A!{\"a\":1}", "Dev's spelling: var okay = kv.first is the copy-on-write path");
            accepted("map<string,int> m = {\"a\": 1}; string r = \"\"; for (auto kv : m) { string s2 = kv.first; s2.to_upper(); string u = cast<string>(kv.first); u += \"?\"; r += s2 + u; } r + to_json(m)", "Aa?{\"a\":1}", "typed copies and a by-value cast");
            accepted("map<string,int> m = {\"a\": 1}; function first_key() -> string { for (auto kv : m) { return kv.first; } return \"\"; } string k = first_key(); k.to_upper(); k + to_json(m)", "A{\"a\":1}", "returned by value: a copy");
            accepted("map<string,int> m = {\"a\": 1}; string r = \"\"; for (auto kv : m) { auto f = [=]() { return kv.first + \"x\"; }; r += f(); } r", "ax", "reading the key inside a lambda");
            accepted("map<string,int> m = {\"a\": 1, \"b\": 2}; string s = \"\"; for (auto kv : m) { s += kv.first; s += to_string(kv.first.length()); } s", "a1b1", "reads of the key are fine");
            accepted("map<string,int> m = {\"a\": 1}; for (auto& kv : m) { kv.second = kv.second * 10; kv.second += 1; } to_string(m[\"a\"])", "11", "the value stays writable by reference");
            accepted("map<string,string> m = {\"a\": \"v\"}; for (auto& kv : m) { kv.second.to_upper(); } m[\"a\"]", "V", "a string value mutator by reference");
            accepted("function show(string s) { return s; } map<string,int> m = {\"a\": 1}; string r = \"\"; for (auto kv : m) { r += show(kv.first); } r", "a", "passed by value");
            accepted("var m = {\"a\": 1}; for (auto kv : m) { kv.first.to_upper(); } to_json(m)", "{\"a\":1}", "a var map is dynamic: the runtime backstop keeps the key");
            accepted("map<string,int> m = {\"a\": 1}; for (auto kv : m) { var k = kv.first; k.to_upper(); } to_json(m)", "{\"a\":1}", "a var copy of the key is the checker-invisible path: runtime backstop");
            accepted("var arr = [[\"a\", 1]]; for (auto kv : arr) { kv[0].to_upper(); } string s = to_json(arr); for (auto& kv : arr) { kv[0].to_upper(); } s + to_json(arr)", "[[\"a\",1]][[\"A\",1]]", "an array of pairs is not a map: by value copies, by reference writes through");
        });

        test("map_key_immutable_battery", [this, both_mode]() {
            auto row = [&](const char* src, const std::string& expected, const char* what) {
                auto [i_out, v_out] = both_mode(src, check_mode::off);
                check_eq(expected, i_out, std::string("interp: ") + what);
                check_eq(expected, v_out, std::string("vm: ") + what);
            };
            row("map<string,int> m = {\"a\": 1, \"b\": 2}; string seen = \"\"; for (auto kv : m) { kv.first.to_upper(); seen += kv.first; } to_json(m) + seen + to_string(m.contains(\"a\"))",
                "{\"a\":1,\"b\":2}ABtrue", "by-value pair: kv.first mutates the pair's copy, never the key");
            row("map<string,int> m = {\"a\": 1, \"b\": 2}; for (auto& kv : m) { kv.first.to_upper(); kv.second = kv.second * 10; } to_json(m)",
                "{\"a\":10,\"b\":20}", "by-reference pair: second aliases the value, first is a copy");
            row("map<string,int> m = {\"a\": 1, \"b\": 2}; for (auto& kv : m) { kv.first.to_upper(); kv.first += \"!\"; } to_json(m)",
                "{\"a\":1,\"b\":2}", "by-reference pair: mutator and += on first leave the key");
            row("map<string,int> m = {\"a\": 1, \"b\": 2}; for (auto kv : m) { var k = kv.first; k.to_upper(); k += \"!\"; } to_json(m)",
                "{\"a\":1,\"b\":2}", "var-sourced key copy mutated");
            row("map<string,int> m = {\"a\": 1, \"b\": 2}; string r = \"\"; for (auto kv : m) { var p = kv; p.first.to_upper(); r += p.first + kv.first; } to_json(m) + r",
                "{\"a\":1,\"b\":2}AaBb", "the pair escaping whole: the copy's first mutates, the loop pair and the key do not");
            row("map<string,int> m = {\"a\": 1, \"b\": 2}; var ks = m.keys(); ks[0].to_upper(); to_json(m) + to_json(ks)",
                "{\"a\":1,\"b\":2}[\"A\",\"b\"]", "keys() hands out shared handles; the element detaches on write");
            row("map<string,int> m = {\"b\": 2, \"a\": 1, \"c\": 3}; for (auto& kv : m) { kv.first.to_upper(); } m[\"a\"] = 100; string o = \"\"; for (auto kv : m) { o += kv.first; } o + to_string(m.size()) + to_string(m[\"a\"])",
                "abc3100", "iteration order and lookups hold after the attempt");
            row("map<var,int> m = {\"a\": 1, \"b\": 2}; for (auto kv : m) { kv.first.to_upper(); } for (auto& kv : m) { kv.first.to_upper(); } to_json(m) + to_string(m.contains(\"a\"))",
                "{\"a\":1,\"b\":2}true", "string keys in map<var,V>, both shapes");
            row("string k = \"a\"; map<string,int> m = {}; m[k] = 1; k.to_upper(); k += \"!\"; to_json(m) + to_string(m.contains(\"a\")) + k",
                "{\"a\":1}trueA!", "a key inserted through a variable shares its node until the variable is written");
            row("map<string,int> m = {\"y\": 1}; var f = m.filter([](k, v) { k.to_upper(); return true; }); to_json(m) + to_json(f)",
                "{\"y\":1}{\"y\":1}", "filter predicate argument detaches on write");
            row("map<string,int> m = {\"z\": 1}; m.remove_if([](k, v) { k.to_upper(); return false; }); to_json(m)",
                "{\"z\":1}", "remove_if predicate argument detaches on write");
            row("map<string,int> m = {\"v\": 1}; var e = m.to_array(); e[0][0].to_upper(); to_json(m) + to_json(e)",
                "{\"v\":1}[[\"V\",1]]", "to_array() entries detach on write");
            row("function tk(string s) { s.to_upper(); return s; } function tkr(string& s) { s.to_upper(); return s; } map<string,int> m = {\"u\": 1}; string r = \"\"; for (auto kv : m) { r += tk(kv.first) + kv.first; tkr(kv.first); r += kv.first; } to_json(m) + r",
                "{\"u\":1}UuU", "kv.first through a by-value and a string& parameter: the pair's own copy is the lvalue");
            row("map<string,int> m = {\"s\": 1}; string r = \"\"; for (auto kv : m) { auto& kr = kv.first; kr.to_upper(); r += kr + kv.first; } to_json(m) + r",
                "{\"s\":1}SS", "auto& kr = kv.first aliases the pair's copy");
            row("map<string,int> m = {\"w\": 1}; for (auto kv : m) { kv.first += \"!\"; kv.first = \"zzz\"; } to_json(m)",
                "{\"w\":1}", "stores to kv.first replace the pair field");
            row("map<int,string> m = {1: \"a\", 2: \"b\"}; for (auto kv : m) { kv.second.to_upper(); } string s1 = to_json(m); for (auto& kv : m) { kv.second.to_upper(); } s1 + to_json(m)",
                "{\"$map\":[[1,\"a\"],[2,\"b\"]]}{\"$map\":[[1,\"A\"],[2,\"B\"]]}", "int keys: by-value second is a copy, by-reference second writes through");
            row("map<string,string> m = {\"k\": \"v\"}; m[\"k\"].to_upper(); string a = to_json(m); string key = \"k\"; m[key].to_lower(); a + to_json(m)",
                "{\"k\":\"V\"}{\"k\":\"v\"}", "map value through a subscript receiver mutates in place");
            row("string src = \"v\"; map<string,string> m = {\"k\": src}; m[\"k\"].to_upper(); to_json(m) + src",
                "{\"k\":\"V\"}v", "a map value copied from a variable detaches on write");
        });

        // The map's contract (Dev's map-order ruling): iteration and serialization see key order, never insertion order; an
        // iteration continues after its key when the map changes under it (a callee's stores and erases included); a reference
        // to a value survives the table's growth; keys of different kinds stay distinct; == and JSON follow key order
        test("map_contract_battery", [this, both_mode]() {
            const char* src = R"JAI(
string log = "";
map<var, int> m = {};
for (int i = 0; i < 2000; ++i) { m[(i * 7919) % 2000] = i; }
for (int i = 0; i < 2000; i += 2) { m.erase(i); }
for (int i = 3000; i > 2000; i -= 3) { m[i] = 1; }
int prev = -1; bool sorted = true; int n = 0;
for (auto [k, v] : m) { if (k <= prev) { sorted = false; } prev = k; n += 1; }
log += to_string(sorted) + " " + to_string(n) + " " + to_string(m.size());
var mixed = {};
mixed["b"] = 1; mixed[3] = 2; mixed["a"] = 3; mixed[1] = 4; mixed[2.5] = 5; mixed[true] = 6; mixed['c'] = 7;
string order = "";
for (auto [k, v] : mixed) { order += to_string(k) + ":" + to_string(v) + ","; }
log += " | " + order;
var e = {};
for (int i = 0; i < 10; ++i) { e[i] = i; }
string seen = "";
for (auto [k, v] : e) { seen += to_string(k); if (k == 3) { e.erase(4); e.erase(3); e[11] = 0; } }
log += " | " + seen;
map<string, int> refs = {};
refs["k"] = 1;
auto& r = refs["k"];
for (int i = 0; i < 3000; ++i) { refs["x" + to_string(i)] = i; }
r = 42;
log += " | " + to_string(refs["k"]) + " " + to_string(refs.size());
var u = {};
u["ab"] = 1;
u[utf8_string("ab")] = 2;
log += " | " + to_string(u.size());
var f = {};
f[0.0] = 1; f[-0.0] = 2; f[math::NAN_VALUE] = 3; f[-math::NAN_VALUE] = 4; f[1] = 5; f[1.0] = 6;
string fs = "";
for (auto [k, v] : f) { fs += to_string(v) + ","; }
log += " | " + to_string(f.size()) + " " + fs;
var a = {"z": 1, "y": 2, "x": [1, 2]};
var b = {};
b["x"] = [1, 2]; b["z"] = 1; b["y"] = 2;
log += " | " + to_string(a == b) + " " + to_json(a) + " " + to_json(from_json(to_json(b)));
var c = {};
for (int i = 0; i < 50; ++i) { c[i] = i; }
int keys_seen = 0;
void grow(var& target, int at) { for (int j = 100; j < 400; ++j) { target[j] = j; } target.erase(at + 1); }
for (auto [k, v] : c) { keys_seen += 1; if (k == 10) { grow(c, 10); } }
log += " | " + to_string(keys_seen) + " " + to_string(c.size());
var d = {};
for (int i = 0; i < 20; ++i) { d[i] = i; }
int calls = 0;
d.filter([&](var k, var v) -> bool { calls += 1; if (k == 5) { d[500] = 1; d.erase(6); } return k % 2 == 0; });
string ds = "";
for (auto [k, v] : d) { ds += to_string(k) + ","; }
log += " | " + to_string(calls) + " " + ds;
log;
)JAI";
            auto [i_out, v_out] = both_mode(src, check_mode::strict);
            check_eq(std::string("true 1334 1334 | 1:4,3:2,2.5:5,a:3,b:1,c:7,true:6, | 01235678911 | 42 3001 | 2 | 5 5,4,2,6,3, | "
                                 "true {\"x\":[1,2],\"y\":2,\"z\":1} {\"x\":[1,2],\"y\":2,\"z\":1} | 349 349 | 19 0,2,4,8,10,12,14,16,18,500,"), i_out, "interpreter");
            check_eq(i_out, v_out, "backend parity");
        });

        // string keys around the eight-byte short form: lengths 0..9, embedded NULs, a string and a utf8_string of the same
        // bytes, a key read as a member, a built key equal to a stored one, an erase
        test("map_short_string_keys", [this, both_mode]() {
            const char* src = R"JAI(
var m = {};
array<string> ks = ["", "a", "ab", "abcdefg", "abcdefgh", "abcdefghi", "abcdefgh!", "a\x00", "a\x00b", "hp"];
for (int i = 0; i < ks.size(); ++i) { m[ks[i]] = i; }
m[utf8_string("ab")] = 100;
m[utf8_string("abcdefgh")] = 101;
string out = to_string(m.size());
for (int i = 0; i < ks.size(); ++i) { out += " " + to_string(m[ks[i]]); }
out += " " + to_string(m[utf8_string("ab")]) + " " + to_string(m[utf8_string("abcdefgh")]) + " " + to_string(m.hp);
string copy = "abcdefg";
copy += "h";
out += " " + to_string(m[copy]) + " " + to_string(m.has("abcdefgh\x00")) + " " + to_string(m.has("a"));
m.erase("abcdefgh");
out += " " + to_string(m.has("abcdefgh")) + " " + to_string(m[utf8_string("abcdefgh")]);
out;
)JAI";
            auto [i_out, v_out] = both_mode(src, check_mode::strict);
            check_eq(std::string("12 0 1 2 3 4 5 6 7 8 9 100 101 9 4 false true false 101"), i_out, "interpreter");
            check_eq(i_out, v_out, "backend parity");
        });

        // script_map::recall / recall_or_insert against find through every mutator: an entry memo holds only while its map is
        // unchanged and the remembered key equals the asked key; a copy's memo names its source; a new map at a dead map's address
        // starts with no memo
        test("map_entry_memo_through_every_mutator", [this]() {
            auto e = jai::engine::make();
            auto key = [&](const std::string& t) { return jai::script_value(t, e.get()); };
            auto num = [&](int64_t v) { return jai::script_value(static_cast<jai::script_int>(v), e.get()); };
            jai::script_map m;
            jai::script_map::entry_memo memo;
            auto agrees = [&](jai::script_map& map, const std::string& k, const char* step) {
                const jai::script_map::value_type* r = map.recall(memo, key(k));
                auto it = map.find(key(k));
                const jai::script_map::value_type* f = it == map.end() ? nullptr : &*it;
                check_true(r == f, std::string("recall agrees with find after ") + step + " (" + k + ")");
            };
            m[key("a")] = num(1);
            agrees(m, "a", "an insert");
            m.recall_or_insert(memo, key("a"))->second = num(2);
            check_eq(int64_t(2), m.find(key("a"))->second.as_int(), "a store through the memo lands in the entry");
            m[key("b")] = num(3);                         agrees(m, "a", "another insert");
            m[key("a")] = num(4);                         agrees(m, "a", "a value store");
            m.erase(key("a"));                            agrees(m, "a", "an erase of the key");
            check_true(m.recall(memo, key("a")) == nullptr, "an erased key is absent");
            m.recall_or_insert(memo, key("a"))->second = num(5);
            agrees(m, "a", "an insert through the memo");
            m.erase(key("b"));                            agrees(m, "a", "an erase of another key");
            for (int i = 0; i < 2000; ++i) { m[key("g" + std::to_string(i))] = num(i); }
            agrees(m, "a", "growth");
            agrees(m, "g1999", "growth");
            m.clear();                                    agrees(m, "a", "clear");
            m[key("a")] = num(6); agrees(m, "a", "a reinsert");
            jai::script_map other; other[key("a")] = num(7);
            m = other;                                    agrees(m, "a", "a copy assignment");
            check_eq(int64_t(7), m.recall(memo, key("a"))->second.as_int(), "the assigned map's value");
            jai::script_map moved; moved[key("a")] = num(8);
            m = std::move(moved);                         agrees(m, "a", "a move assignment");
            jai::script_map copy(m);
            m.recall(memo, key("a"));
            const jai::script_map::value_type* in_copy = copy.recall(memo, key("a"));
            check_true(in_copy == &*copy.find(key("a")), "a memo taken on the source misses on the copy");
            alignas(jai::script_map) unsigned char buf[sizeof(jai::script_map)];
            auto* first = new (buf) jai::script_map();
            (*first)[key("a")] = num(9);
            check_true(first->recall(first->lane_memo(), key("a")) != nullptr, "the first map's lane memo holds");
            first->~script_map();
            auto* second = new (buf) jai::script_map();
            (*second)[key("a")] = num(10);
            const jai::script_map::value_type* r2 = second->recall(second->lane_memo(), key("a"));
            check_true(r2 == &*second->find(key("a")) && r2->second.as_int() == 10, "a new map at the dead map's address starts without its memo");
            second->~script_map();
        });

        test("map_field_survives_hot_reload", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                e->set_backend(use_vm ? jai::backend_type::vm : jai::backend_type::interpreter);
                jai::stdlib::register_all(e);
                e->execute("class Inv { map<string, int> items = {}; }\nvar inv = Inv();\n"
                           "for (int i = 0; i < 300; ++i) { inv.items[\"k\" + to_string(i)] = i; }");
                e->execute("class Inv { map<string, int> items = {}; int extra = 7; }");
                const std::string got = e->execute(
                    "string first = \"\"; for (auto [k, v] : inv.items) { if (first == \"\") { first = k; } }\n"
                    "inv.items[\"k5\"] = 50; inv.items.erase(\"k6\");\n"
                    "to_string(inv.items.size()) + \" \" + to_string(inv.items[\"k42\"]) + \" \" + to_string(inv.items[\"k5\"]) + \" \" + first + \" \" + to_string(inv.extra);").to_string();
                check_eq(std::string("299 42 50 k0 7"), got, use_vm ? "vm" : "interpreter");
            }
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::map_tests)
