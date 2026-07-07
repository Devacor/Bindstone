#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/debug/controller.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#include <future>

using namespace jai::foundry;

namespace jai::foundry::tests {

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
        if (type != jai::backend_type::interpreter) eng->set_backend(type);
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
        const auto& pout = eng->get_variable("pout").as_array();
        check_eq(static_cast<size_t>(4), pout.size());
        check_eq(static_cast<int64_t>(8), pout[3].as_int());
    }

    void forge_tests() override {
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
        test("parallel_region_atomic_to_debugger", [this]() {
            parallel_region_atomic_to_debugger(jai::backend_type::interpreter);
        });
        test("vm_parallel_region_atomic_to_debugger", [this]() {
            parallel_region_atomic_to_debugger(jai::backend_type::vm);
        });

        test("detach_wakes_a_parked_thread", [this]() {
            // A client disconnect / shutdown must force-resume a parked script, never hang.
            auto eng = jai::engine::make();
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
            if (!bench_eng_) bench_eng_ = jai::engine::make();
            bench_eng_->execute(hot_loop_src);
        });

        benchmark("Hot Loop: controller constructed, no session", [this]() {
            if (!bench_eng_) {
                bench_eng_ = jai::engine::make();
                bench_eng_->debugger();   // = connector listening, no client attached
            }
            bench_eng_->execute(hot_loop_src);
        });

        benchmark("Hot Loop: session enabled, no breakpoints", [this]() {
            if (!bench_eng_) {
                bench_eng_ = jai::engine::make();
                bench_eng_->debugger().set_enabled(true);
            }
            bench_eng_->execute(hot_loop_src);
        });

        benchmark("Hot Loop: session enabled, bp in cold file", [this]() {
            if (!bench_eng_) {
                bench_eng_ = jai::engine::make();
                auto& dbg = bench_eng_->debugger();
                dbg.set_breakpoints("cold.jai", {50});   // no line in the script matches
                dbg.set_enabled(true);
            }
            bench_eng_->execute(hot_loop_src);
        });

        benchmark("Hot Loop: session enabled, bp line collides", [this]() {
            if (!bench_eng_) {
                bench_eng_ = jai::engine::make();
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
