// jaibite save/load: differential round-trip battery. For each script, a freshly
// parsed bite and a load(save(bite)) bite (into a DIFFERENT engine) must produce
// identical results and identical error text, on both backends.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/testing/temporary_directory.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <filesystem>
#include <fstream>
#include <functional>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

namespace jaibite_test_detail {

    class jb_point {
    public:
        double x = 0;
        double y = 0;
        jb_point() = default;
        jb_point(double ax, double ay) : x(ax), y(ay) {}
        double sum() const { return x + y; }
    };

    class jb_tag {
    public:
        std::string label;
        jb_tag() = default;
        explicit jb_tag(const std::string& l) : label(l) {}
        std::string decorated() const { return "[" + label + "]"; }
    };

    inline void register_point(engine& eng) {
        dynamic_binder<jb_point>(eng, "JbPoint")
            .constructor<>()
            .constructor<double, double>()
            .method("sum", &jb_point::sum)
            .property("x", &jb_point::x)
            .property("y", &jb_point::y)
            .build();
    }

    inline void register_tag(engine& eng) {
        dynamic_binder<jb_tag>(eng, "JbTag")
            .constructor<>()
            .constructor<std::string>()
            .method("decorated", &jb_tag::decorated)
            .property("label", &jb_tag::label)
            .build();
    }

    using engine_setup = std::function<void(engine&)>;

    inline std::shared_ptr<engine> fresh_engine(bool use_vm, const engine_setup& setup) {
        auto e = engine::make();
        jai::stdlib::register_all(e);
        if (setup) setup(*e);
        if (use_vm) e->set_backend(backend_type::vm);
        return e;
    }

    struct run_output {
        bool threw = false;
        std::string error;
        std::string value;

        bool operator==(const run_output& other) const {
            return threw == other.threw && error == other.error && value == other.value;
        }
    };

    inline run_output run_once(jai::jaibite& bite) {
        run_output out;
        try {
            out.value = bite.execute().to_string();
        } catch (const std::exception& ex) {
            out.threw = true;
            out.error = ex.what();
        }
        return out;
    }

} // namespace jaibite_test_detail

class jaibite_serialization_tests : public suite {
public:
    jaibite_serialization_tests() : suite("Jaibite Serialization") {}

    void forge_tests() override {
        using namespace jaibite_test_detail;

        // Fresh bite vs saved->loaded-into-another-engine bite, two executions each
        // (second loaded run exercises the lazily compiled cached vm chunk), both backends.
        auto check_roundtrip = [this](const std::string& src, const engine_setup& setup = {}) {
            for (bool use_vm : { false, true }) {
                auto direct_engine = fresh_engine(use_vm, setup);
                auto direct_bite = direct_engine->jaibite(src);
                run_output direct_first = run_once(direct_bite);
                run_output direct_second = run_once(direct_bite);

                auto save_engine = fresh_engine(use_vm, setup);
                auto bytes = save_engine->jaibite(src).save_bytes();

                auto load_engine = fresh_engine(use_vm, setup);
                auto loaded_bite = load_engine->jaibite_load_bytes(bytes);
                check_true(loaded_bite.valid());
                run_output loaded_first = run_once(loaded_bite);
                run_output loaded_second = run_once(loaded_bite);

                check_eq(direct_first.threw, loaded_first.threw);
                check_eq(direct_first.error, loaded_first.error);
                check_eq(direct_first.value, loaded_first.value);
                check_eq(direct_second.threw, loaded_second.threw);
                check_eq(direct_second.error, loaded_second.error);
                check_eq(direct_second.value, loaded_second.value);
            }
        };

        test("roundtrip_arithmetic_strings", [check_roundtrip]() {
            check_roundtrip(R"(
                int a = 7; int b = 3;
                float f = 2.5 * 4.0 + a;
                var s = "r:" + to_string(a * b + a % b) + "|" + to_string(a / b);
                s + "|" + to_string(f) + "|" + to_string(a > b) + "|" + to_string(-a) + "|" + to_string('Q');
            )");
        });

        test("roundtrip_functions_defaults_ref_params", [check_roundtrip]() {
            check_roundtrip(R"(
                int bump(int& x, int by = 5) { x += by; return x; }
                class Holder { int v = 1; Holder() {} }
                auto h = Holder();
                int r1 = bump(h.v);
                auto arr = [10, 20, 30];
                int r2 = bump(arr[1], 2);
                int loc = 100;
                int r3 = bump(loc);
                to_string(r1) + "," + to_string(r2) + "," + to_string(arr[1]) + "," + to_string(h.v) + "," + to_string(r3) + "," + to_string(loc);
            )");
        });

        test("roundtrip_classes_inheritance_statics", [check_roundtrip]() {
            check_roundtrip(R"(
                class Animal {
                    int legs = 4;
                    static int count = 0;
                    Animal() { count += 1; }
                    Animal(int l) { legs = l; count += 1; }
                    int describe() { return legs; }
                }
                class Bird : Animal {
                    Bird() : super(2) {}
                    int describe() { return legs * 10; }
                }
                auto a = Animal();
                auto b = Bird();
                to_string(a.describe()) + "|" + to_string(b.describe()) + "|" + to_string(Animal::count);
            )");
        });

        test("roundtrip_lambdas_captures", [check_roundtrip]() {
            check_roundtrip(R"(
                int base = 10;
                auto add = [=](int x) { return x + base; };
                int counter = 0;
                auto inc = [&]() { counter += 1; return counter; };
                inc(); inc();
                auto make_adder = [](int n) { return [=](int m) { return n + m; }; };
                to_string(add(5)) + "|" + to_string(counter) + "|" + to_string(make_adder(3)(4));
            )");
        });

        // Method coroutines, a field-held handle, yields in expression positions and the
        // completion-value conversion must survive save/load exactly like a fresh parse.
        test("roundtrip_coroutine_methods_and_expression_yields", [check_roundtrip]() {
            check_roundtrip(R"(
                class Gen {
                    int base = 10;
                    var h = null;
                    coroutine int steps(int n) {
                        for (int i = 0; i < n; ++i) { yield base + i; }
                        return -1;
                    }
                    coroutine function mixed() -> auto {
                        var t = ((yield 1) == null ? 10 : 0) + 1;
                        yield t;
                        return 2.5;
                    }
                }
                var g = Gen();
                g.h = g.steps(3);
                int total = 0;
                for (auto v : g.steps(2)) { total += v; }
                string s = to_string(g.h.resume()) + "," + to_string(g.h.resume());
                auto m = g.mixed();
                s += "|" + to_string(m.resume()) + "," + to_string(m.resume()) + "," + to_string(m.resume()) + "," + to_string(m.done());
                coroutine int typed() { yield 1; return 2.7; }
                auto t = typed();
                t.resume();
                var last = t.resume();
                string e = "";
                try { t.resume(); } catch (err) { e = to_string(err); }
                to_string(total) + "|" + s + "|" + type_of(last) + ":" + to_string(last) + "|" + e;
            )");
        });

        test("roundtrip_coroutines_range_for", [check_roundtrip]() {
            check_roundtrip(R"(
                coroutine int gen(int n) {
                    for (int i = 0; i < n; ++i) { yield i * i; }
                    return -1;
                }
                coroutine sq() -> auto { yield 4; yield 9; return -1; }
                int total = 0;
                for (auto v : gen(5)) { total += v; }
                for (auto v : sq()) { total += v; }
                auto c = gen(3);
                int manual = 0;
                while (!c.done()) { manual += c.resume(); }
                to_string(total) + "|" + to_string(manual);
            )");
        });

        test("roundtrip_maps_arrays", [check_roundtrip]() {
            check_roundtrip(R"(
                var m = {"a": 1, "b": 2};
                m["c"] = m["a"] + m["b"];
                var arr = [1, 2, 3];
                arr.push(m["c"]);
                var s = "";
                for (auto kv : m) { s += kv.first + "=" + to_string(kv.second) + ";"; }
                for (auto& x : arr) { x *= 2; }
                s + to_string(arr.size()) + "|" + to_string(arr[3]);
            )");
        });

        test("roundtrip_try_catch_error_text", [check_roundtrip]() {
            check_roundtrip(R"(
                var msg = "";
                try { throw "boom " + to_string(42); } catch (e) { msg = e; }
                var msg2 = "";
                try { var arr = [1]; var x = arr[10]; } catch (e2) { msg2 = e2; }
                msg + "|" + msg2;
            )");
        });

        // The catch variable is a frame-slot local (function) / scope local (top level):
        // the slot and symbol ride the bite, and the loaded tree reads it exactly like a
        // fresh parse - shadowing, callee isolation, yields inside the catch included
        test("roundtrip_catch_variable_slot", [check_roundtrip]() {
            check_roundtrip(R"(
                var e = "global";
                int callee() { int e = 5; return e; }
                string f() { string s = ""; try { throw "x"; } catch (e) { e += "!"; s = e + to_string(callee()); } return s + e; }
                string log = "";
                try { throw 2; } catch (e) { log = to_string(e) + "/"; }
                coroutine var g() { try { throw "c"; } catch (e) { yield e; yield e + "2"; return e; } }
                var h = g();
                log + f() + "|" + to_string(h.resume()) + "|" + to_string(h.resume()) + "|" + to_string(h.resume()) + "|" + e;
            )");
        });

        test("roundtrip_parallel_for_zip", [check_roundtrip]() {
            check_roundtrip(R"(
                class V { float x = 0.0; float y = 0.0; V() {} V(float a) { x = a; y = a * 2; } }
                array<V> vs = []; array<V> os = []; array<float> ss = [];
                for (int i = 0; i < 50; i++) { vs.push(V(i)); os.push(V()); ss.push(0.0); }
                parallel_for (auto& v, auto& o, auto& s : vs, os, ss) { o.x = v.x * v.y; o.y = v.y + 1.0; s = o.x + o.y; }
                to_string(os[7].x) + "|" + to_string(os[7].y) + "|" + to_string(ss[49]) + "|" + to_string(os.size());
            )");
        });

        test("roundtrip_uncaught_throw_error_text", [check_roundtrip]() {
            check_roundtrip(R"(
                int helper(int n) { if (n > 2) { throw "kaboom at " + to_string(n); } return n; }
                helper(1) + helper(5);
            )");
        });

        test("roundtrip_registered_type", [check_roundtrip]() {
            check_roundtrip(R"(
                auto p = JbPoint(3.0, 4.0);
                p.x = p.x + 10.0;
                auto t = JbTag("hi");
                to_string(p.sum()) + "|" + t.decorated() + "|" + to_string(p.x);
            )", [](engine& e) { register_point(e); register_tag(e); });
        });

        test("roundtrip_kitchen_sink", [check_roundtrip]() {
            check_roundtrip(R"(
                enum Color { Red, Green, Blue }
                namespace util { int twice(int v) { return v * 2; } }
                auto [dx, dy] = [4, 7];
                int pick = 2;
                var name = "";
                switch (pick) {
                    case 1: name = "one";
                    case 2: name = "two"; fallthrough;
                    case 3: name += "+three";
                    default: name += "!";
                }
                int n = 0;
                while (true) { n++; if (n >= 3) { break; } }
                var t = n == 3 ? "yes" : "no";
                var maybe = null;
                var q = maybe?.anything;
                name + "|" + t + "|" + to_string(dx + dy) + "|" + to_string(util::twice(21)) + "|" + to_string(q == null) + "|" + to_string(Color::Blue);
            )");
        });

        test("roundtrip_cast_expressions", [this, check_roundtrip]() {
            check_roundtrip(R"(
                class S { int x = 1; }
                class T : S { int t = 2; }
                class H { S p = null; }
                auto h = H();
                h.p = T();
                cast<T&>(h.p).t = 5;
                T tv = cast<T>(h.p);
                tv.t = 9;
                shared_ptr<T> th = cast<shared_ptr<T>>(h.p);
                th.t += 1;
                T tmp = T();
                auto& r = cast<T&>(tmp);
                r.t = 3;
                var f = 2.7;
                var c = "";
                try { var bad = cast<H>(h.p); } catch (e) { c = e; }
                to_string(cast<T&>(h.p).t) + "|" + to_string(tv.t) + "|" + to_string(tmp.t) + "|" + to_string(cast<int>(f)) + "|" + cast<string>(42) + "|" + c;
            )");
        });

        test("roundtrip_extern_and_check_mode_attributes", [this, check_roundtrip]() {
            check_roundtrip(R"([[warn_mode]];
                extern function host_fn;
                extern int host_int;
                extern loose;
                class Game { int v = 3; }
                extern shared_ptr<Game> G;
                inline int shared_count = 2;
                inline int shared_count = 9;
                namespace cfg { inline int depth = 3; }
                namespace cfg { inline int depth = 4; }
                var shared_text = to_string(shared_count) + to_string(cfg::depth);
                function use_all() -> int { return host_int + G.v; }
                var caught = "";
                try { use_all(); } catch (e) { caught = e; }
                var c2 = "";
                try { host_fn(1); } catch (e) { c2 = e; }
                int y = 4;
                { [[strict_mode]] int z = y + 1; y = z; }
                if (y > 0) [[warn_mode]] y = y + 1;
                [[strict_mode]] y = y * 2;
                function bump() { [[warn_mode]] y = y + 1; }
                bump();
                caught + "|" + c2 + "|" + to_string(y) + "|" + shared_text;
            )");
            // statement::check_mode_attr arrived with format version 11, the try_stmt catch
            // variable symbol + slot with 12, the own-file location marker with 14 (right after
            // the 4-byte magic)
            using namespace jaibite_test_detail;
            auto bytes = fresh_engine(false, {})->jaibite("[[warn_mode]];\nextern int q;\nvar v = 1;").save_bytes();
            check_eq((int)30, (int)bytes[4], "jaibite format version");   // 30: C++ lvalue targets read their pieces as references (cast_expr::implicit_ref, assignment_expr::yields_reference); 29: a shared_ptr<T> namespace variable store is checked (R-q6); 28: variable_decl::is_inline; 27: math::vec2/vec3/vec4/ivec2/ivec3/color read as types; 26: import(...) is an expression (import_expr); 24: array<uint8> / array<int8> element types; 23: a lambda capture carries its init-capture expression; 22: variable_decl::default_initialized (a same-unit re-run keeps a declaration without an initializer); 21: typed declarations without an initializer default-initialize; 20: template strings join no empty parts; 19: utf8_string reads as a type; 18: a literal include inside a function splices at parse time; 17: a negated numeric literal is a literal; 16: parallel_for frame captures
            {   // import(...) round-trips: literal paths, a group, computed arguments and jai::force
                const auto dir = jai::foundry::test_temp_directory();
                { std::ofstream(dir / "jb_imp_a.jai") << "int JB_A = 3;\n"; }
                { std::ofstream(dir / "jb_imp_b.jai") << "int JB_B = 4;\n"; }
                const std::string src = "array<string> MODS = [\"jb_imp_b.jai\"];\n"
                                        "bool one = import(\"jb_imp_a.jai\");\nbool again = import(\"jb_imp_a.jai\", \"jb_imp_b.jai\", jai::force);\nbool more = import(MODS);\n"
                                        "to_string(one) + \"|\" + to_string(again) + \"|\" + to_string(more) + \"|\" + to_string(JB_A + JB_B);";
                for (bool use_vm : {false, true}) {
                    const auto setup = [&](engine& e) { e.add_include_path(dir.string()); e.import_recheck_interval(0); };
                    auto saved = fresh_engine(use_vm, setup)->jaibite(src).save_bytes();
                    auto load_engine = fresh_engine(use_vm, setup);
                    auto loaded = load_engine->jaibite_load_bytes(saved);
                    check_true(loaded.valid());
                    check_eq(std::string("true|true|false|7"), run_once(loaded).value, "a loaded bite's imports run as parsed");
                }
                std::filesystem::remove(dir / "jb_imp_a.jai");
                std::filesystem::remove(dir / "jb_imp_b.jai");
            }
            // a loaded bite carries the attributes: a warn region's errors stay warnings on the
            // loading engine's own check, and the runtime raises
            for (const char* src : {"[[warn_mode]];\nint x = \"s\";\nx",
                                    "int a = 1;\n[[warn_mode]] int x = \"s\";\nx",
                                    "int a = 1;\n{ [[warn_mode]] a = 2; int x = \"s\"; }\na"}) {
                auto save_engine = fresh_engine(false, {});
                auto saved = save_engine->jaibite(src).save_bytes();
                auto load_engine = fresh_engine(false, {});
                auto loaded = load_engine->jaibite_load_bytes(saved);
                check_true(loaded.valid());
                check_false(load_engine->last_check_diagnostics().has_errors());
                run_output out = run_once(loaded);
                check_true(out.threw);
                check_true(out.error.find("Cannot initialize") == 0, "got: " + out.error);
            }
            // a strict region survives the trip too: it gates a warn-mode loading engine
            auto off_saver = fresh_engine(false, {});
            off_saver->static_checking(check_mode::off);
            auto strict_bytes = off_saver->jaibite("int a = 1;\n[[strict_mode]] int x = \"s\";\nx").save_bytes();
            auto warn_loader = fresh_engine(false, {});
            warn_loader->static_checking(check_mode::warn);
            check_throws<static_check_error>([&]() { warn_loader->jaibite_load_bytes(strict_bytes); });
            auto plain_bytes = off_saver->jaibite("int a = 1;\nint x = \"s\";\nx").save_bytes();
            check_true(warn_loader->jaibite_load_bytes(plain_bytes).valid());
        });

        // Symbol IDs are per-engine and order-dependent: pre-warming the loader's interner
        // and registering classes in a different order guarantees IDs differ, proving the
        // string-table relocation actually rewrites them.
        test("cross_engine_symbol_relocation", [this]() {
            using namespace jaibite_test_detail;
            const std::string src = R"(
                auto p = JbPoint(1.0, 2.0);
                auto t = JbTag("x");
                int stray_name_one = 5;
                to_string(p.sum()) + t.decorated() + to_string(stray_name_one);
            )";
            for (bool use_vm : { false, true }) {
                auto engine_a = fresh_engine(use_vm, [](engine& e) { register_point(e); register_tag(e); });
                std::string expected = engine_a->execute(src).to_string();
                auto bytes = engine_a->jaibite(src).save_bytes();

                auto engine_b = fresh_engine(use_vm, [](engine& e) {
                    register_tag(e);   // reversed registration order vs engine_a
                    register_point(e);
                });
                engine_b->execute("var warm_a = 1; var warm_b = 2; int shift_the_interner = 3;");
                auto loaded = engine_b->jaibite_load_bytes(bytes);
                auto result = loaded.execute();
                check_eq(expected, result.to_string());
            }
        });

        // HAZARD: the interpreter patches lambda/coroutine AST in place on first execution
        // (identifier slot_index -> SIZE_MAX + outer_slot_plan). A bite saved AFTER running
        // must still load and behave exactly like a fresh parse, on both backends.
        test("save_after_execute_matches_fresh", [this]() {
            using namespace jaibite_test_detail;
            const std::string src = R"(
                int seed = 3;
                auto scaled = [=](int v) { return v * seed; };
                coroutine int trickle(int n) {
                    for (int i = 0; i < n; ++i) { yield i + seed; }
                }
                int total = 0;
                for (auto v : trickle(3)) { total += v; }
                to_string(scaled(7)) + "|" + to_string(total);
            )";
            for (bool save_vm : { false, true }) {
                auto save_engine = fresh_engine(save_vm, {});
                auto bite = save_engine->jaibite(src);
                run_output pre_save_run = run_once(bite);
                check_false(pre_save_run.threw);
                auto bytes = bite.save_bytes();   // AST now carries runtime patches
                // Loading is not a rewriting pass. Use the same registration surface
                // so the saved header's host fingerprint is also expected to match.
                auto unchanged = save_engine->jaibite_load_bytes(bytes);
                check_true(bytes == unchanged.save_bytes(), "post-execution closure bytes stay identical");

                for (bool load_vm : { false, true }) {
                    auto fresh = fresh_engine(load_vm, {});
                    auto fresh_bite = fresh->jaibite(src);
                    run_output expected = run_once(fresh_bite);

                    auto load_engine = fresh_engine(load_vm, {});
                    auto loaded = load_engine->jaibite_load_bytes(bytes);
                    run_output actual = run_once(loaded);
                    check_eq(expected.threw, actual.threw);
                    check_eq(expected.error, actual.error);
                    check_eq(expected.value, actual.value);
                }
            }
        });

        test("class_and_lambda_bytes_stable_before_and_after_execute", [this]() {
            using namespace jaibite_test_detail;
            const char* src = R"(
                class StableClass {
                    int value = 7;
                    StableClass() {}
                    int get() { return value; }
                }
                auto c = StableClass();
                auto read = [=](int by) { return c.get() + by; };
                read(5);
            )";
            for (bool use_vm : { false, true }) {
                auto owner = fresh_engine(use_vm, {});
                auto bite = owner->jaibite(src);
                const auto before = bite.save_bytes();
                auto before_loaded = owner->jaibite_load_bytes(before);
                check_true(before == before_loaded.save_bytes(), "class/lambda bytes before execution");
                check_eq(int64_t(12), bite.execute().as_int());
                const auto after = bite.save_bytes();
                auto after_loaded = owner->jaibite_load_bytes(after);
                check_true(after == after_loaded.save_bytes(), "class/lambda bytes after execution");
            }
        });

        test("nested_lambda_chain_captures_survive_roundtrip", [this]() {
            using namespace jaibite_test_detail;
            const char* src = R"(
                var stored = null;
                function stash(var cb) -> void { stored = cb; }
                function with_probe(var cb) -> void { cb(7); }
                var run = [=](int outer) -> void {
                    with_probe([outer](var probe) -> void {
                        stash([outer, probe]() -> int { return probe * 10 + outer; });
                    });
                };
                function f(int k) -> int { var g = [=]() -> int { var h = [=]() -> int { return k; }; return h(); }; return g(); }
                run(3);
                stored() * 100 + f(9)
            )";
            auto author = fresh_engine(false, {});
            auto bytes = author->jaibite(src).save_bytes();
            for (bool load_vm : { false, true }) {
                auto host = fresh_engine(load_vm, {});
                auto loaded = host->jaibite_load_bytes(bytes);
                check_eq(int64_t(7309), loaded.execute().as_int());
            }
        });

        test("file_save_load_roundtrip", [this]() {
            using namespace jaibite_test_detail;
            auto path = (jai::foundry::test_temp_directory() / "jaibite_roundtrip_test.jbite").string();
            auto engine_a = fresh_engine(false, {});
            engine_a->jaibite("int x = 6; x * 7;").save(path);
            auto engine_b = fresh_engine(true, {});
            auto loaded = engine_b->jaibite_load(path);
            check_eq(int64_t(42), loaded.execute().as_int());
            std::filesystem::remove(path);
        });

        test("load_missing_file_throws", [this]() {
            using namespace jaibite_test_detail;
            auto e = fresh_engine(false, {});
            check_throws([&]() { e->jaibite_load("definitely_not_a_real_file.jbite"); });
        });

        test("load_rejects_bad_magic", [this]() {
            using namespace jaibite_test_detail;
            auto e = fresh_engine(false, {});
            auto bytes = e->jaibite("1 + 1;").save_bytes();
            bytes[0] = 'X';
            bool threw = false;
            try { e->jaibite_load_bytes(bytes); }
            catch (const std::exception& ex) {
                threw = true;
                check_true(std::string(ex.what()).find("magic") != std::string::npos);
            }
            check_true(threw);
        });

        test("load_rejects_version_mismatch", [this]() {
            using namespace jaibite_test_detail;
            auto e = fresh_engine(false, {});
            auto bytes = e->jaibite("1 + 1;").save_bytes();
            bytes[4] = 99;   // format version lives right after the magic
            bool threw = false;
            try { e->jaibite_load_bytes(bytes); }
            catch (const std::exception& ex) {
                threw = true;
                check_true(std::string(ex.what()).find("version") != std::string::npos);
            }
            check_true(threw);
            // the previous format (22: no init-capture expressions) is refused too, so a sibling written before it re-parses
            bytes[4] = 22;
            bool older_threw = false;
            try { e->jaibite_load_bytes(bytes); } catch (const std::exception&) { older_threw = true; }
            check_true(older_threw, "a version-22 bite is refused");
        });

        // Every strict prefix must fail with a clean error — never UB, crash, or success.
        test("load_rejects_truncation_everywhere", [this]() {
            using namespace jaibite_test_detail;
            auto e = fresh_engine(false, {});
            auto bytes = e->jaibite(R"(
                class C { int v = 1; C() {} int get() { return v; } }
                auto c = C();
                var m = {"k": [1, 2, 3]};
                c.get() + m["k"][2];
            )").save_bytes();
            for (size_t len = 0; len < bytes.size(); len += (len < 32 ? 1 : 7)) {
                std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + len);
                check_throws([&]() { e->jaibite_load_bytes(cut); });
            }
        });

        test("load_rejects_trailing_bytes", [this]() {
            using namespace jaibite_test_detail;
            auto e = fresh_engine(false, {});
            auto bytes = e->jaibite("1 + 1;").save_bytes();
            bytes.push_back(0);
            check_throws([&]() { e->jaibite_load_bytes(bytes); });
        });

        // Registration fingerprint: advisory warning only — a mismatched engine still loads.
        test("registration_fingerprint_warning", [this]() {
            using namespace jaibite_test_detail;
            auto engine_a = fresh_engine(false, [](engine& e) { register_point(e); });
            auto bytes = engine_a->jaibite("1 + 2;").save_bytes();

            auto matching = fresh_engine(false, [](engine& e) { register_point(e); });
            auto loaded_match = matching->jaibite_load_bytes(bytes);
            check_false(loaded_match.registration_mismatch());

            auto missing = fresh_engine(false, {});
            auto loaded_mismatch = missing->jaibite_load_bytes(bytes);
            check_true(loaded_mismatch.registration_mismatch());
            check_eq(int64_t(3), loaded_mismatch.execute().as_int());
        });

        // FIXED by inclusion (2026-07, open question #10): zero-arg and variadic host
        // functions register as plain globals (never enter overloadedFunctions) and were
        // invisible to registration_fingerprint(), so a .jaibite saved against them loaded
        // with registration_mismatch()==false and died at execute. They now fold into
        // the fingerprint with an arity-class marker.
        test("registration_fingerprint_sees_zero_arg_and_variadic", [this]() {
            using namespace jaibite_test_detail;
            auto a = fresh_engine(false, {});
            auto b = fresh_engine(false, {});
            check_eq(a->registration_fingerprint(), b->registration_fingerprint());

            a->add_function("zero_arg_probe", []() -> jai::script_int { return 1; });
            check_true(a->registration_fingerprint() != b->registration_fingerprint());

            b->add_function("zero_arg_probe", []() -> jai::script_int { return 1; });
            check_eq(a->registration_fingerprint(), b->registration_fingerprint());

            a->add_variadic_function("variadic_probe",
                [](const std::vector<jai::script_value>& args) -> jai::script_value {
                    return jai::script_value((jai::script_int)args.size(), nullptr);
                });
            check_true(a->registration_fingerprint() != b->registration_fingerprint());

            // The failure scenario: save with the zero-arg fn, load into an engine
            // without it -> the advisory flag fires. A mismatched bite is re-checked at
            // load against THIS engine's surface: under warn the load never hard-fails
            // and the missing function is a diagnostic; under strict (the default) the
            // load throws before anything runs.
            auto bytes = a->jaibite("zero_arg_probe();").save_bytes();
            auto bare = fresh_engine(false, {});
            bare->static_checking(check_mode::warn);
            auto loaded = bare->jaibite_load_bytes(bytes);
            check_true(loaded.registration_mismatch());
            check_true(loaded.check_diagnostics().has_errors());
            auto strict_bare = fresh_engine(false, {});
            std::string text;
            try { strict_bare->jaibite_load_bytes(bytes); }
            catch (const static_check_error& ex) { text = ex.what(); }
            check_true(text.find("use of undeclared identifier 'zero_arg_probe'") != std::string::npos,
                "strict load re-checks against this engine's surface (got: " + text + ")");
        });
    }
};

} // namespace jai::foundry::tests

using jaibite_serialization_tests = jai::foundry::tests::jaibite_serialization_tests;
FOUNDRY_REGISTER(jaibite_serialization_tests)
