// The JavaScript and C# additions (docs/site/bridges): the array helpers, arrow functions, ?? and ??=, optional chains that skip
// their rest, print through to_string(), do-while, let, structured range-for bindings over maps, and the named texts for other
// languages' keywords. Every test runs on whichever backend the runner selected (--backend=vm for the vm).

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/detail/foreign_keyword.hpp>

#include <memory>
#include <sstream>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

namespace {

std::string run_output(const std::string& source) {
    auto e = make_engine();
    stdlib::register_all(*e);
    auto out = std::make_shared<std::ostringstream>();
    e->set_output_stream(out);
    e->execute(source);
    return out->str();
}

std::string error_text(const std::string& source) {
    auto e = make_engine();
    stdlib::register_all(*e);
    auto out = std::make_shared<std::ostringstream>();
    e->set_output_stream(out);
    try {
        e->execute(source);
    } catch (const std::exception& ex) {
        return ex.what();
    }
    return "(no error)";
}

bool contains(const std::string& haystack, std::string_view needle) { return haystack.find(needle) != std::string::npos; }

}   // namespace

class bridge_additions_tests : public suite {
public:
    bridge_additions_tests() : suite("Bridge Additions") {}

    void forge_tests() override {
        // ------------------------------------------------------------------ array helpers
        test("array_map_untyped_and_typed", [this]() {
            check_eq(std::string("[2,4,6] array\n[6,7]\ncaught: Cannot push 'string' to array<int>\n[2,3,\"s\"]\n"), run_output(R"JS(
                var a = [1, 2, 3];
                var d = array::map(a, (x) => x * 2);
                print(to_json(d), " ", type_of(d));
                array<int> b = [5, 6];
                auto t = array::map(b, [](int x) -> int { return x + 1; });
                print(to_json(t));
                try { t.push("s"); } catch (e) { print("caught: ", e); }
                var u = array::map([1, 2], x => x + 1);
                u.push("s");
                print(to_json(u));
            )JS"));
        });

        test("array_map_index_and_reference", [this]() {
            check_eq(std::string("[\"0:a\",\"1:b\"]\n[10,20] [1,2]\n"), run_output(R"JS(
                var names = ["a", "b"];
                print(to_json(array::map(names, (n, i) => `${i}:${n}`)));
                array<int> v = [1, 2];
                var tens = array::map(v, [](int& x) -> int { x = x; return x * 10; });
                print(to_json(tens), " ", to_json(v));
            )JS"));
        });

        test("array_reduce", [this]() {
            check_eq(std::string("10 10 abc 7\ncaught: Cannot reduce an empty array without an initial value\n"), run_output(R"JS(
                var a = [1, 2, 3, 4];
                var empty = [];
                print(a.reduce((acc, x) => acc + x, 0), " ", a.reduce((acc, x) => acc + x), " ",
                      ["a", "b", "c"].reduce((s, x) => s + x, ""), " ", empty.reduce((acc, x) => acc + x, 7));
                try { empty.reduce((acc, x) => acc + x); } catch (e) { print("caught: ", e); }
            )JS"));
        });

        test("array_for_each_and_reference_elements", [this]() {
            check_eq(std::string("0:5 1:6 \n[50,60]\n[11,21]\n"), run_output(R"JS(
                array<int> b = [5, 6];
                string line = "";
                b.for_each((x, i) => { line += `${i}:${x} `; });
                print(line);
                b.for_each([](int& x) { x *= 10; });
                print(to_json(b));
                class Unit { int hp = 10; }
                array<Unit> units = [Unit(), Unit()];
                units[1].hp = 20;
                units.for_each([](Unit& u) { u.hp += 1; });
                print(to_json(array::map(units, u => u.hp)));
            )JS"));
        });

        test("array_find_find_index_some_every", [this]() {
            check_eq(std::string("3 true 2 -1\ntrue false true false\ntrue 1\n"), run_output(R"JS(
                var a = [1, 2, 3, 4];
                var empty = [];
                print(a.find(x => x > 2), " ", a.find(x => x > 9) == null, " ", a.find_index(x => x > 2), " ", a.find_index(x => x > 9));
                print(a.some(x => x > 3), " ", empty.some(x => true), " ", empty.every(x => false), " ", a.every(x => x > 1));
                class P { int v = 1; }
                array<P> ps = [P()];
                var found = ps.find(p => p.v == 1);
                found.v = 99;
                print(found != null, " ", ps[0].v);
            )JS"));
        });

        test("array_helpers_stop_at_the_answer", [this]() {
            check_eq(std::string("2 2 3\n"), run_output(R"JS(
                int seen_some = 0;
                int seen_find = 0;
                int seen_every = 0;
                var a = [1, 5, 2, 7];
                a.some(x => { seen_some += 1; return x > 3; });
                a.find(x => { seen_find += 1; return x > 3; });
                a.every(x => { seen_every += 1; return x != 2; });
                print(seen_some, " ", seen_find, " ", seen_every);
            )JS"));
        });

        test("array_concat_and_insert", [this]() {
            check_eq(std::string("[1,2,3,4] [1,2]\ncaught: Cannot push 'string' to array<int>\n[0,1,9,2,3]\n[\"a\",\"x\",\"b\"]\n"
                                 "caught: Cannot insert at index 9 of an array of size 5\ncaught: Cannot insert 'string' into array<int>\n"), run_output(R"JS(
                array<int> a = [1, 2];
                auto both = array::concat(a, [3, 4]);
                print(to_json(both), " ", to_json(a));
                try { array::concat(a, ["s"]); } catch (e) { print("caught: ", e); }
                array<int> b = [1, 2, 3];
                b.insert(0, 0);
                b.insert(2, 9);
                print(to_json(b));
                var s = ["a", "b"];
                s.insert(1, "x");
                print(to_json(s));
                try { b.insert(9, 1); } catch (e) { print("caught: ", e); }
                try { b.insert(1, "s"); } catch (e) { print("caught: ", e); }
            )JS"));
        });

        test("array_helper_callback_throw_propagates", [this]() {
            check_eq(std::string("caught: boom at 2\n[1,2,3]\n"), run_output(R"JS(
                var a = [1, 2, 3];
                try { array::map(a, x => { if (x == 2) { throw "boom at " + x; } return x; }); } catch (e) { print("caught: ", e); }
                print(to_json(a));
            )JS"));
        });

        test("a_class_map_method_wins", [this]() {
            check_eq(std::string("mapped 3\n"), run_output(R"JS(
                class Grid { string map(int n) { return "mapped " + n; } }
                Grid g;
                print(g.map(3));
            )JS"));
        });

        test("array_helpers_checker", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            auto report = e->check("array<int> a = [1, 2]; a.mapp(x => x);");
            check_eq((size_t)1, report.error_count());
            check_true(!report.diagnostics.empty() && contains(report.diagnostics[0].message, "did you mean 'map'"));
            check_eq((size_t)0, e->check("array<int> a = [1, 2]; bool b = a.some(x => x > 1); int i = a.find_index(x => x > 1); auto m = array::map(a, x => x); auto j = m.size();").error_count());
            check_eq((size_t)1, e->check("array<int> a = [1, 2]; auto m = array::map(a, x => x); m.nosuch();").error_count());
        });

        // ------------------------------------------------------------------ arrow functions
        test("arrow_function_forms", [this]() {
            check_eq(std::string("4 6 42 7 9 15\n"), run_output(R"JS(
                var dbl = (x) => x * 2;
                var inc = x => x + 1;
                var answer = () => 42;
                var add = (int a, int b) => a + b;
                var block = (x) => { var y = x * 3; return y; };
                var adder = x => y => x + y;
                print(dbl(2), " ", dbl(3), " ", answer(), " ", add(3, 4), " ", block(3), " ", adder(10)(5));
            )JS"));
        });

        test("arrow_function_closes_over_its_scope_by_reference", [this]() {
            check_eq(std::string("2 7 3\n"), run_output(R"JS(
                function make_counter() {
                    int count = 0;
                    return () => { count += 1; return count; };
                }
                var next = make_counter();
                next();
                int total = 0;
                var bank = (int n) => total += n;
                bank(3);
                bank(4);
                int seen = 0;
                [1, 2, 3].for_each(x => { seen = x; });
                print(next(), " ", total, " ", seen);
            )JS"));
        });

        test("arrow_function_in_a_method_sees_this", [this]() {
            check_eq(std::string("[11,12] [21,22]\n"), run_output(R"JS(
                class Shift {
                    int by = 10;
                    var arrow(var xs) { return array::map(xs, x => x + this.by); }
                    var bare(var xs) { return array::map(xs, (int x) => x + by + 10); }
                }
                Shift s;
                print(to_json(s.arrow([1, 2])), " ", to_json(s.bare([1, 2])));
            )JS"));
        });

        test("arrow_function_typed_parameter_rejects", [this]() {
            const std::string err = error_text("var f = (int x) => x * 2; var s = \"text\"; f(s);");
            check_true(contains(err, "Cannot pass string to parameter 'x'"), err);
        });

        test("parentheses_still_group", [this]() {
            check_eq(std::string("14 5 3\n"), run_output(R"JS(
                int a = 3;
                int b = 4;
                var x = 5;
                print((a + b) * 2, " ", (x), " ", ((a)));
            )JS"));
        });

        // ------------------------------------------------------------------ ?? and ??=
        test("coalesce_values", [this]() {
            check_eq(std::string("5 0 false  x 3\n"), run_output(R"JS(
                var n = null;
                var z = 0;
                var f = false;
                var e = "";
                var c = null;
                print(n ?? 5, " ", z ?? 5, " ", f ?? true, " ", e ?? "x", " ", n ?? c ?? "x", " ", n ?? 1 + 2);
            )JS"));
        });

        test("coalesce_runs_its_right_side_only_for_null", [this]() {
            check_eq(std::string("1 7 1\n"), run_output(R"JS(
                int calls = 0;
                function fallback() { calls += 1; return 7; }
                var have = 1;
                var none = null;
                var a = have ?? fallback();
                var b = none ?? fallback();
                print(a, " ", b, " ", calls);
            )JS"));
        });

        test("coalesce_map_missing_key_and_get", [this]() {
            check_eq(std::string("0 3 0 1\n"), run_output(R"JS(
                var m = {"hp": 3};
                print(m["mana"] ?? 0, " ", m["hp"] ?? 0, " ", m.get("mana", 0), " ", m.size());
            )JS"));
        });

        test("coalesce_precedence_with_the_conditional", [this]() {
            check_eq(std::string("no yes\n"), run_output(R"JS(
                var none = null;
                var some = true;
                string a = none ?? false ? "yes" : "no";
                string b = some ?? false ? "yes" : "no";
                print(a, " ", b);
            )JS"));
        });

        test("coalesce_assign_targets", [this]() {
            check_eq(std::string("42 [9,2,3] axe 1 5\n"), run_output(R"JS(
                var z = null;
                z ??= 42;
                z ??= 7;
                var arr = [null, 2, 3];
                arr[0] ??= 9;
                arr[1] ??= 8;
                class Hero { var weapon = null; }
                Hero h;
                h.weapon ??= "axe";
                h.weapon ??= "bow";
                var m = {};
                m["k"] ??= 1;
                m["k"] ??= 2;
                int calls = 0;
                function five() { calls += 1; return 5; }
                var once = null;
                once ??= five();
                once ??= five();
                print(z, " ", to_json(arr), " ", h.weapon, " ", m["k"], " ", once * calls);
            )JS"));
        });

        test("coalesce_assign_needs_a_plain_target", [this]() {
            const std::string err = error_text("var a = [null]; int i = 0; a[i++] ??= 1;");
            check_true(contains(err, "'??=' needs a variable, a member or an element whose index runs no code"), err);
        });

        test("coalesce_in_a_coroutine_resumes", [this]() {
            check_eq(std::string("1 2 done\n"), run_output(R"JS(
                coroutine c() {
                    var none = null;
                    var b = none ?? (yield 1);
                    yield 2;
                }
                string line = "";
                for (auto v : c()) { line += to_string(v) + " "; }
                print(line, "done");
            )JS"));
        });

        test("coalesce_checker_types", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            check_eq((size_t)0, e->check("string s = null ?? \"x\"; int n = 1 ?? 2; var v = null; int k = v ?? 0;").error_count());
            check_eq((size_t)1, e->check("int n = 1 ?? 2; n = \"s\" ?? \"t\";").error_count());
        });

        // ------------------------------------------------------------------ optional chains
        test("optional_chain_skips_its_rest", [this]() {
            check_eq(std::string("null null null null null\n3 5 axe\n"), run_output(R"JS(
                class W { string name = "axe"; array<int> arr = [4, 5]; W next() { return W(); } }
                var b = null;
                print(b?.name.size(), " ", b?.arr[0], " ", b?.next()?.name, " ", b?.next().name, " ", b?.name);
                var w = W();
                print(w?.name.size(), " ", w?.arr[1], " ", w?.next()?.name);
            )JS"));
        });

        test("optional_chain_with_coalesce", [this]() {
            check_eq(std::string("fists fists axe -1\n"), run_output(R"JS(
                class Weapon { string name = "axe"; }
                class Player { var weapon = null; }
                var nobody = null;
                Player p;
                string a = nobody?.weapon?.name ?? "fists";
                string b = p?.weapon?.name ?? "fists";
                p.weapon = Weapon();
                string c = p?.weapon.name ?? "fists";
                print(a, " ", b, " ", c, " ", nobody?.weapon.name.size() ?? -1);
            )JS"));
        });

        test("optional_chain_raises_on_a_later_null", [this]() {
            check_eq(std::string("raised: Cannot access 'name' on null ('inner' is null)\n"), run_output(R"JS(
                class Outer { var inner = null; }
                var o = Outer();
                try { print(o?.inner.name); } catch (e) { print("raised: ", e); }
            )JS"));
        });

        test("optional_chain_evaluates_a_call_base_once", [this]() {
            check_eq(std::string("null 1\nno inner 2\n30 30\n"), run_output(R"JS(
                class Outer { var inner = null; Outer make() { return Outer(); } }
                int calls = 0;
                function find_outer(bool real) { calls += 1; return real ? Outer() : null; }
                print(find_outer(false)?.inner.name, " ", calls);
                print(find_outer(true)?.make().inner ?? "no inner", " ", calls);
                function local_chain(var p) {
                    int k = 2;
                    var arr = [10, 20, 30];
                    return find_outer(p != null)?.make().inner ?? arr[k];
                }
                print(local_chain(null), " ", local_chain(1));
            )JS"));
        });

        // ------------------------------------------------------------------ print
        test("print_uses_to_string", [this]() {
            check_eq(std::string("P(1)\nP(1) and 3\nat P(1)!\n[object]\n{x} 2\n"), run_output(R"JS(
                class P { int x = 1; string to_string() { return "P(" + x + ")"; } }
                class Plain { int y = 2; }
                class Braces { string to_string() { return "{x}"; } }
                P p;
                Plain q;
                Braces b;
                print(p);
                print("{} and {}", p, 3);
                print("at ", p, "!");
                print(q);
                print(b, " ", 2);
            )JS"));
        });

        test("print_to_string_throw_is_catchable", [this]() {
            check_eq(std::string("caught: bad text\n"), run_output(R"JS(
                class Bad { string to_string() { throw "bad text"; } }
                Bad b;
                try { print(b); } catch (e) { print("caught: ", e); }
            )JS"));
        });

        // ------------------------------------------------------------------ do-while
        test("do_while_runs_its_body_first", [this]() {
            check_eq(std::string("1 5 3\n"), run_output(R"JS(
                int runs = 0;
                do { runs++; } while (false);
                int c = 0;
                do { c++; } while (c < 5);
                function first_multiple(int n) {
                    int i = 0;
                    do { i++; } while (i % n != 0);
                    return i;
                }
                print(runs, " ", c, " ", first_multiple(3));
            )JS"));
        });

        test("do_while_continue_reaches_the_condition", [this]() {
            check_eq(std::string("5 4 3\n"), run_output(R"JS(
                int c = 0;
                int bodies = 0;
                do {
                    c++;
                    if (c == 2) { continue; }
                    bodies++;
                } while (c < 5);
                int i = 0;
                do {
                    i++;
                    if (i == 3) { break; }
                } while (true);
                print(c, " ", bodies, " ", i);
            )JS"));
        });

        test("do_while_nested_and_in_a_coroutine", [this]() {
            check_eq(std::string("6\ng0 g1 g2 \n"), run_output(R"JS(
                int total = 0;
                int a = 0;
                do {
                    int b = 0;
                    do { total++; b++; } while (b < 3);
                    a++;
                } while (a < 2);
                print(total);
                coroutine gen() {
                    int i = 0;
                    do { yield i; i++; } while (i < 3);
                }
                string line = "";
                for (auto v : gen()) { line += "g" + to_string(v) + " "; }
                print(line);
            )JS"));
        });

        test("do_while_condition_cannot_see_the_body", [this]() {
            const std::string err = error_text("do { int k = 1; } while (k < 3);");
            check_true(contains(err, "'k'"), err);
        });

        test("do_stays_a_name", [this]() {
            check_eq(std::string("5\n"), run_output("var do = 5; print(do);"));
        });

        // ------------------------------------------------------------------ let
        test("let_is_var", [this]() {
            check_eq(std::string("5 text 3 6 9\n"), run_output(R"JS(
                let x = 5;
                let y = 1;
                y = "text";
                let n = 0;
                for (let i = 0; i < 3; i++) { n++; }
                let sum = 0;
                for (let v : [1, 2, 3]) { sum += v; }
                let target = 1;
                let& alias = target;
                alias = 9;
                print(x, " ", y, " ", n, " ", sum, " ", target);
            )JS"));
        });

        // ------------------------------------------------------------------ structured bindings
        test("structured_binding_over_a_map", [this]() {
            check_eq(std::string("arrow=20 potion=3 \n{\"arrow\":20,\"potion\":3}\n{\"arrow\":21,\"potion\":4}\n{\"a\":10,\"b\":20}\n"), run_output(R"JS(
                var inv = {"potion": 3, "arrow": 20};
                string line = "";
                for (auto [k, v] : inv) { line += k + "=" + v + " "; v = 0; }
                print(line);
                print(to_json(inv));
                for (auto& [k, v] : inv) { v += 1; }
                print(to_json(inv));
                map<string, int> scores = {"a": 1, "b": 2};
                function bump(map<string, int>& m) { for (var& [name, score] : m) { score *= 10; } }
                bump(scores);
                print(to_json(scores));
            )JS"));
        });

        test("structured_binding_break_and_continue", [this]() {
            check_eq(std::string("b\n"), run_output(R"JS(
                map<string, int> m = {"a": 1, "b": 2, "c": 3};
                string picked = "";
                for (auto [k, v] : m) {
                    if (v == 1) { continue; }
                    picked += k;
                    break;
                }
                print(picked);
            )JS"));
        });

        test("structured_binding_checker", [this]() {
            auto e = make_engine();
            stdlib::register_all(*e);
            auto report = e->check("array<int> a = [1]; for (auto [x, y] : a) { }");
            check_eq((size_t)1, report.error_count());
            check_true(!report.diagnostics.empty() && contains(report.diagnostics[0].message, "a structured binding takes a map's entries, its key and value: cannot bind 'array<int>' to [x, y]"));
            check_eq((size_t)0, e->check("map<string, int> m = {\"a\": 1}; for (auto [k, v] : m) { for (auto [k2, v2] : m) { int s = v + v2; } }").diagnostics.size());
        });

        test("structured_binding_takes_two_names", [this]() {
            const std::string err = error_text("var m = {\"a\": 1}; for (auto [a, b, c] : m) { }");
            check_true(contains(err, "A map entry binds two names, the key and the value"), err);
        });

        // ------------------------------------------------------------------ foreign keywords
        test("foreign_keywords_name_the_jaiscript_spelling", [this]() {
            using detail::foreign_word;
            using detail::foreign_word_text;
            struct sample { const char* source; foreign_word word; };
            const sample samples[] = {
                {"class A { int size() const { return 1; } }", foreign_word::const_method},
                {"const int f() { return 1; }", foreign_word::const_return},
                {"array<const int> a = [];", foreign_word::const_type_argument},
                {"shared_ptr<const int> p;", foreign_word::pointer_to_const},
                {"var x = 1; var y = const_cast<int&>(x);", foreign_word::const_cast_call},
                {"constexpr int N = 4;", foreign_word::constexpr_declaration},
                {"class A { mutable int cache = 0; }", foreign_word::mutable_field},
                {"class A { virtual void f() { } }", foreign_word::virtual_method},
                {"var x = 1; delete x;", foreign_word::delete_statement},
                {"var a = new int[5];", foreign_word::new_array},
                {"class A { } var a = new A;", foreign_word::new_without_arguments},
                {"goto done;", foreign_word::goto_statement},
                {"var x = 1; var p = &x;", foreign_word::address_of},
                {"try { throw 1; } catch (Error e) { }", foreign_word::typed_catch},
                {"try { throw 1; } catch (int e) { }", foreign_word::typed_catch},
                {"class A { } class B extends A { }", foreign_word::extends_base},
                {"var a = 1; var b = a === 1;", foreign_word::strict_equal},
                {"var a = 1; var b = a !== 1;", foreign_word::strict_not_equal},
                {"function* gen() { yield 1; }", foreign_word::generator_function},
                {"var g = function*() { yield 1; };", foreign_word::generator_function},
            };
            for (const auto& s : samples) {
                const std::string err = error_text(s.source);
                check_true(contains(err, foreign_word_text(s.word)), std::string(s.source) + " -> " + err);
            }
        });

        test("foreign_words_stay_names_elsewhere", [this]() {
            check_eq(std::string("3 4 5\n"), run_output(R"JS(
                var virtual = 3;
                function delete(int n) { return n + 1; }
                var goto = 5;
                print(virtual, " ", delete(3), " ", goto);
            )JS"));
        });
    }
};

}   // namespace jai::foundry::tests

using bridge_additions_tests = jai::foundry::tests::bridge_additions_tests;
FOUNDRY_REGISTER(bridge_additions_tests)
