#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai::foundry;

namespace jai::foundry::tests {

class range_based_for_tests : public suite {
public:
    range_based_for_tests() : suite("range_based_for") {}
    
    void forge_tests() override {
        test("array_value_iteration", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto numbers = [1, 2, 3, 4, 5];
                auto sum = 0;
                for (auto x : numbers) {
                    sum += x;
                }
                sum
            )");
            
            check_eq(result.as<int>(), 15);
        });
        
        test("array_reference_iteration", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto numbers = [1, 2, 3, 4, 5];
                for (auto& x : numbers) {
                    x *= 2;
                }
                auto sum = 0;
                for (auto x : numbers) {
                    sum += x;
                }
                sum
            )");
            
            check_eq(result.as<int>(), 30);
        });
        
        test("map_value_iteration", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto scores = {"alice": 100, "bob": 85, "charlie": 92};
                auto total = 0;
                for (auto kv : scores) {
                    total += kv.second;
                }
                total
            )");
            
            check_eq(result.as<int>(), 277);
        });
        
        test("map_reference_iteration", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto scores = {"alice": 100, "bob": 85, "charlie": 92};
                
                // Add 10 bonus points to each score
                for (auto& kv : scores) {
                    kv.second += 10;
                }
                
                scores["alice"] + scores["bob"] + scores["charlie"]
            )");
            
            check_eq(result.as<int>(), 307);
        });
        
        test("map_key_const_check", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            // `kv.first = ...` is a strict load error (Map Tests :: map_key_read_only_static_battery);
            // this pins the runtime backstop: the store lands on the pair's copy, never the key
            engine->static_checking(check_mode::off);
            
            auto result = engine->execute(R"(
                auto test_map = {"key1": 100, "key2": 200};
                auto original_keys = "";
                for (auto kv : test_map) {
                    original_keys += kv.first + " ";
                }
                
                // Modifying kv.first should not affect the actual map keys
                for (auto& kv : test_map) {
                    kv.first = "modified";
                }
                
                auto after_keys = "";
                for (auto kv : test_map) {
                    after_keys += kv.first + " ";
                }
                
                original_keys == after_keys
            )");
            
            check_eq(result.as<bool>(), true);
        });
        
        test("nested_iteration", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto matrix = [[1, 2], [3, 4], [5, 6]];
                auto sum = 0;
                for (auto row : matrix) {
                    for (auto val : row) {
                        sum += val;
                    }
                }
                sum
            )");
            
            check_eq(result.as<int>(), 21);
        });
        
        test("break_in_range_for", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto numbers = [1, 2, 3, 4, 5];
                auto sum = 0;
                for (auto x : numbers) {
                    if (x > 3) {
                        break;
                    }
                    sum += x;
                }
                sum
            )");

            std::cout << "break_in_range_for result: " << result.as<int>() << " (expected 6)" << std::endl;
            check_eq(result.as<int>(), 6);
        });

        test("continue_in_range_for", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);

            auto result = engine->execute(R"(
                auto numbers = [1, 2, 3, 4, 5];
                auto sum = 0;
                for (auto x : numbers) {
                    if (x == 3) {
                        continue;
                    }
                    sum += x;
                }
                sum
            )");

            std::cout << "continue_in_range_for result: " << result.as<int>() << " (expected 12)" << std::endl;
            check_eq(result.as<int>(), 12);
        });
        
        test("empty_container_iteration", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto empty_array = [];
                auto count = 0;
                for (auto x : empty_array) {
                    count += 1;
                }
                
                auto empty_map = {};
                for (auto kv : empty_map) {
                    count += 1;
                }
                
                count
            )");
            
            check_eq(result.as<int>(), 0);
        });
        
        test("pair_member_access", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto data = {"x": 10, "y": 20};
                auto keys = "";
                auto sum = 0;
                
                for (auto kv : data) {
                    keys += kv.first + " ";
                    sum += kv.second;
                }
                
                keys + to_string(sum)
            )");
            
            check(result.as<std::string>().find("30") != std::string::npos);
        });

        // ===== STRINGS (docs/site/guide/09-containers-strings.html "Bytes and chars"): one char per element,
        // by value only - the same char s[i] reads =====
        test("string_value_iteration", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                string s = "abc";
                string joined = "";
                for (auto c : s) {
                    if (joined != "") { joined += ","; }
                    joined += c;
                }
                joined
            )");
            check_eq(std::string("a,b,c"), result.as<std::string>());
        });

        test("string_iteration_yields_chars", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                string s = "ab";
                string kinds = "";
                int code = 0;
                for (auto c : s) {
                    kinds += type_of(c);
                    code = code * 1000 + (c + 0);
                }
                kinds + ":" + to_string(code)
            )");
            check_eq(std::string("charchar:97098"), result.as<std::string>());
        });

        test("string_iteration_sources", [this]() {
            // a literal, a var-held string, a typed loop variable, const, a nested read and a
            // slotted function body all iterate the same way
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                string out = "";
                for (auto c : "xy") { out += c; }
                out += "|";
                var held = "pq";
                for (auto c : held) { out += c; }
                out += "|";
                for (char c : "mn") { out += c; }
                out += "|";
                for (const auto c : "rs") { out += c; }
                out += "|";
                var m = {"k": "hi"};
                for (auto c : m["k"]) { out += c; }
                out += "|";
                string dotted(string s) { string o = ""; for (auto c : s) { o += c; o += "."; } return o; }
                out += dotted("ab");
                out += "|";
                for (auto a : "ab") { for (auto b : "xy") { out += a; out += b; out += ","; } }
                out
            )");
            check_eq(std::string("xy|pq|mn|rs|hi|a.b.|ax,ay,bx,by,"), result.as<std::string>());
        });

        test("empty_string_iterates_zero_times", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                string s = "";
                int n = 0;
                for (auto c : s) { n++; }
                for (auto c : "") { n++; }
                n
            )");
            check_eq(result.as<int>(), 0);
        });

        test("break_and_continue_in_string_range_for", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                string s = "abcdef";
                string out = "";
                for (auto c : s) {
                    if (c == 'b') { continue; }
                    if (c == 'e') { break; }
                    out += c;
                }
                out + "|" + s
            )");
            check_eq(std::string("acd|abcdef"), result.as<std::string>());
        });

        test("string_iteration_is_by_value", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                string s = "abc";
                for (auto c : s) { c = 'z'; }
                s
            )");
            check_eq(std::string("abc"), result.as<std::string>());
        });

        test("string_reference_iteration_raises", [this]() {
            // a string char is a read, never a cell: the by-reference binding raises before
            // the first element, with the body untouched
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                string s = "abc";
                var held = "xy";
                string empty = "";
                string r1 = "";
                int visited = 0;
                try { for (auto& c : s) { visited++; } } catch (e) { r1 = e; }
                string r2 = "";
                try { for (const auto& c : held) { visited++; } } catch (e) { r2 = e; }
                string r3 = "";
                try { for (auto& c : empty) { visited++; } } catch (e) { r3 = e; }
                r1 + "#" + r2 + "#" + r3 + "#" + to_string(visited) + "#" + s
            )");
            const std::string text = "Cannot iterate a string by reference: its chars are not lvalues (use for (auto c : s))";
            check_eq(text + "#" + text + "#" + text + "#0#abc", result.as<std::string>());
        });

        test("non_iterable_value_names_its_type", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                class Foo { int a = 1; }
                var n = 5; var f = Foo(); var nothing = null; var x = 2.5;
                string r1 = ""; try { for (auto c : n) { } } catch (e) { r1 = e; }
                string r2 = ""; try { for (auto c : f) { } } catch (e) { r2 = e; }
                string r3 = ""; try { for (auto c : nothing) { } } catch (e) { r3 = e; }
                string r4 = ""; try { for (auto& c : x) { } } catch (e) { r4 = e; }
                r1 + "#" + r2 + "#" + r3 + "#" + r4
            )");
            check_eq(std::string("Cannot iterate int: not an array, map, string or coroutine#"
                                 "Cannot iterate Foo: not an array, map, string or coroutine#"
                                 "Cannot iterate null: not an array, map, string or coroutine#"
                                 "Cannot iterate float: not an array, map, string or coroutine"),
                     result.as<std::string>());
        });
    }
};

}

FOUNDRY_REGISTER(jai::foundry::tests::range_based_for_tests)