#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/debug/controller.hpp>
#include <jaiscript/testing/temporary_directory.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#include <future>
#include <filesystem>
#include <fstream>

using namespace jai::foundry;

namespace jai::foundry::tests {

// The old connector performed this normalization before its native setter. Keep
// that baseline spelling available while the new source-path entry point is added.
template<typename Controller>
void set_source_breakpoints_for_test(Controller& dbg, const std::string& path, std::vector<int> lines) {
    if constexpr (requires { dbg.set_source_breakpoints(path, lines); }) {
        dbg.set_source_breakpoints(path, std::move(lines));
    } else {
        dbg.set_breakpoints(std::filesystem::weakly_canonical(path).string(), std::move(lines));
    }
}

// Phase 5: BOTH backends carry the statement hook, driven by the one engine-owned
// controller. The core flows run twice — an interpreter engine and a vm engine built
// explicitly (not via the backend-aware make_engine(); the suite is backend-agnostic
// by construction, so it runs identically under --backend=vm).
class debugger_tests : public suite {
public:
    debugger_tests() : suite("Debugger") {}

    // Benchmark engines (one per cost-model configuration), built lazily inside the
    // benchmark lambdas so setup cost stays out of the measured loop.
    std::shared_ptr<jai::engine> bench_eng_;

    void pre_test() override { bench_eng_.reset(); }

    static std::shared_ptr<jai::engine> make_backend_engine(jai::backend_type type) {
        auto eng = jai::engine::make();
        eng->set_backend(type);
        return eng;
    }

    // Full core flow: breakpoint -> inspect -> step -> inspect -> resume. Identical
    // DAP-visible behavior expected from both backends.
    void breakpoint_inspect_step_resume(jai::backend_type type) {
        auto eng = make_backend_engine(type);
        auto& dbg = eng->debugger();

        std::mutex m;
        std::condition_variable cv;
        bool stopped = false;
        jai::debug::stop_info last;
        dbg.set_on_stopped([&](const jai::debug::stop_info& si) {
            std::lock_guard<std::mutex> lk(m);
            last = si;
            stopped = true;
            cv.notify_all();
        });

        auto wait_for_stop = [&]() -> bool {
            std::unique_lock<std::mutex> lk(m);
            bool got = cv.wait_for(lk, std::chrono::seconds(5), [&] { return stopped; });
            stopped = false;   // re-arm for the next stop
            return got;
        };

        dbg.set_breakpoints("<script>", {2});
        dbg.set_enabled(true);

        const std::string src =
            "var x = 1;\n"       // line 1
            "var y = 2;\n"       // line 2  (breakpoint: stop BEFORE this runs)
            "var z = x + y;\n";  // line 3

        std::atomic<bool> done{false};
        std::thread script([&] { eng->execute(src); done = true; });

        // --- breakpoint hit at line 2 ---
        check_true(wait_for_stop());
        check_eq(std::string("breakpoint"), last.reason);
        check_eq(2, last.line);
        check_true(dbg.is_paused());

        // inspect on the parked thread: line 1 ran (x==1), line 2 hasn't (y undefined)
        std::promise<int> px;
        dbg.post_command([&] { px.set_value(static_cast<int>(dbg.get_variable("x").as_int())); });
        check_eq(1, px.get_future().get());

        // --- step to line 3 ---
        dbg.step_into();
        check_true(wait_for_stop());
        check_eq(std::string("step"), last.reason);
        check_eq(3, last.line);

        // now line 2 has run: y==2
        std::promise<int> py;
        dbg.post_command([&] { py.set_value(static_cast<int>(dbg.get_variable("y").as_int())); });
        check_eq(2, py.get_future().get());

        // --- resume to completion ---
        dbg.resume();
        script.join();
        check_true(done.load());
        check_eq(static_cast<int64_t>(3), eng->get_variable("z").as_int());
    }

    // A breakpoint on a loop-body line re-fires every iteration (the vm's boundary
    // detector must treat a backward jump into the same statement as a fresh statement).
    void loop_breakpoint_hits_each_iteration(jai::backend_type type) {
        auto eng = make_backend_engine(type);
        auto& dbg = eng->debugger();

        std::mutex m;
        std::condition_variable cv;
        bool stopped = false;
        bool finished = false;
        dbg.set_on_stopped([&](const jai::debug::stop_info&) {
            std::lock_guard<std::mutex> lk(m);
            stopped = true;
            cv.notify_all();
        });

        dbg.set_breakpoints("<script>", {3});
        dbg.set_enabled(true);

        const std::string src =
            "auto total = 0;\n"                      // line 1
            "for (auto i = 0; i < 3; i += 1) {\n"    // line 2
            "    total += i;\n"                      // line 3  (breakpoint)
            "}\n";                                   // line 4

        std::thread script([&] {
            eng->execute(src);
            std::lock_guard<std::mutex> lk(m);
            finished = true;
            cv.notify_all();
        });

        int hits = 0;
        for (;;) {
            std::unique_lock<std::mutex> lk(m);
            if (!cv.wait_for(lk, std::chrono::seconds(5), [&] { return stopped || finished; })) break;
            if (finished) break;
            stopped = false;
            ++hits;
            lk.unlock();
            dbg.resume();
        }
        dbg.set_enabled(false);   // stray stop can never park past this; join is hang-proof
        script.join();
        check_eq(3, hits);
        check_eq(static_cast<int64_t>(3), eng->get_variable("total").as_int());
    }

    // Named frame locals at a stop inside a function: parameters + reached body locals
    // with their live values. Interpreter reads the frame's function AST; the vm
    // reconstructs slot names lazily from the chunk's decl/load/store operands.
    void frame_locals_at_breakpoint(jai::backend_type type) {
        auto eng = make_backend_engine(type);
        auto& dbg = eng->debugger();

        std::mutex m;
        std::condition_variable cv;
        bool stopped = false;
        dbg.set_on_stopped([&](const jai::debug::stop_info&) {
            std::lock_guard<std::mutex> lk(m);
            stopped = true;
            cv.notify_all();
        });

        dbg.set_breakpoints("<script>", {4});
        dbg.set_enabled(true);

        const std::string src =
            "function calc(int a, int b) -> int {\n"   // line 1
            "    int c = a + b;\n"                     // line 2
            "    int d = c * 2;\n"                     // line 3
            "    return d;\n"                          // line 4  (breakpoint: a,b,c,d live)
            "}\n"
            "auto r = calc(3, 4);\n";                  // line 6

        std::atomic<bool> done{false};
        std::thread script([&] { eng->execute(src); done = true; });

        bool got_stop = false;
        {
            std::unique_lock<std::mutex> lk(m);
            got_stop = cv.wait_for(lk, std::chrono::seconds(5), [&] { return stopped; });
        }

        // Gather while parked, then ALWAYS release the script thread before asserting —
        // a failed check must never leave a parked thread for the std::thread dtor.
        std::vector<jai::debug::variable_info> locals;
        bool got_locals = false;
        if (got_stop) {
            std::promise<std::vector<jai::debug::variable_info>> pv;
            auto fut = pv.get_future();
            dbg.post_command([&] { pv.set_value(dbg.list_locals()); });
            got_locals = fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
            if (got_locals) locals = fut.get();
            dbg.resume();
        }
        dbg.detach();
        script.join();

        check_true(got_stop);
        check_true(got_locals);
        auto value_of = [&](const char* name) -> std::string {
            for (const auto& v : locals) { if (v.name == name) return v.value; }
            return "<missing>";
        };
        check_eq(std::string("3"), value_of("a"));
        check_eq(std::string("4"), value_of("b"));
        check_eq(std::string("7"), value_of("c"));
        check_eq(std::string("14"), value_of("d"));
        check_true(done.load());
        check_eq(static_cast<int64_t>(14), eng->get_variable("r").as_int());
    }

    // A parameter whose ONLY use is a bare compound rhs (`total += bump`) keeps its name
    // at a stop: the vm fuses that op_load away, so naming rides the compound_fused
    // proto's bare rhs operand (the interpreter reads the frame's AST as usual).
    void frame_locals_name_bare_compound_rhs(jai::backend_type type) {
        auto eng = make_backend_engine(type);
        auto& dbg = eng->debugger();

        std::mutex m;
        std::condition_variable cv;
        bool stopped = false;
        dbg.set_on_stopped([&](const jai::debug::stop_info&) {
            std::lock_guard<std::mutex> lk(m);
            stopped = true;
            cv.notify_all();
        });

        dbg.set_breakpoints("<script>", {4});
        dbg.set_enabled(true);

        const std::string src =
            "function acc(int seed, int bump) -> int {\n"   // line 1
            "    int total = seed;\n"                       // line 2
            "    total += bump;\n"                          // line 3  (bare compound rhs)
            "    return total;\n"                           // line 4  (breakpoint)
            "}\n"
            "auto r = acc(30, 12);\n";                      // line 6

        std::atomic<bool> done{false};
        std::thread script([&] { eng->execute(src); done = true; });

        bool got_stop = false;
        {
            std::unique_lock<std::mutex> lk(m);
            got_stop = cv.wait_for(lk, std::chrono::seconds(5), [&] { return stopped; });
        }

        std::vector<jai::debug::variable_info> locals;
        bool got_locals = false;
        if (got_stop) {
            std::promise<std::vector<jai::debug::variable_info>> pv;
            auto fut = pv.get_future();
            dbg.post_command([&] { pv.set_value(dbg.list_locals()); });
            got_locals = fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
            if (got_locals) locals = fut.get();
            dbg.resume();
        }
        dbg.detach();
        script.join();

        check_true(got_stop);
        check_true(got_locals);
        auto value_of = [&](const char* name) -> std::string {
            for (const auto& v : locals) { if (v.name == name) return v.value; }
            return "<missing>";
        };
        check_eq(std::string("30"), value_of("seed"));
        check_eq(std::string("12"), value_of("bump"));
        check_eq(std::string("42"), value_of("total"));
        check_true(done.load());
        check_eq(static_cast<int64_t>(42), eng->get_variable("r").as_int());
    }

    // Dev ruling: parallel regions are ATOMIC to the debugger. A breakpoint inside the
    // body never fires while the body runs as part of a region — including chunk 0 on
    // the calling thread (structural: every region context is a fresh slot backend the
    // controller was never wired to) — but the SAME body called serially hits normally.
    void parallel_region_atomic_to_debugger(jai::backend_type type) {
        auto eng = make_backend_engine(type);
        auto& dbg = eng->debugger();

        std::mutex m;
        std::condition_variable cv;
        bool stopped = false;
        bool finished = false;
        std::vector<jai::debug::stop_info> stops;
        dbg.set_on_stopped([&](const jai::debug::stop_info& si) {
            std::lock_guard<std::mutex> lk(m);
            stops.push_back(si);
            stopped = true;
            cv.notify_all();
        });

        dbg.set_breakpoints("<script>", {2, 6});

        dbg.set_enabled(true);

        const std::string src =
            "int double_it(int x) {\n"                            // line 1
            "    return x * 2;\n"                                 // line 2  (breakpoint)
            "}\n"                                                 // line 3
            "auto arr = [1, 2, 3, 4];\n"                          // line 4
            "auto pout = parallel_transform(arr, double_it);\n"   // line 5 (region: MUST be silent)
            "auto marker = 0;\n"                                  // line 6  (breakpoint: order sentinel)
            "auto serial = double_it(21);\n";                     // line 7 (serial: hits)

        std::thread script([&] {
            eng->execute(src);
            std::lock_guard<std::mutex> lk(m);
            finished = true;
            cv.notify_all();
        });

        int hits = 0;
        for (;;) {
            std::unique_lock<std::mutex> lk(m);
            if (!cv.wait_for(lk, std::chrono::seconds(10), [&] { return stopped || finished; })) break;
            if (finished) break;
            stopped = false;
            ++hits;
            lk.unlock();
            dbg.resume();
        }
        dbg.set_enabled(false);   // stray stop can never park past this; join is hang-proof
        script.join();
        if (hits != 2) {
            for (const auto& s : stops) {
                std::cout << "    stop: " << s.reason << " " << s.file << ":" << s.line
                          << " depth=" << s.depth << std::endl;
            }
        }
        check_eq(2, hits);   // marker + serial call ONLY; the region ran the body 4x silently
        check_eq(static_cast<size_t>(2), stops.size());
        check_eq(6, stops[0].line);   // region produced no stop before the sentinel
        check_eq(2, stops[1].line);
        check_eq(static_cast<int64_t>(42), eng->get_variable("serial").as_int());
        // pout is an inferred array<int> (raw buffer): the host extracts the typed vector
        const auto pout = eng->get_variable("pout").as<std::vector<jai::script_int>>();
        check_eq(static_cast<size_t>(4), pout.size());
        check_eq(static_cast<int64_t>(8), pout[3]);
    }

    // How a run is driven at each stop: one step kind, a fixed cycle of them, or breakpoints on the odd lines and a resume.
    enum class drive { into, over, mixed, odd_breakpoints };

    // Drives a whole run from its first statement on the script thread (the stop callback queues the next step before the park
    // waits) and returns the stop lines. Stops past the cap resume, so a runaway sequence still ends.
    static std::vector<int> step_line_sequence(jai::backend_type type, const std::string& src, drive how = drive::into) {
        auto eng = make_backend_engine(type);
        auto& dbg = eng->debugger();
        std::vector<int> lines;
        dbg.set_on_stopped([&](const jai::debug::stop_info& si) {
            lines.push_back(si.line);
            if (lines.size() > 4000 || how == drive::odd_breakpoints) { dbg.resume(); return; }
            static constexpr jai::debug::step_mode mixed[] = { jai::debug::step_mode::into, jai::debug::step_mode::into,
                jai::debug::step_mode::into, jai::debug::step_mode::over, jai::debug::step_mode::into, jai::debug::step_mode::out };
            const auto mode = how == drive::mixed ? mixed[(lines.size() - 1) % 6]
                : how == drive::over ? jai::debug::step_mode::over : jai::debug::step_mode::into;
            switch (mode) {
                case jai::debug::step_mode::over: dbg.step_over(); break;
                case jai::debug::step_mode::out: dbg.step_out(); break;
                default: dbg.step_into(); break;
            }
        });
        if (how == drive::odd_breakpoints) {
            std::vector<int> odd;
            for (int line = 1; line < 64; line += 2) { odd.push_back(line); }
            dbg.set_breakpoints("<script>", odd);
        }
        dbg.set_enabled(true);
        if (how != drive::odd_breakpoints) { dbg.request_pause(); }
        try { eng->execute(src); } catch (...) { lines.push_back(-1); }
        dbg.set_enabled(false);
        return lines;
    }

    static std::string line_list(const std::vector<int>& lines) {
        std::string out;
        for (int l : lines) { out += (out.empty() ? "" : " ") + std::to_string(l); }
        return out;
    }

    void forge_tests() override {
        test("step_into_line_sequence_through_hot_loops", [this]() {
            const std::string src =
                "int s = 0;\n"                                   // 1
                "int t = 0;\n"                                   // 2
                "for (int i = 0; i < 4; i += 1) {\n"             // 3
                "    t = t + 60;\n"                              // 4
                "    if (t > 100) { s = 0; }\n"                  // 5
                "}\n"
                "auto done = s + t;\n";                          // 7
            // one stop per statement execution: the header once, then each iteration's statements (the then-branch's
            // block holds a statement, it is not one)
            const std::string expected = "1 2 3 4 5 4 5 5 4 5 5 4 5 5 7";
            check_eq(expected, line_list(step_line_sequence(jai::backend_type::interpreter, src)));
            check_eq(expected, line_list(step_line_sequence(jai::backend_type::vm, src)), "the vm stops where the interpreter stops");
        });

        test("step_line_sequences_match_the_interpreter_over_statement_shapes", [this]() {
            struct shape { const char* name; const char* src; };
            const shape shapes[] = {
                { "untyped loop, if last", "var s = 0;\nvar t = 0;\nfor (var i = 0; i < 4; i += 1) {\n    t = t + 60;\n    if (t > 100) { s = 0; }\n}\nvar done = s;\n" },
                { "if/else blocks", "var a = 3;\nif (a > 2) {\n    a = 1;\n} else {\n    a = 2;\n}\nif (a > 2) {\n    a = 5;\n} else {\n    a = 6;\n}\nvar b = a;\n" },
                { "if/else one line", "var a = 3;\nif (a > 2) a = 1; else a = 2;\nif (a > 2) a = 5; else { a = 6; }\nvar b = a;\n" },
                { "else if chain", "var a = 0;\nfor (var i = 0; i < 4; i += 1) {\n    if (i == 0) a += 1;\n    else if (i == 1) a += 2;\n    else if (i == 2) { a += 3; }\n    else a += 4;\n}\nvar b = a;\n" },
                { "while", "var n = 0;\nwhile (n < 3) {\n    n += 1;\n}\nwhile (n < 6) n += 1;\nvar b = n;\n" },
                { "typed while", "int n = 0;\nint m = 0;\nwhile (n < 5) {\n    n += 1;\n    m += n;\n}\nauto b = m;\n" },
                { "do while", "var n = 0;\ndo {\n    n += 1;\n} while (n < 3);\nvar b = n;\n" },
                { "counted for, typed", "int s = 0;\nfor (int i = 0; i < 5; i++) {\n    s += i;\n}\nauto b = s;\n" },
                { "nested loops break continue", "var s = 0;\nfor (var i = 0; i < 3; i += 1) {\n    for (var j = 0; j < 3; j += 1) {\n        if (j == 1) continue;\n        if (i == 2) break;\n        s += j;\n    }\n}\nvar b = s;\n" },
                { "range for", "var s = 0;\nvar arr = [1, 2, 3];\nfor (auto x : arr) {\n    s += x;\n}\nfor (auto& y : arr) y *= 2;\nvar b = s;\n" },
                { "switch", "var r = 0;\nfor (var i = 0; i < 4; i += 1) {\n    switch (i) {\n        case 0:\n            r += 1;\n        case 1:\n            r += 2;\n            fallthrough;\n        case 2:\n            r += 3;\n        default:\n            r += 4;\n    }\n}\nvar b = r;\n" },
                { "and or chains", "var a = 1;\nvar b = 0;\nfor (var i = 0; i < 3; i += 1) {\n    if (a > 0 && b < 2 || i == 7) {\n        b += 1;\n    }\n    var c = a > 0 && b > 1;\n    if (!(a < 0 || b > 5)) a += 1;\n}\nvar d = b;\n" },
                { "ternary", "var a = 0;\nfor (var i = 0; i < 3; i += 1) {\n    a = i > 1 ? a + 10 : a + 1;\n    var c = (a > 5) ? 1 : 2;\n}\nvar d = a;\n" },
                { "return with expression", "function f(int x) -> int {\n    if (x > 2) {\n        return x * 2;\n    }\n    return x + 1;\n}\nvar s = 0;\nfor (var i = 0; i < 4; i += 1) {\n    s += f(i);\n}\nvar d = s;\n" },
                { "typed calls in a loop", "int add(int a, int b) {\n    return a + b;\n}\nint s = 0;\nfor (int i = 0; i < 4; i++) {\n    s = add(s, i);\n}\nauto d = s;\n" },
                { "void call statement", "var total = 0;\nfunction bump(int by) {\n    total += by;\n}\nfor (var i = 0; i < 3; i += 1) {\n    bump(i);\n}\nvar d = total;\n" },
                { "lambda", "var k = 2;\nauto mul = [k](int x) {\n    return x * k;\n};\nvar s = 0;\nfor (var i = 0; i < 3; i += 1) {\n    s += mul(i);\n}\nvar d = s;\n" },
                { "class method", "class C {\n    int v = 0;\n    void add(int x) {\n        v += x;\n    }\n    int get() { return v; }\n}\nauto c = C();\nfor (var i = 0; i < 3; i += 1) {\n    c.add(i);\n}\nvar d = c.get();\n" },
                { "try catch", "var r = 0;\ntry {\n    r = 1;\n    throw \"x\";\n    r = 2;\n} catch (e) {\n    r = 3;\n}\nvar d = r;\n" },
                { "blocks and empty branches", "var a = 1;\n{\n    var b = 2;\n    a += b;\n}\nif (a > 0) { }\nif (a < 0) { } else { a += 1; }\nvar d = a;\n" },
                { "while first in a function", "function drain(int n) -> int {\n    while (n > 0) {\n        n -= 1;\n    }\n    return n;\n}\nvar s = 0;\nfor (var i = 0; i < 3; i += 1) {\n    s += drain(i);\n}\nvar d = s;\n" },
                { "single statement function called twice", "function twice(int x) -> int { return x * 2; }\nvar a = twice(1);\nvar b = twice(a);\nvar c = twice(twice(b));\n" },
                { "recursion", "function fact(int n) -> int {\n    if (n <= 1) return 1;\n    return n * fact(n - 1);\n}\nvar f = fact(4);\n" },
                { "builtin callback", "function g(int x) -> int {\n    return x * 2;\n}\nvar arr = [1, 2, 3];\nvar m = array::map(arr, g);\nvar n = m.size();\n" },
                { "dot builtin callbacks", "function less(int a, int b) -> bool { return a < b; }\narray<int> arr = [3, 1, 2];\narr.sort(less);\nvar t = 0;\narr.for_each([&](int x) {\n    t += x;\n});\narr.sort([](int a, int b) -> bool { return a > b; });\nvar n = arr.filter((x) => x > 1).size();\n" },
                { "nested function and class declarations", "var r = 0;\nfor (var i = 0; i < 2; i += 1) {\n    function inner(int v) -> int { return v + 1; }\n    r = inner(r);\n}\nclass K { int z = 3; }\nauto k = K();\nvar d = r + k.z;\n" },
                { "coroutine", "coroutine gen(int n) -> int {\n    for (int i = 0; i < n; i++) {\n        yield i;\n    }\n}\nvar s = 0;\nfor (auto v : gen(3)) {\n    s += v;\n}\nvar d = s;\n" },
                { "namespace", "namespace ns {\n    int base = 4;\n    int twice(int x) { return x * 2; }\n    class P { int q = 1; }\n    int third = base + 1;\n}\nvar d = ns::twice(ns::base);\n" },
                { "return from inside a loop", "function find(int target) -> int {\n    for (int i = 0; i < 10; i++) {\n        if (i == target) {\n            return i;\n        }\n    }\n    return -1;\n}\nvar a = find(2);\nvar b = find(20);\n" },
                { "throw through a call", "function boom(int x) {\n    if (x > 1) {\n        throw \"x\";\n    }\n}\nvar r = 0;\nfor (var i = 0; i < 3; i += 1) {\n    try {\n        boom(i);\n        r += 1;\n    } catch (e) {\n        r += 10;\n    }\n}\nvar d = r;\n" },
                { "typed cell loop body", "int s = 0;\nint t = 0;\nint i = 0;\nwhile (i < 6) {\n    t = t + 3;\n    s = s + t;\n    i = i + 1;\n}\nauto d = s;\n" },
            };
            std::string mismatches;
            for (const auto& sh : shapes) {
                for (auto how : { drive::into, drive::over, drive::mixed, drive::odd_breakpoints }) {
                    const auto interp = line_list(step_line_sequence(jai::backend_type::interpreter, sh.src, how));
                    const auto vm = line_list(step_line_sequence(jai::backend_type::vm, sh.src, how));
                    if (interp != vm) {
                        mismatches += std::string("\n    ") + sh.name + (how == drive::into ? " (into)" : how == drive::over ? " (over)" : how == drive::mixed ? " (mixed)" : " (breakpoints)")
                            + "\n      interpreter: " + interp + "\n      vm:          " + vm;
                    }
                }
            }
            check_eq(std::string(), mismatches);
        });

        test("native_and_source_breakpoints_with_same_spelling_remain_distinct", [this]() {
            auto eng = make_engine();
            eng->jaibite_cache(false);
            auto& dbg = eng->debugger();
            const std::string raw = "./virtual/../mixed.jai";
            const std::string canonical = std::filesystem::weakly_canonical(raw).string();
            std::vector<jai::debug::stop_info> stops;
            dbg.set_on_stopped([&](const jai::debug::stop_info& info) {
                stops.push_back(info);
                dbg.resume();
            });
            dbg.set_breakpoints(raw, {1});
            set_source_breakpoints_for_test(dbg, raw, {2});
            dbg.set_enabled(true);
            eng->execute_file_source(raw, "1;\n2;\n");
            eng->execute_file_source(canonical, "1;\n2;\n");
            dbg.set_enabled(false);
            check_eq(size_t(2), stops.size());
            check_eq(raw, stops[0].file);
            check_eq(1, stops[0].line);
            check_eq(canonical, stops[1].file);
            check_eq(2, stops[1].line);
        });

        test("execute_file_attributes_the_canonical_path_so_source_breakpoints_bind", [this]() {
            auto dir = jai::foundry::test_temp_directory() / "execute_file_canonical";
            std::filesystem::create_directories(dir);
            { std::ofstream(dir / "mixed.jai") << "var a = 1;\nvar b = 2;\n"; }
            const std::string canonical = std::filesystem::canonical(dir / "mixed.jai").string();
            const std::string mixed = dir.generic_string() + "/./" + "mixed.jai";   // forward slashes and a dot segment
            auto eng = make_engine();
            eng->jaibite_cache(false);
            auto& dbg = eng->debugger();
            std::vector<jai::debug::stop_info> stops;
            dbg.set_on_stopped([&](const jai::debug::stop_info& info) {
                stops.push_back(info);
                dbg.resume();
            });
            set_source_breakpoints_for_test(dbg, canonical, {2});
            dbg.set_enabled(true);
            eng->execute_file(mixed);
            dbg.set_enabled(false);
            check_eq(size_t(1), stops.size(), "a breakpoint set on the canonical path binds a file run through another spelling");
            if (!stops.empty()) { check_eq(canonical, stops[0].file); }
            { std::ofstream(dir / "throws.jai") << "function f() { throw \"x\"; }\nf();\n"; }
            try {
                eng->execute_file(dir.generic_string() + "/./throws.jai");
            } catch (...) {}
            auto trace = eng->last_stack_trace();
            check_false(trace.empty());
            if (!trace.empty()) { check_eq(std::filesystem::canonical(dir / "throws.jai").string(), trace[0].file); }
        });

        test("native_breakpoint_labels_survive_disable_and_rearm", [this]() {
            auto eng = make_engine();
            eng->jaibite_cache(false);
            auto& dbg = eng->debugger();
            const std::string first = "./virtual/../first.jai";
            const std::string second = "./virtual/../second.jai";
            std::vector<jai::debug::stop_info> stops;
            dbg.set_on_stopped([&](const jai::debug::stop_info& info) {
                stops.push_back(info);
                dbg.resume();
            });
            dbg.set_breakpoints(first, {1});
            dbg.set_enabled(true);
            eng->execute_file_source(first, "1;\n2;\n");
            check_eq(size_t(1), stops.size());

            dbg.set_enabled(false);
            check_false(dbg.sync_hot_state());
            dbg.set_breakpoints(second, {2});
            check_false(dbg.sync_hot_state());
            dbg.set_enabled(true);
            eng->execute_file_source(first, "1;\n2;\n");
            eng->execute_file_source(second, "1;\n2;\n");
            check_eq(size_t(3), stops.size(), "rearm must retain installed and queued breakpoints");
            check_eq(first, stops[1].file, "native breakpoint labels are exact, never filesystem-normalized");
            check_eq(second, stops[2].file);
            check_eq(2, stops[2].line);

            dbg.set_breakpoints(first, {2});
            dbg.clear_breakpoints();
            dbg.set_breakpoints(second, {1});
            eng->execute_file_source(first, "1;\n2;\n");
            eng->execute_file_source(second, "1;\n2;\n");
            check_eq(size_t(4), stops.size(), "clear-all must preserve its order among queued updates");
            check_eq(second, stops[3].file);
            check_eq(1, stops[3].line);
            dbg.set_enabled(false);
        });

        test("breakpoint_edit_while_paused_applies_at_park_exit", [this]() {
            auto eng = make_engine();
            auto& dbg = eng->debugger();
            std::vector<int> lines;
            dbg.set_on_stopped([&](const jai::debug::stop_info& info) {
                lines.push_back(info.line);
                if (lines.size() == 1) dbg.set_breakpoints("<script>", {2});
                dbg.resume();
            });
            dbg.set_breakpoints("<script>", {1});
            dbg.set_enabled(true);
            eng->execute("1;\n2;\n3;\n");
            dbg.set_enabled(false);
            check_eq(size_t(2), lines.size());
            check_eq(1, lines[0]);
            check_eq(2, lines[1]);
        });

        test("breakpoint_inspect_step_resume", [this]() {
            breakpoint_inspect_step_resume(jai::backend_type::interpreter);
        });
        test("vm_breakpoint_inspect_step_resume", [this]() {
            breakpoint_inspect_step_resume(jai::backend_type::vm);
        });
        test("loop_breakpoint_hits_each_iteration", [this]() {
            loop_breakpoint_hits_each_iteration(jai::backend_type::interpreter);
        });
        test("vm_loop_breakpoint_hits_each_iteration", [this]() {
            loop_breakpoint_hits_each_iteration(jai::backend_type::vm);
        });
        test("frame_locals_at_breakpoint", [this]() {
            frame_locals_at_breakpoint(jai::backend_type::interpreter);
        });
        test("vm_frame_locals_at_breakpoint", [this]() {
            frame_locals_at_breakpoint(jai::backend_type::vm);
        });
        test("frame_locals_name_bare_compound_rhs", [this]() {
            frame_locals_name_bare_compound_rhs(jai::backend_type::interpreter);
        });
        test("vm_frame_locals_name_bare_compound_rhs", [this]() {
            frame_locals_name_bare_compound_rhs(jai::backend_type::vm);
        });
        test("parallel_region_atomic_to_debugger", [this]() {
            parallel_region_atomic_to_debugger(jai::backend_type::interpreter);
        });
        test("vm_parallel_region_atomic_to_debugger", [this]() {
            parallel_region_atomic_to_debugger(jai::backend_type::vm);
        });

        test("detach_wakes_a_parked_thread", [this]() {
            // A client disconnect / shutdown must force-resume a parked script, never hang.
            auto eng = jai::foundry::make_engine();
            auto& dbg = eng->debugger();

            std::mutex m;
            std::condition_variable cv;
            bool stopped = false;
            dbg.set_on_stopped([&](const jai::debug::stop_info&) {
                std::lock_guard<std::mutex> lk(m);
                stopped = true;
                cv.notify_all();
            });

            dbg.set_breakpoints("<script>", {1});
            dbg.set_enabled(true);

            std::atomic<bool> done{false};
            std::thread script([&] { eng->execute("var a = 1;\nvar b = 2;\n"); done = true; });

            {
                std::unique_lock<std::mutex> lk(m);
                check_true(cv.wait_for(lk, std::chrono::seconds(5), [&] { return stopped; }));
            }
            check_true(dbg.is_paused());

            dbg.detach();                 // no resume() — detach must unblock it
            script.join();
            check_true(done.load());
        });

        // ---- cost-model benchmarks (verbose runs only; non-asserting) ----
        // The same script as Performance Benchmarks' "Hot Loop (1000 iterations)", run
        // under each debugger configuration from docs/DEBUGGER_DESIGN.md "Debugger
        // performance". The loop-body statement sits on line 4 of the source.
        static constexpr const char* hot_loop_src = R"(
                auto sum = 0;
                for (auto i = 0; i < 1000; i += 1) {
                    sum += i * 2;
                }
            )";

        benchmark("Hot Loop: no debugger constructed", [this]() {
            if (!bench_eng_) bench_eng_ = jai::foundry::make_engine();
            bench_eng_->execute(hot_loop_src);
        });

        benchmark("Hot Loop: controller constructed, no session", [this]() {
            if (!bench_eng_) {
                bench_eng_ = jai::foundry::make_engine();
                bench_eng_->debugger();   // = connector listening, no client attached
            }
            bench_eng_->execute(hot_loop_src);
        });

        benchmark("Hot Loop: session enabled, no breakpoints", [this]() {
            if (!bench_eng_) {
                bench_eng_ = jai::foundry::make_engine();
                bench_eng_->debugger().set_enabled(true);
            }
            bench_eng_->execute(hot_loop_src);
        });

        benchmark("Hot Loop: session enabled, bp in cold file", [this]() {
            if (!bench_eng_) {
                bench_eng_ = jai::foundry::make_engine();
                auto& dbg = bench_eng_->debugger();
                dbg.set_breakpoints("cold.jai", {50});   // no line in the script matches
                dbg.set_enabled(true);
            }
            bench_eng_->execute(hot_loop_src);
        });

        benchmark("Hot Loop: session enabled, bp line collides", [this]() {
            if (!bench_eng_) {
                bench_eng_ = jai::foundry::make_engine();
                auto& dbg = bench_eng_->debugger();
                dbg.set_breakpoints("cold.jai", {4});    // same line as the hot statement
                dbg.set_enabled(true);
            }
            bench_eng_->execute(hot_loop_src);
        });

        // Same 5 configurations on the vm backend (phase 5: the per-dispatch gate must
        // hold the vm's hot-loop band).
        benchmark("Hot Loop [vm]: no debugger constructed", [this]() {
            if (!bench_eng_) bench_eng_ = make_backend_engine(jai::backend_type::vm);
            bench_eng_->execute(hot_loop_src);
        });

        benchmark("Hot Loop [vm]: controller constructed, no session", [this]() {
            if (!bench_eng_) {
                bench_eng_ = make_backend_engine(jai::backend_type::vm);
                bench_eng_->debugger();
            }
            bench_eng_->execute(hot_loop_src);
        });

        benchmark("Hot Loop [vm]: session enabled, no breakpoints", [this]() {
            if (!bench_eng_) {
                bench_eng_ = make_backend_engine(jai::backend_type::vm);
                bench_eng_->debugger().set_enabled(true);
            }
            bench_eng_->execute(hot_loop_src);
        });

        benchmark("Hot Loop [vm]: session enabled, bp in cold file", [this]() {
            if (!bench_eng_) {
                bench_eng_ = make_backend_engine(jai::backend_type::vm);
                auto& dbg = bench_eng_->debugger();
                dbg.set_breakpoints("cold.jai", {50});
                dbg.set_enabled(true);
            }
            bench_eng_->execute(hot_loop_src);
        });

        benchmark("Hot Loop [vm]: session enabled, bp line collides", [this]() {
            if (!bench_eng_) {
                bench_eng_ = make_backend_engine(jai::backend_type::vm);
                auto& dbg = bench_eng_->debugger();
                dbg.set_breakpoints("cold.jai", {4});
                dbg.set_enabled(true);
            }
            bench_eng_->execute(hot_loop_src);
        });
    }
};

} // namespace jai::foundry::tests

using debugger_tests = jai::foundry::tests::debugger_tests;
FOUNDRY_REGISTER(debugger_tests)
