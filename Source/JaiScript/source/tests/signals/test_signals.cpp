#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/signals/signal.hpp>
#include <jaiscript/signals/signal_impl.hpp>
#include <jaiscript/core/engine.hpp>
#include <vector>
#include <stdexcept>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class signal_tests : public suite {
public:
    signal_tests() : suite("Signals") {}

    void forge_tests() override {
        test("receiver_cpp_callback", [this]() {
            int called = 0;
            int received_value = 0;

            auto recv = receiver<void(int)>::make([&](int x) {
                called++;
                received_value = x;
            });

            recv->notify(42);
            check_eq(called, 1, "Receiver called once");
            check_eq(received_value, 42, "Receiver received correct value");

            recv->notify(100);
            check_eq(called, 2, "Receiver called twice");
            check_eq(received_value, 100, "Receiver received updated value");
        });

        test("receiver_block_unblock", [this]() {
            int called = 0;
            auto recv = receiver<void()>::make([&]() { called++; });

            recv->notify();
            check_eq(called, 1, "Unblocked receiver fires");

            recv->block();
            recv->notify();
            check_eq(called, 1, "Blocked receiver doesn't fire");

            recv->unblock();
            recv->notify();
            check_eq(called, 2, "Unblocked receiver fires again");
        });

        test("signal_emitter_connect_emit", [this]() {
            signal_emitter<void(int, int)> sig;

            int sum = 0;
            auto recv = sig.connect([&](int a, int b) {
                sum = a + b;
            });

            sig.emit(10, 20);
            check_eq(sum, 30, "Signal emitted to receiver");
        });

        // A throwing slot must not permanently wedge the emitter: the RAII depth guard restores
        // depth (so later disconnects take effect immediately) and drains even when emit throws.
        test("emit_not_wedged_after_slot_throws", [this]() {
            signal_emitter<void()> sig;

            int fired = 0;
            auto bad = sig.connect([]() { throw std::runtime_error("boom"); });
            auto good = sig.connect([&fired]() { fired++; });  // hold the receiver alive

            try { sig.emit(); } catch (...) {}  // slot throws; emitter must stay usable

            sig.disconnect(bad);  // must take effect now, not be stuck in the deferred queue

            fired = 0;
            sig.emit();  // would re-throw (bad still connected) if the emitter were wedged
            check_eq(1, fired, "remaining receiver fires after a prior slot threw");
        });

        // A slot that re-emits the SAME signal (reentrancy) must not corrupt iteration via a
        // bool in_call_; the depth counter defers cleanup to the outermost emit only.
        test("reentrant_emit_is_safe", [this]() {
            signal_emitter<void(int)> sig;
            int total = 0;
            auto recv = sig.connect([&](int x) {
                total += x;
                if (x > 0) sig.emit(x - 1);
            });
            sig.emit(3);
            check_eq(3 + 2 + 1 + 0, total, "reentrant emits all fire without corruption");
        });

        test("signal_emitter_multiple_receivers", [this]() {
            signal_emitter<void(int)> sig;

            std::vector<int> results;
            auto recv1 = sig.connect([&](int x) { results.push_back(x * 1); });
            auto recv2 = sig.connect([&](int x) { results.push_back(x * 2); });
            auto recv3 = sig.connect([&](int x) { results.push_back(x * 3); });

            sig.emit(10);
            check_eq(results.size(), 3u, "All three receivers called");
            // Results may be in any order due to set ordering
            int total = 0;
            for (int r : results) total += r;
            check_eq(total, 60, "All receivers processed correctly"); // 10 + 20 + 30
        });

        test("signal_emitter_disconnect", [this]() {
            signal_emitter<void()> sig;

            int count1 = 0, count2 = 0;
            auto recv1 = sig.connect([&]() { count1++; });
            auto recv2 = sig.connect([&]() { count2++; });

            sig.emit();
            check_eq(count1, 1, "First receiver called");
            check_eq(count2, 1, "Second receiver called");

            sig.disconnect(recv1);
            sig.emit();
            check_eq(count1, 1, "Disconnected receiver not called");
            check_eq(count2, 2, "Remaining receiver still called");
        });

        test("signal_emitter_owned_connections", [this]() {
            signal_emitter<void()> sig;

            int count = 0;
            sig.connect("my_handler", [&]() { count++; });

            check_true(sig.connected("my_handler"), "Owned connection exists");

            sig.emit();
            check_eq(count, 1, "Owned connection fires");

            auto conn = sig.connection("my_handler");
            check_true(conn != nullptr, "Can retrieve owned connection");

            sig.disconnect("my_handler");
            check_false(sig.connected("my_handler"), "Owned connection removed");

            sig.emit();
            check_eq(count, 1, "Disconnected owned connection doesn't fire");
        });

        test("signal_emitter_block_unblock", [this]() {
            signal_emitter<void()> sig;

            int count = 0;
            auto recv = sig.connect([&]() { count++; });

            sig.emit();
            check_eq(count, 1, "Unblocked signal fires");

            sig.block();
            sig.emit();
            check_eq(count, 1, "Blocked signal doesn't fire");

            bool was_called = sig.unblock();
            check_true(was_called, "Signal reports it was called while blocked");

            sig.emit();
            check_eq(count, 2, "Unblocked signal fires again");
        });

        test("signal_emitter_weak_reference", [this]() {
            signal_emitter<void()> sig;

            int count = 0;
            {
                auto recv = sig.connect([&]() { count++; });
                sig.emit();
                check_eq(count, 1, "Receiver fires while alive");
            }
            // recv is out of scope, weak_ptr should expire

            sig.emit();
            check_eq(count, 1, "Dead receiver doesn't fire");

            // Cull should remove dead observers
            size_t remaining = sig.cull_dead_observers();
            check_eq(remaining, 0u, "No observers remain after cull");
        });

        test("public_signal_connect_only", [this]() {
            // Simulate the Button pattern
            struct MockButton {
                signal_emitter<void(int, int)> on_click_;
                signal<void(int, int)> on_click{on_click_};

                void click(int x, int y) {
                    on_click_.emit(x, y);
                }
            };

            MockButton btn;
            int click_x = 0, click_y = 0;

            // Connect via public signal interface
            auto recv = btn.on_click.connect([&](int x, int y) {
                click_x = x;
                click_y = y;
            });

            btn.click(100, 200);
            check_eq(click_x, 100, "Click x coordinate received");
            check_eq(click_y, 200, "Click y coordinate received");
        });

        test("signal_script_callback", [this]() {
            auto eng = jai::foundry::make_engine();

            int result = 0;
            eng->add_global_ref("result", result);

            signal_emitter<void(int)> sig;
            sig.script_engine(eng.get());
            sig.parameter_names({"value"});

            auto recv = sig.connect("result = value * 2;");

            sig.emit(21);
            check_eq(result, 42, "Script callback executed correctly");
        });

        // jaidoom netplay finding (d): "nested-element pushes from signal receivers
        // don't land" — the mutations always landed; a receiver-body ERROR was being
        // swallowed by call_script's silent catch, which is indistinguishable from a
        // lost mutation at the consumer. Pins: pushes from BOTH receiver flavors
        // mutate live globals, and receiver errors surface via report_script_error.
        test("signal_receiver_container_pushes_land", [this]() {
            auto eng = jai::foundry::make_engine();
            eng->execute(R"(
                var G = {"lists": [[]], "flat": []};
                function on_evt(int v) -> void {
                    G["flat"].push(v);
                    G["lists"][0].push(v * 10);
                }
            )");
            signal_emitter<void(int)> sig;
            sig.script_engine(eng.get());
            sig.parameter_names({"v"});
            auto r1 = sig.connect("fn_recv", eng->get_variable("on_evt"));
            auto r2 = sig.connect("G[\"lists\"][0].push(v + 1);");
            sig.emit(7);
            check_eq((int64_t)1, eng->execute("G[\"flat\"].size()").as_int(), "flat push landed");
            check_eq((int64_t)2, eng->execute("G[\"lists\"][0].size()").as_int(),
                "nested pushes landed from both receiver flavors");
        });

        test("signal_receiver_errors_surface_not_swallowed", [this]() {
            auto eng = jai::foundry::make_engine();
            std::vector<std::string> reported;
            eng->set_script_error_handler([&](const std::string& msg) { reported.push_back(msg); });
            signal_emitter<void(int)> sig;
            sig.script_engine(eng.get());
            sig.parameter_names({"v"});
            auto recv = sig.connect("undefined_name_xyz.push(v);");
            sig.emit(3);
            check_true(!reported.empty(), "receiver-body error routes to report_script_error");
            check_eq((int64_t)3, eng->execute("1 + 2").as_int(), "engine stays usable after the reported error");
        });

        test("signal_disconnect_during_emit", [this]() {
            signal_emitter<void()> sig;

            int count = 0;
            std::shared_ptr<receiver<void()>> recv_to_disconnect;

            auto recv1 = sig.connect([&]() {
                count++;
                // Disconnect recv2 during emission
                if (recv_to_disconnect) {
                    sig.disconnect(recv_to_disconnect);
                }
            });

            auto recv2 = sig.connect([&]() {
                count += 10;
            });
            recv_to_disconnect = recv2;

            sig.emit();
            // Both should fire on first emit (disconnect is queued)
            check_true(count >= 1, "At least first receiver fired");

            count = 0;
            sig.emit();
            // Only recv1 should fire now
            check_eq(count, 1, "Only first receiver fires after disconnect");
        });

        test("signal_clear", [this]() {
            signal_emitter<void()> sig;

            int count = 0;
            auto recv1 = sig.connect([&]() { count++; });
            auto recv2 = sig.connect([&]() { count++; });
            sig.connect("handler1", [&]() { count++; });

            sig.emit();
            check_eq(count, 3, "All receivers fire");

            sig.clear();
            count = 0;
            sig.emit();
            check_eq(count, 0, "No receivers fire after clear");
        });

        test("connect_oneshot", [this]() {
            signal_emitter<void(int)> sig;

            int call_count = 0;
            int last_value = 0;
            auto recv = sig.connect_oneshot([&](int x) {
                call_count++;
                last_value = x;
            });

            sig.emit(10);
            check_eq(call_count, 1, "One-shot called once");
            check_eq(last_value, 10, "One-shot received value");

            sig.emit(20);
            check_eq(call_count, 1, "One-shot not called again");
            check_eq(last_value, 10, "Value unchanged after second emit");
        });

        test("connect_oneshot_multiple", [this]() {
            signal_emitter<void()> sig;

            int regular_count = 0;
            int oneshot_count = 0;

            auto regular = sig.connect([&]() { regular_count++; });
            auto oneshot = sig.connect_oneshot([&]() { oneshot_count++; });

            sig.emit();
            check_eq(regular_count, 1, "Regular receiver called");
            check_eq(oneshot_count, 1, "One-shot receiver called");

            sig.emit();
            check_eq(regular_count, 2, "Regular receiver called again");
            check_eq(oneshot_count, 1, "One-shot not called again");

            sig.emit();
            check_eq(regular_count, 3, "Regular receiver keeps firing");
            check_eq(oneshot_count, 1, "One-shot still only once");
        });

        test("emit_while_short_circuit", [this]() {
            signal_emitter<bool(int)> sig;

            std::vector<int> called_with;

            auto recv1 = sig.connect([&](int x) -> bool {
                called_with.push_back(x);
                return true;  // Continue
            });
            auto recv2 = sig.connect([&](int x) -> bool {
                called_with.push_back(x * 10);
                return false;  // Stop propagation
            });
            auto recv3 = sig.connect([&](int x) -> bool {
                called_with.push_back(x * 100);
                return true;
            });

            bool result = sig.emit_while(5);
            check_false(result, "emit_while returns false when short-circuited");
            check_eq(called_with.size(), 2u, "Only first two receivers called");
            check_eq(called_with[0], 5, "First receiver got value");
            check_eq(called_with[1], 50, "Second receiver got value");
        });

        test("emit_while_no_short_circuit", [this]() {
            signal_emitter<bool()> sig;

            int count = 0;
            auto recv1 = sig.connect([&]() -> bool { count++; return true; });
            auto recv2 = sig.connect([&]() -> bool { count++; return true; });
            auto recv3 = sig.connect([&]() -> bool { count++; return true; });

            bool result = sig.emit_while();
            check_true(result, "emit_while returns true when all continue");
            check_eq(count, 3, "All receivers called");
        });

        test("observer_count", [this]() {
            signal_emitter<void()> sig;

            check_eq(sig.observer_count(), 0u, "No observers initially");

            auto recv1 = sig.connect([&]() {});
            check_eq(sig.observer_count(), 1u, "One observer after connect");

            auto recv2 = sig.connect([&]() {});
            check_eq(sig.observer_count(), 2u, "Two observers");

            sig.disconnect(recv1);
            check_eq(sig.observer_count(), 1u, "One observer after disconnect");

            recv2.reset();  // Let it expire
            sig.cull_dead_observers();
            check_eq(sig.observer_count(), 0u, "No observers after expiry");
        });

        test("receiver_connected", [this]() {
            signal_emitter<void()> sig;

            auto recv = sig.connect([&]() {});
            check_true(recv->connected(), "Receiver is connected after connect");

            sig.disconnect(recv);
            check_false(recv->connected(), "Receiver not connected after disconnect");
        });

        test("receiver_connected_signal_clear", [this]() {
            signal_emitter<void()> sig;

            auto recv1 = sig.connect([&]() {});
            auto recv2 = sig.connect([&]() {});

            check_true(recv1->connected(), "recv1 connected");
            check_true(recv2->connected(), "recv2 connected");

            sig.clear();

            check_false(recv1->connected(), "recv1 not connected after clear");
            check_false(recv2->connected(), "recv2 not connected after clear");
        });

        test("insertion_order_preserved", [this]() {
            signal_emitter<void()> sig;

            std::vector<int> order;
            auto recv1 = sig.connect([&]() { order.push_back(1); });
            auto recv2 = sig.connect([&]() { order.push_back(2); });
            auto recv3 = sig.connect([&]() { order.push_back(3); });

            sig.emit();

            check_eq(order.size(), 3u, "All receivers called");
            check_eq(order[0], 1, "First connected fires first");
            check_eq(order[1], 2, "Second connected fires second");
            check_eq(order[2], 3, "Third connected fires third");
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::signal_tests);
