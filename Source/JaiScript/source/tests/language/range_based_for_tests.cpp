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

        // An iteration continues with the next greater key at each step, so a key the body inserts ahead of its position is
        // visited and one behind it is not (the guide's Maps section), whatever the binding's form
        test("map_insert_ahead_of_position_is_visited", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                var m1 = {1: 1, 5: 5};
                int c1 = 0;
                for (auto [k, v] : m1) { if (k == 1) { m1[3] = 3; m1[0] = 0; } c1 += 1; }
                var m2 = {1: 1, 5: 5};
                int c2 = 0;
                for (auto& kv : m2) { if (kv.first == 1) { m2[3] = 3; } c2 += 1; }
                int in_function() {
                    var m = {1: 1, 5: 5};
                    int c = 0;
                    for (auto kv : m) { if (kv.first == 1) { m[3] = 3; } c += 1; }
                    for (auto& [k, v] : m) { if (k == 1) { m[2] = 2; } c += 1; }
                    return c;
                }
                to_string(c1) + to_string(c2) + to_string(in_function())
            )");
            check_eq(std::string("337"), result.as<std::string>());
        });

        // A map's entries are the library's pair whatever the name `pair` means where the loop runs
        test("map_entries_ignore_a_script_pair", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                int pair(int a, int b) { return a * 100 + b; }
                var m = {"a": 1, "b": 2};
                string s = "";
                for (auto kv : m) { s += kv.first + to_string(kv.second); }
                for (auto& kv : m) { kv.second += 1; }
                string local_pair() {
                    var pair = 5;
                    var mm = {"z": 9};
                    string r = "";
                    for (auto kv : mm) { r += kv.first + to_string(kv.second + pair); }
                    return r;
                }
                s + " " + to_json(m) + " " + local_pair() + " " + to_string(pair(1, 2))
            )");
            check_eq(std::string("a1b2 {\"a\":2,\"b\":3} z14 102"), result.as<std::string>());
        });

        // A by-reference binding over a map<K, V> is a V&: a store converts to V or raises like m[k] = x and an
        // array's element do
        test("map_reference_binding_holds_the_value_type", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                map<string, int> tm = {"q": 1};
                var bad = "str";
                string r1 = "";
                try { for (auto& [k, v] : tm) { v = bad; } } catch (e) { r1 = e; }
                string r2 = "";
                try { for (auto& kv : tm) { kv.second = bad; } } catch (e) { r2 = e; }
                map<string, float> fm = {"a": 1.0};
                for (auto& [k, v] : fm) { v = 3; }
                string t1 = type_of(fm["a"]);
                for (auto& kv : fm) { kv.second = 4; }
                r1 + "#" + r2 + "#" + to_json(tm) + "#" + t1 + "#" + type_of(fm["a"])
            )");
            check_eq(std::string("Cannot assign 'string' to element of type 'int'#Cannot assign 'string' to element of type 'int'#{\"q\":1}#float#float"),
                     result.as<std::string>());
        });

        // A pair is a value: a copy holds copies of both halves, and the copy of a by-reference entry is a snapshot of it
        test("pair_copy_is_deep", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                var m = {1: [1, 2]};
                string s = "";
                for (auto kv : m) { var p2 = kv; p2.second.push(7); s += to_json(kv.second) + to_json(p2.second) + " "; }
                for (auto& kv : m) {
                    var p3 = kv;
                    p3.second.push(6);
                    s += to_json(m) + " ";
                    var p4 = kv;
                    m[1] = [0];
                    s += to_json(p4.second) + " ";
                }
                var p = pair([1], "s");
                var q = p;
                q.first.push(2);
                s + to_json(p.first) + to_json(q.first)
            )");
            check_eq(std::string("[1,2][1,2,7] {\"$map\":[[1,[1,2]]]} [1,2] [1][1,2]"), result.as<std::string>());
        });

        // A container the checker cannot see that is no map at run time: the names read the element's members as spelled, so a
        // coroutine yielding pairs binds their halves and an array element raises the member read's text; a yield inside the
        // loop resumes it
        test("structured_binding_over_a_non_map", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine pairs() { yield pair(1, 2); yield pair(3, 4); }
                string out = "";
                for (auto [k, v] : pairs()) { out += to_string(k) + to_string(v) + " "; }
                var arr = [[1, 2]];
                try { for (auto [k, v] : arr) { out += "unreached"; } } catch (e) { out += e + " "; }
                coroutine walk(var m) {
                    for (auto [k, v] : m) { yield k + "=" + to_string(v); }
                    for (auto& [k, v] : m) { v += 100; yield k; }
                    for (auto kv : m) { yield kv.first + to_string(kv.second); }
                }
                var m = {"a": 1, "b": 2};
                for (auto s : walk(m)) { out += s + " "; }
                out + to_json(m)
            )");
            check_eq(std::string("12 34 'first' is a method of array; call it with entry#.first() or wrap the call in a lambda a=1 b=2 a b a101 b102 {\"a\":1,\"b\":2}"),
                     result.as<std::string>());
        });

        // The bindings' C++ meanings in every spelling, at program scope and in a function: by value the names hold the
        // iteration's copies (a closure, a reference parameter and a write see the copy), by reference the value is the
        // entry; nested loops, break, continue, const and var spellings, a structural change, and value destructors (each
        // iteration's copy dies when its iteration ends)
        test("structured_binding_battery", [this]() {
            auto engine = make_engine();
            stdlib::register_all(engine);
            auto result = engine->execute(R"(
                string log = "";
                class Tracer {
                    string name = "";
                    Tracer(string n) { name = n; }
                    Tracer(const Tracer& o) { name = o.name + "'"; }
                    ~Tracer() { log += "~" + name + " "; }
                }
                string run() {
                    var m = {"a": [1, 2], "b": [3]};
                    string out = "";
                    for (auto [k, v] : m) { v.push(9); out += k + to_json(v) + " "; }
                    out += to_json(m) + " | ";
                    for (auto& [k, v] : m) { v.push(8); }
                    out += to_json(m) + " | ";
                    var fns = [];
                    for (auto [k, v] : m) { fns.push([&v]() { return v.size(); }); fns.push([v]() { return v.size(); }); fns.push([&k]() { return k; }); }
                    for (var f : fns) { out += to_string(f()) + ","; }
                    out += " | ";
                    void grow(var& x) { x.push(0); }
                    for (auto [k, v] : m) { grow(v); out += to_string(v.size()); }
                    for (auto& [k, v] : m) { grow(v); }
                    out += " " + to_json(m) + " | ";
                    map<int, int> im = {1: 10, 2: 20, 3: 30};
                    int s = 0;
                    for (const auto& [k, v] : im) { for (auto [k2, v2] : im) { if (k2 == 2) { continue; } if (k2 > k) { break; } s += v * k2; } }
                    out += to_string(s) + " | ";
                    for (var [k, v] : im) { v = 0; s += k; }
                    out += to_string(s) + to_json(im) + " | ";
                    var e = {1: 1, 2: 2, 3: 3, 4: 4};
                    for (auto [k, v] : e) { out += to_string(k); if (k == 1) { e.erase(2); e[5] = 5; } }
                    out += " | ";
                    var objs = {1: Tracer("x"), 2: Tracer("y")};
                    for (auto [k, v] : objs) { log += "[" + v.name + "] "; }
                    log += "| ";
                    for (auto& [k, v] : objs) { log += "[" + v.name + "] "; }
                    log += "| ";
                    for (auto kv : objs) { log += "[" + kv.second.name + "] "; }
                    log += "| ";
                    return out;
                }
                string r = run();
                var g = {"p": 1, "q": 2};
                string top = "";
                for (auto [k, v] : g) { top += k + to_string(v); v = 7; }
                for (auto& [k, v] : g) { v *= 3; }
                var tf = [];
                for (auto [k, v] : g) { tf.push([&v]() { return v; }); tf.push([&k]() { return k; }); }
                for (var f : tf) { top += to_string(f()); }
                string sk = "";
                var smap = {"long string key one": 1};
                for (auto [k, v] : smap) { k += "!"; sk = k; }
                r + "\n" + log + "\n" + top + " " + to_json(g) + "\n" + sk + " " + to_json(smap)
            )");
            check_eq(std::string("a[1,2,9] b[3,9] {\"a\":[1,2],\"b\":[3]} | {\"a\":[1,2,8],\"b\":[3,8]} | 3,3,a,2,2,b, | 43 {\"a\":[1,2,8,0],\"b\":[3,8,0]} | "
                                 "150 | 156{\"$map\":[[1,10],[2,20],[3,30]]} | 1345 | \n"
                                 "[x] ~x [y] ~y | [x] [y] | [x] ~x [y] ~y | ~x ~y \n"
                                 "p1q23p6q {\"p\":3,\"q\":6}\n"
                                 "long string key one! {\"long string key one\":1}"),
                     result.as<std::string>());
        });
    }
};

}

FOUNDRY_REGISTER(jai::foundry::tests::range_based_for_tests)