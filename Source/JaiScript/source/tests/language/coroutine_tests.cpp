#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class coroutine_tests : public suite {
public:
    coroutine_tests() : suite("Coroutine Tests") {}

    void forge_tests() override {

        test("basic_yield_with_value", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int counter() {
                    yield 1;
                    yield 2;
                    yield 3;
                    return 4;
                }
                auto c = counter();
                auto a = c.resume();
                auto b = c.resume();
                auto d = c.resume();
                auto e = c.resume();
                a + b + d + e
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(10));
        });

        // Regression: the interpreter's continuation-replay path skipped the typed-return
        // conversion on the coroutine's FINAL return (2.5 leaked out as float; vm converts)
        test("coroutine_final_typed_return_converts", [this]() {
            auto engine = make_engine();
            engine->execute(R"(
                coroutine int g() { yield 1; return 2.5; }
                var c = g();
                var first = c.resume();
            )");
            auto final_val = engine->execute("c.resume();");
            check_true(final_val.is_int());
            check_eq(static_cast<script_int>(2), final_val.as<script_int>());
        });

        // Regression: end-of-execute reset_environment_pool() reset(nullptr)'d the suspended
        // coroutine's saved env chain, so resuming in a later execute (with global redeclares
        // in between) threw "Undefined variable 'g'"; vm fibers resumed fine.
        test("coroutine_resume_across_executes_with_redeclare", [this]() {
            auto engine = make_engine();
            engine->execute("var g = 1; coroutine int co() { yield g; yield g; yield g; }");
            engine->execute("var c = co();");
            check_eq(static_cast<script_int>(1), engine->execute("c.resume();").as<script_int>());
            engine->execute("var g = 2; var advnew_a = 10;");   // redeclare + fresh define
            check_eq(static_cast<script_int>(2), engine->execute("c.resume();").as<script_int>());
            engine->execute("g = 3;");
            check_eq(static_cast<script_int>(3), engine->execute("c.resume();").as<script_int>());
        });

        test("coroutine_done_check", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int gen() {
                    yield 42;
                    return 0;
                }
                auto c = gen();
                auto before = c.done();
                c.resume();
                auto mid = c.done();
                c.resume();
                auto after = c.done();
                auto result = 0;
                if (!before && !mid && after) {
                    result = 1;
                }
                result
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(1));
        });

        test("yield_inside_for_loop", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int range_gen() {
                    for (int i = 0; i < 5; ++i) {
                        yield i;
                    }
                    return -1;
                }
                auto c = range_gen();
                auto sum = 0;
                while (!c.done()) {
                    sum = sum + c.resume();
                }
                sum
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(9));
        });

        test("yield_inside_while_loop", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int while_gen() {
                    auto i = 0;
                    while (i < 3) {
                        yield i;
                        i = i + 1;
                    }
                    return 99;
                }
                auto c = while_gen();
                auto r0 = c.resume();
                auto r1 = c.resume();
                auto r2 = c.resume();
                auto r3 = c.resume();
                r0 * 1000 + r1 * 100 + r2 * 10 + r3
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(219));
        });

        test("yield_inside_if_else", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int branch_gen(int x) {
                    if (x > 0) {
                        yield 1;
                    } else {
                        yield -1;
                    }
                    return 0;
                }
                auto c1 = branch_gen(5);
                auto c2 = branch_gen(-3);
                auto r1 = c1.resume();
                auto r2 = c2.resume();
                r1 + r2
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(0));
        });

        test("multiple_yields_in_sequence", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int seq() {
                    yield 10;
                    yield 20;
                    yield 30;
                    yield 40;
                    yield 50;
                    return 0;
                }
                auto c = seq();
                auto sum = 0;
                sum = sum + c.resume();
                sum = sum + c.resume();
                sum = sum + c.resume();
                sum = sum + c.resume();
                sum = sum + c.resume();
                sum = sum + c.resume();
                sum
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(150));
        });

        test("multiple_concurrent_coroutines", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int gen_a() {
                    yield 1;
                    yield 2;
                    return 3;
                }
                coroutine int gen_b() {
                    yield 10;
                    yield 20;
                    return 30;
                }
                auto a = gen_a();
                auto b = gen_b();
                auto r1 = a.resume();
                auto r2 = b.resume();
                auto r3 = a.resume();
                auto r4 = b.resume();
                auto r5 = a.resume();
                auto r6 = b.resume();
                r1 + r2 + r3 + r4 + r5 + r6
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(66));
        });

        test("coroutine_with_parameters", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int multiply_gen(int start, int factor) {
                    int val = start;
                    yield val;
                    val = val * factor;
                    yield val;
                    val = val * factor;
                    return val;
                }
                auto c = multiply_gen(2, 3);
                auto r1 = c.resume();
                auto r2 = c.resume();
                auto r3 = c.resume();
                r1 * 100 + r2 * 10 + r3
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(278));
        });

        test("yield_string_values", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine string greet() {
                    yield "hello";
                    yield "world";
                    return "!";
                }
                auto c = greet();
                auto s = "";
                s = s + c.resume();
                s = s + " ";
                s = s + c.resume();
                s = s + c.resume();
                s
            )");
            check_eq(result.template as<std::string>(), std::string("hello world!"));
        });

        test("nested_loops_with_yield", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine int matrix_gen() {
                    for (int i = 0; i < 2; ++i) {
                        for (int j = 0; j < 3; ++j) {
                            yield i * 10 + j;
                        }
                    }
                    return -1;
                }
                auto c = matrix_gen();
                auto sum = 0;
                while (!c.done()) {
                    sum = sum + c.resume();
                }
                sum
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(35));
        });

        test("coroutine_void_yield", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                coroutine void stepper() {
                    yield;
                    yield;
                    return;
                }
                auto c = stepper();
                c.resume();
                c.resume();
                c.resume();
                auto result = 0;
                if (c.done()) {
                    result = 1;
                }
                result
            )");
            check_eq(result.template as<script_int>(), static_cast<script_int>(1));
        });

        test("yield_no_duplicate_side_effects", [this]() {
            auto eng = jai::foundry::make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(R"(
                auto count = 0;
                coroutine void counter() {
                    for (auto i = 0; i < 5; i++) {
                        count = count + 1;
                        yield;
                    }
                }
                auto c = counter();
                c.resume();
                c.resume();
                c.resume();
            )");
            auto count = eng->get_variable("count").as<int64_t>();
            check_eq(count, int64_t(3));
        });

        test("range_for_over_generator", [this]() {
            auto eng = jai::foundry::make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(R"(
                coroutine int fibonacci() {
                    int a = 0;
                    int b = 1;
                    while (true) {
                        yield a;
                        int temp = a + b;
                        a = b;
                        b = temp;
                    }
                }
                auto sum = 0;
                auto count = 0;
                for (auto x : fibonacci()) {
                    sum = sum + x;
                    count = count + 1;
                    if (count == 7) { break; }
                }
            )");
            check_eq(eng->get_variable("sum").as<int64_t>(), int64_t(20));
            check_eq(eng->get_variable("count").as<int64_t>(), int64_t(7));
        });

        test("range_for_over_finite_generator", [this]() {
            auto eng = jai::foundry::make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(R"(
                coroutine int range(int n) {
                    for (int i = 0; i < n; i++) {
                        yield i;
                    }
                }
                auto total = 0;
                for (auto x : range(5)) {
                    total = total + x;
                }
            )");
            check_eq(eng->get_variable("total").as<int64_t>(), int64_t(10));
        });

        test("for_loop_yield_step_by_step", [this]() {
            auto eng = jai::foundry::make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(R"(
                coroutine int gen() {
                    for (int i = 0; i < 3; ++i) {
                        yield i;
                    }
                    return 99;
                }
                auto c = gen();
                auto r0 = c.resume();
                auto r1 = c.resume();
                auto r2 = c.resume();
                auto r3 = c.resume();
            )");
            check_eq(eng->get_variable("r0").as<int64_t>(), int64_t(0));
            check_eq(eng->get_variable("r1").as<int64_t>(), int64_t(1));
            check_eq(eng->get_variable("r2").as<int64_t>(), int64_t(2));
            check_eq(eng->get_variable("r3").as<int64_t>(), int64_t(99));
        });

        test("nested_coroutine_captures_enclosing_locals", [this]() {
            // A coroutine declared inside a function snapshots the enclosing frame's
            // slot locals at declaration (lambda-style capture); each factory call gets
            // its own independent snapshot.
            auto eng = jai::foundry::make_engine();
            auto result = eng->execute(R"(
                function make_counter(auto start) -> auto {
                    coroutine int counter() {
                        auto v = start;
                        while (v < start + 3) {
                            yield v;
                            v = v + 1;
                        }
                        return 0 - 1;
                    }
                    return counter();
                }
                auto c1 = make_counter(5);
                auto c2 = make_counter(50);
                c1.resume() + c2.resume() + c1.resume() + c2.resume();
            )");
            check_eq(int64_t(112), result.as<int64_t>());
        });

        test("resume_inside_running_function_keeps_caller_frame", [this]() {
            // Suspending used to steal the CALLER's call frames into the coroutine
            // state, leaving the rest of the caller's body frameless (hang/corruption)
            auto eng = jai::foundry::make_engine();
            auto result = eng->execute(R"(
                coroutine int gen2() {
                    yield 10;
                    yield 11;
                    return 0;
                }
                function pair() -> int {
                    auto g = gen2();
                    var local = 100;
                    return g.resume() + g.resume() + local;
                }
                pair() + pair();
            )");
            check_eq(int64_t(242), result.as<int64_t>());
        });

        test("nested_coroutine_declared_and_resumed_inside_function", [this]() {
            auto eng = jai::foundry::make_engine();
            auto result = eng->execute(R"(
                function pair_sum(int base) -> int {
                    coroutine int gen() {
                        yield base * 10;
                        yield base * 10 + 1;
                        return 0;
                    }
                    auto g = gen();
                    return g.resume() + g.resume();
                }
                pair_sum(1) + pair_sum(2);
            )");
            check_eq(int64_t(62), result.as<int64_t>());
        });

        test("coroutine_range_for_over_inner_coroutine", [this]() {
            // Interpreter used to abort the whole process (env-cycle abort) here
            auto eng = jai::foundry::make_engine();
            auto result = eng->execute(R"(
                coroutine int inner() {
                    for (int i = 0; i < 3; i++) {
                        yield i;
                    }
                }
                coroutine int outer() {
                    for (auto x : inner()) {
                        yield x * 10;
                    }
                    return 0 - 1;
                }
                auto o = outer();
                o.resume() * 100 + o.resume() * 10 + o.resume();
            )");
            check_eq(int64_t(120), result.as<int64_t>());
        });

        test("failed_resume_marks_only_that_handle_done", [this]() {
            // The failed handle must report done() even when an unrelated coroutine
            // was resumed between its suspension and its failure
            auto eng = jai::foundry::make_engine();
            auto result = eng->execute(R"(
                function boom() -> auto { throw "die"; }
                coroutine int bad() { yield 1; boom(); yield 2; return 3; }
                coroutine int good() { yield 10; yield 20; return 30; }
                auto vb = bad();
                auto vg = good();
                auto d = 0;
                auto r1 = vb.resume();
                if (vb.done()) { d = d + 1; }
                auto r2 = vg.resume();
                if (vb.done()) { d = d + 2; }
                auto caught = 0;
                try { vb.resume(); } catch (e) { caught = 1; }
                if (vb.done()) { d = d + 4; }
                if (!vg.done()) { d = d + 8; }
                d * 10 + caught;
            )");
            check_eq(int64_t(121), result.as<int64_t>());
        });

        test("yield_per_element_inside_array_range_for", [this]() {
            // Resume used to re-evaluate the container and restart iteration at 0
            auto eng = jai::foundry::make_engine();
            auto result = eng->execute(R"(
                coroutine int walker() {
                    auto acc = 1000;
                    for (auto x : [3, 5, 7]) {
                        yield x;
                        acc = acc + x;
                    }
                    return acc;
                }
                auto w = walker();
                w.resume() * 1000 + w.resume() * 100 + w.resume() * 10 + (w.resume() - 1000);
            )");
            check_eq(int64_t(3585), result.as<int64_t>());
        });

        test("resume_through_by_ref_helper_param", [this]() {
            auto eng = jai::foundry::make_engine();
            auto result = eng->execute(R"(
                coroutine int gen() {
                    for (int i = 1; i <= 4; i++) {
                        yield i * i;
                    }
                    return 0;
                }
                function pump(auto& h) -> auto {
                    auto local = 100;
                    auto v = h.resume();
                    return local + v;
                }
                auto c = gen();
                auto s = 0;
                for (int i = 0; i < 4; i++) {
                    s = s + pump(c);
                }
                s;
            )");
            check_eq(int64_t(430), result.as<int64_t>());
        });

        test("nested_coroutine_snapshot_is_per_declaration", [this]() {
            auto eng = jai::foundry::make_engine();
            auto result = eng->execute(R"(
                function make_gen(int base) -> auto {
                    coroutine int gen() {
                        yield base * 10;
                        yield base * 10 + 1;
                        return 0;
                    }
                    return gen();
                }
                auto g1 = make_gen(1);
                auto g2 = make_gen(2);
                g1.resume() + g1.resume() + g2.resume() + g2.resume();
            )");
            check_eq(int64_t(62), result.as<int64_t>());
        });

        // ===== COROUTINE METHODS (ruling 2026-07) =====
        // `coroutine int steps() { ... }` inside a class: calling it on an instance mints
        // a handle exactly like a free coroutine; 'this' is pinned in the handle (its own
        // method env + receiver) and survives suspension on both backends.

        auto both_backends = [](const char* src) {
            std::string out[2];
            int idx = 0;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execution_budget(0);
                jai::stdlib::register_all(e);
                try { out[idx] = e->execute(src).to_string(); }
                catch (const std::exception& ex) { out[idx] = std::string("ERROR: ") + ex.what(); }
                ++idx;
            }
            return std::make_pair(out[0], out[1]);
        };
        auto check_both = [this, both_backends](const char* src, const std::string& expected, const char* what) {
            auto [i_out, v_out] = both_backends(src);
            check_eq(expected, i_out, std::string("interp: ") + what);
            check_eq(expected, v_out, std::string("vm: ") + what);
        };

        test("method_coroutine_this_across_suspensions", [this, check_both]() {
            check_both(R"(
                class Counter {
                    int x = 10;
                    coroutine int steps() {
                        yield 1;
                        yield this.x;
                        x = x + 5;
                        yield x;
                        return 2.9;
                    }
                }
                auto c = Counter();
                auto g = c.steps();
                var a = g.resume();
                var b = g.resume();
                var d = g.resume();
                var e = g.resume();
                to_string(a) + "|" + to_string(b) + "|" + to_string(d) + "|" +
                    type_of(e) + ":" + to_string(e) + "|" + to_string(g.done()) + "|" + to_string(c.x)
            )", "1|10|15|int:2|true|15",
                "this-field reads/writes across suspensions + typed final return + done()");
        });

        test("method_coroutine_range_for_drives", [this, check_both]() {
            check_both(R"(
                class Gen {
                    int n = 3;
                    coroutine int items() { for (int i = 0; i < n; ++i) { yield i * 10; } }
                }
                auto g = Gen();
                var sum = 0;
                for (auto v : g.items()) { sum += v; }
                sum
            )", "30", "range-for drives a method coroutine");
        });

        test("method_coroutine_two_instances_interleaved", [this, check_both]() {
            check_both(R"(
                class C { int id = 0; C(int i) { id = i; } coroutine int gen() { yield id; yield id * 2; } }
                auto a = C(1); auto b = C(2);
                auto ga = a.gen(); auto gb = b.gen();
                to_string(ga.resume()) + to_string(gb.resume()) + to_string(ga.resume()) + to_string(gb.resume())
            )", "1224", "two instances' coroutines interleave independently");
        });

        test("method_coroutine_pins_instance", [this, check_both]() {
            // The only strong ref to the instance dies with make()'s frame; the handle
            // keeps it alive (receiver + method env pin), so resumes still see fields
            check_both(R"(
                class D { int v = 7; coroutine int g() { yield v; v = 99; yield v; } }
                function make() { auto d = D(); return d.g(); }
                auto h = make();
                to_string(h.resume()) + "|" + to_string(h.resume())
            )", "7|99", "handle keeps the instance alive while suspended");
        });

        test("method_coroutine_inherited_on_derived", [this, check_both]() {
            check_both(R"(
                class Base { int bv = 5; coroutine int gen() { yield bv; yield bv * 2; } }
                class Derived : Base { int dv = 1; }
                auto d = Derived();
                d.bv = 6;
                auto g = d.gen();
                to_string(g.resume()) + "|" + to_string(g.resume())
            )", "6|12", "base coroutine method binds the derived receiver");
        });

        test("static_coroutine_method_rejected", [this]() {
            // Both orders parse-error cleanly; the engine stays usable (parser synchronizes)
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                bool threw = false;
                try { e->execute("class S { static coroutine int f() { yield 1; } }"); }
                catch (const std::exception& ex) {
                    threw = std::string(ex.what()).find("static coroutine methods are not supported") != std::string::npos;
                }
                check(threw, "static coroutine reports the dedicated error");
                bool threw2 = false;
                try { e->execute("class S2 { coroutine static int f() { yield 1; } }"); }
                catch (const std::exception& ex) {
                    threw2 = std::string(ex.what()).find("static coroutine methods are not supported") != std::string::npos;
                }
                check(threw2, "coroutine static reports the dedicated error");
                check_eq(static_cast<script_int>(4), e->execute("2 + 2").as<script_int>());
            }
        });

        test("coroutine_field_rejected", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                bool threw = false;
                try { e->execute("class K { coroutine int x = 5; }"); }
                catch (const std::exception& ex) {
                    threw = std::string(ex.what()).find("'coroutine' in a class body must be followed by a method") != std::string::npos;
                }
                check(threw, "coroutine before a field reports the dedicated error");
            }
        });

        // HOT RELOAD PRECEDENT (free coroutines): redefining the function while a handle
        // is suspended leaves the handle running the OLD body (it pins its own decl +
        // closure env); new calls mint handles over the NEW body.
        test("free_coroutine_redefine_while_suspended", [this]() {
            auto engine = make_engine();
            engine->execute("coroutine int f() { yield 1; yield 2; return 3; }");
            engine->execute("var h = f();");
            check_eq(static_cast<script_int>(1), engine->execute("h.resume();").as<script_int>());
            engine->execute("coroutine int f() { yield 100; return 200; }"); // redefine mid-suspension
            check_eq(static_cast<script_int>(2), engine->execute("h.resume();").as<script_int>());  // old body continues
            check_eq(static_cast<script_int>(3), engine->execute("h.resume();").as<script_int>());
            engine->execute("var h2 = f();");
            check_eq(static_cast<script_int>(100), engine->execute("h2.resume();").as<script_int>()); // new body for new handles
        });

        // A FIELD-CHANGING reload while a method coroutine is suspended: migration runs
        // IN PLACE (kept fields keep their storage nodes — suspended env chains cache
        // field pointers), so the old body resumes over the live, migrated instance.
        // A retype mid-suspension converts the value per the retype ruling and the old
        // body simply sees the converted value (int 40 -> "40"; "40" + 1 concatenates).
        test("method_coroutine_survives_field_changing_reload", [this, check_both]() {
            check_both(R"(
                class W { int v = 1; coroutine int gen() { yield v; yield v + 1; return 0; } }
                var w = W();
                w.v = 40;
                var h = w.gen();
                var a = h.resume();
                class W { int v = 1; string extra = "e"; coroutine int gen() { yield 8; return 0; } }
                var b = h.resume();
                to_string(a) + "|" + to_string(b) + "|" + w.extra
            )", "40|41|e", "field added mid-suspension: old body continues on stable storage");
            check_both(R"(
                class W { int v = 1; coroutine int gen() { yield v; yield v + 1; return 0; } }
                var w = W();
                w.v = 40;
                var h = w.gen();
                var a = h.resume();
                class W { string v = ""; coroutine int gen() { yield 8; return 0; } }
                var b = to_string(h.resume());
                to_string(a) + "|" + b + "|" + w.v
            )", "40|401|40", "retype mid-suspension: old body sees the converted value");
        });

        // Method coroutines MATCH that precedent: a class hot reload while a method
        // coroutine is suspended leaves the suspended handle on the OLD body, resuming
        // against the SAME (migrated) instance; fresh calls use the NEW body.
        // HANDLE REFERENCE SEMANTICS (Dev ruling, 2026-07): a handle IS a reference to a
        // running computation, so copies SHARE the live coroutine (clone() shallow-shares
        // the holder - value.cpp jai_object_type case). Field stores, aliases, by-value
        // params, and container elements all see the same resume/done state.

        test("handle_stored_in_field_resumes", [this, check_both]() {
            check_both(R"(
                coroutine function co() { yield 1; yield 2; }
                class B { var h = null; }
                var b = B();
                b.h = co();
                to_string(b.h.resume()) + "|" + to_string(b.h.resume()) + "|" + to_string(b.h.done())
            )", "1|2|false", "handle lives in a field; resume/done through the field");
        });

        test("handle_copies_share_the_coroutine", [this, check_both]() {
            check_both(R"(
                coroutine function co() { yield 1; yield 2; yield 3; return 0; }
                var h = co();
                var h2 = h;
                var a = h.resume(); var b = h2.resume(); var c = h.resume(); h2.resume();
                to_string(a) + to_string(b) + to_string(c) + "|" +
                    to_string(h.done()) + "|" + to_string(h2.done())
            )", "123|true|true", "aliases interleave one coroutine; done() agrees");
        });

        test("handle_in_array_and_map", [this, check_both]() {
            check_both(R"(
                coroutine function co() { yield 7; yield 8; }
                var a = [co()];
                var m = {"k": a[0]};
                to_string(a[0].resume()) + "|" + to_string(m["k"].resume())
            )", "7|8", "array element and map entry share the stored handle");
        });

        test("handle_passed_by_value_resumes_shared", [this, check_both]() {
            check_both(R"(
                coroutine function co() { yield 42; return 0; }
                function drive(var h) -> int { return h.resume(); }
                var h = co();
                var a = drive(h);
                h.resume();
                to_string(a) + "|" + to_string(h.done())
            )", "42|true", "by-value param resumes the caller's coroutine");
        });

        test("instance_copy_shares_field_held_handle", [this, check_both]() {
            // Object copies stay value-semantic (deep) - but the handle FIELD shares the
            // one coroutine, exactly like a shared_ptr field would share its pointee.
            check_both(R"(
                coroutine function co() { yield 10; yield 20; yield 30; }
                class Holder { var h = null; int n = 0; }
                var a = Holder();
                a.h = co();
                var b = a;
                b.n = 99;
                to_string(a.h.resume()) + to_string(b.h.resume()) + to_string(a.h.resume()) +
                    "|" + to_string(a.n) + to_string(b.n)
            )", "102030|099", "copied instance shares the coroutine, not the ints");
        });

        test("self_receiver_field_handle_boss_pattern", [this, check_both]() {
            // The crawler boss shape: a Game method coroutine whose handle lives in a
            // Game field (receiver pin + field-held handle = deliberate strong cycle;
            // broken by re-assignment/null like any shared structure).
            check_both(R"(
                class Game {
                    var boss_co = null;
                    int phase = 0;
                    coroutine void brain() { phase = 1; yield; phase = 2; yield; phase = 3; }
                    function tick() -> void {
                        if (boss_co == null || boss_co.done()) { boss_co = brain(); }
                        boss_co.resume();
                    }
                }
                var g = Game();
                g.tick(); g.tick(); g.tick();
                to_string(g.phase)
            )", "3", "own-method handle stored in own field drives across ticks");
        });

        test("field_held_handle_survives_field_changing_reload", [this, check_both]() {
            check_both(R"(
                class W { var co = null; int v = 1; coroutine int gen() { yield v; yield v + 1; return 0; } }
                var w = W();
                w.v = 40;
                w.co = w.gen();
                var a = w.co.resume();
                class W { var co = null; int v = 1; string extra = "e"; coroutine int gen() { yield 8; return 0; } }
                var b = w.co.resume();
                var h2 = w.gen();
                to_string(a) + "|" + to_string(b) + "|" + w.extra + "|" + to_string(h2.resume())
            )", "40|41|e|8", "in-place migration keeps the field-held handle on the old body");
        });

        test("engine_teardown_with_field_held_suspended_handle", [this]() {
            // Debug's 0xDD sweep catches any teardown ordering bug: the engine dies while
            // a SUSPENDED handle sits in a field of its own receiver (the strong cycle).
            for (bool use_vm : {false, true}) {
                {
                    auto e = jai::engine::make();
                    if (use_vm) { e->set_backend(jai::backend_type::vm); }
                    e->execution_budget(0);
                    auto r = e->execute(R"(
                        class Game { var co = null; coroutine int brain() { yield 1; yield 2; yield 3; } }
                        var g = Game();
                        g.co = g.brain();
                        g.co.resume()
                    )");
                    check_eq(static_cast<script_int>(1), r.as<script_int>());
                }   // engine + suspended fiber/continuation + cycle all die here
                auto fresh = jai::engine::make();
                check_eq(static_cast<script_int>(4), fresh->execute("2 + 2").as<script_int>());
            }
        });

        test("method_coroutine_hot_reload_matches_free_precedent", [this]() {
            auto engine = make_engine();
            engine->execute(R"(
                class W { int v = 1; coroutine int gen() { yield v; yield v + 1; return 0; } }
                var w = W();
                var h = w.gen();
            )");
            check_eq(static_cast<script_int>(1), engine->execute("h.resume();").as<script_int>());
            engine->execute("class W { int v = 1; coroutine int gen() { yield v * 100; return 0; } }");
            // Old body continues, reading the live (migrated) instance field
            check_eq(static_cast<script_int>(2), engine->execute("h.resume();").as<script_int>());
            engine->execute("var h2 = w.gen();");
            check_eq(static_cast<script_int>(100), engine->execute("h2.resume();").as<script_int>());
        });

    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::coroutine_tests)
