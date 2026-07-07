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

// The interpreter is the only backend with the statement hook (the vm gets it in phase
// 5), so these tests construct an interpreter engine directly rather than via the
// backend-aware make_engine() — they must not run on the vm under --backend=vm.
class debugger_tests : public suite {
public:
    debugger_tests() : suite("Debugger") {}

    void forge_tests() override {
        test("breakpoint_inspect_step_resume", [this]() {
            auto eng = jai::engine::make();
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
    }
};

} // namespace jai::foundry::tests

using debugger_tests = jai::foundry::tests::debugger_tests;
FOUNDRY_REGISTER(debugger_tests)
