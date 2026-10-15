#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/signals/signal.hpp>
#include <jaiscript/signals/signal_impl.hpp>
#include <jaiscript/signals/signal_binding.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/signals/signal_serialization.hpp>
#include <jaiscript/serialization/convenience.hpp>
#include <vector>
#include <stdexcept>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class emit_board {
public:
    jai::signal_emitter<void(int)> onTickedSignal;
    jai::signal<void(int)> onTicked{ onTickedSignal };
};

struct signal_box {
    signal_emitter<void(int)> sig;
    template<typename Archive> void serialize(Archive& ar) { ar("sig", sig); }
};

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
        // mutate live globals, and receiver errors throw out of the emit.
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

        // A script receiver's failure throws out of the emit with the text a direct call gives (a text receiver: execute of
        // its text; a function receiver: its converted std::function); the receivers after it do not run
        test("script_receiver_errors_throw_out_of_the_emit", [this]() {
            auto eng = jai::foundry::make_engine();
            jai::stdlib::register_all(*eng);
            std::vector<std::string> reported;
            eng->set_script_error_handler([&](const std::string& msg) { reported.push_back(msg); });
            eng->execute(R"(
                int seen = 0;
                var divides = [](int v) -> void { int z = v / 0; seen += 100; };
                var counts = [](int v) -> void { seen += v; };
            )");
            auto thrown_text = [](auto&& fn) {
                try { fn(); } catch (const jai::runtime_error& e) { return std::string(e.what()); }
                return std::string("<no throw>");
            };

            signal_emitter<void(int)> text_sig;
            text_sig.script_engine(eng.get());
            text_sig.parameter_names({"v"});
            auto t1 = text_sig.connect("a_text", std::string("int z = v / 0; seen += 100;"));
            auto t2 = text_sig.connect("b_counts", eng->get_variable("counts"));
            instance_variables locals;
            locals["v"] = eng->make_value(int64_t{3});
            const std::string direct_text = thrown_text([&]() { eng->execute("int z = v / 0; seen += 100;", locals); });
            const std::string text_receiver = thrown_text([&]() { text_sig.emit(3); });
            check_true(direct_text.find("Division by zero") != std::string::npos, direct_text);
            check_eq(direct_text, text_receiver, "a text receiver throws what executing its text throws");
            check_eq((int64_t)0, eng->get_variable("seen").as_int(), "the receiver after the failing one did not run");

            signal_emitter<void(int)> fn_sig;
            auto f1 = fn_sig.connect("a_divides", eng->get_variable("divides"));
            auto f2 = fn_sig.connect("b_counts", eng->get_variable("counts"));
            auto host_fn = convert_script_function<void(int)>(eng->get_variable("divides"));
            const std::string direct_fn = thrown_text([&]() { host_fn(3); });
            const std::string fn_receiver = thrown_text([&]() { fn_sig.emit(3); });
            check_true(direct_fn.find("Division by zero") != std::string::npos, direct_fn);
            check_eq(direct_fn, fn_receiver, "a function receiver throws what its converted std::function throws");
            check_eq((int64_t)0, eng->get_variable("seen").as_int());

            signal_emitter<void(int)> throw_sig;
            throw_sig.script_engine(eng.get());
            throw_sig.parameter_names({"v"});
            auto th = throw_sig.connect("a_throws", std::string("throw \"boom \" + to_string(v);"));
            check_eq(std::string("boom 4"), thrown_text([&]() { throw_sig.emit(4); }), "a script throw arrives with its text");

            signal_emitter<void(int)> once_sig;
            auto once = once_sig.connect_oneshot([](int) { throw jai::runtime_error("once"); });
            check_eq(std::string("once"), thrown_text([&]() { once_sig.emit(1); }));
            check_eq(std::string("<no throw>"), thrown_text([&]() { once_sig.emit(2); }), "a one-shot that threw has fired");

            check_true(reported.empty(), "nothing went to the error handler");
            check_eq((int64_t)3, eng->execute("1 + 2").as_int(), "engine stays usable");
        });

        // A load rebuilds an unnamed text receiver with nothing outside to hold it: the emitter holds it, as it holds a named one
        test("unnamed_text_receivers_survive_a_load", [this]() {
            auto eng = jai::foundry::make_engine();
            eng->execute("int hits = 0;");
            signal_box box;
            box.sig.script_engine(eng.get());
            box.sig.parameter_names({"v"});
            auto held = box.sig.connect(std::string("hits += v;"));
            auto named = box.sig.connect("named", std::string("hits += 100;"));
            auto save = [&](const signal_box& b) {
                serialization::json_archive_writer w(2, eng.get());
                w.begin_object("", 1);
                w.serialize_object_content(b);
                w.end_object();
                return w.str();
            };
            const std::string json = save(box);
            signal_box loaded;
            {
                serialization::json_archive_reader r(json, eng.get());
                std::string type_name;
                uint32_t version = 0;
                r.begin_object(type_name, version);
                r(loaded);
                r.end_object();
            }
            loaded.sig.emit(2);
            check_eq((int64_t)102, eng->get_variable("hits").as_int(), "the unnamed receiver and the named one both ran after the load");
            check_eq(json, save(loaded), "the loaded signal saves what it loaded");
            loaded.sig.clear();
            loaded.sig.emit(5);
            check_eq((int64_t)102, eng->get_variable("hits").as_int(), "clear() drops the loaded receivers");
        });

        test("script_receiver_errors_reach_the_script_that_fired_the_signal", [this]() {
            auto eng = jai::foundry::make_engine();
            jai::stdlib::register_all(*eng);
            signal_emitter<void(int)> text_sig;
            text_sig.script_engine(eng.get());
            text_sig.parameter_names({"v"});
            signal_emitter<void(int)> fn_sig;
            signal_emitter<void(int)> outer;
            signal_emitter<void(int)> inner;
            eng->add_function("fire_text", [&](script_int v) { text_sig.emit(static_cast<int>(v)); });
            eng->add_function("fire_fn", [&](script_int v) { fn_sig.emit(static_cast<int>(v)); });
            eng->add_function("fire_outer", [&](script_int v) { outer.emit(static_cast<int>(v)); });
            eng->add_function("fire_inner", [&](script_int v) { inner.emit(static_cast<int>(v)); });
            eng->execute(R"(
                int seen = 0;
                var lam = [](int v) -> void { throw "lam " + to_string(v); };
                var nests = [](int v) -> void { fire_inner(v); seen += 1000; };
                var counts = [](int v) -> void { seen += v; };
            )");
            auto t1 = text_sig.connect("a_text", std::string("throw \"text \" + to_string(v);"));
            auto t2 = text_sig.connect("b_counts", eng->get_variable("counts"));
            auto f1 = fn_sig.connect("a_lam", eng->get_variable("lam"));
            auto f2 = fn_sig.connect("b_counts", eng->get_variable("counts"));
            auto i1 = inner.connect("a_lam", eng->get_variable("lam"));
            auto o1 = outer.connect("a_nests", eng->get_variable("nests"));
            auto o2 = outer.connect("b_counts", eng->get_variable("counts"));
            auto r = eng->execute(R"(
                string got = "";
                try { fire_text(4); got += "missed"; } catch (e) { got += e.message; }
                got += "|";
                try { fire_fn(5); got += "missed"; } catch (e) { got += e.message; }
                got += "|";
                try { fire_outer(6); got += "missed"; } catch (e) { got += e.message; }
                got + "|" + to_string(seen);
            )");
            check_eq(std::string("text 4|lam 5|lam 6|0"), r.as_string(),
                "each failure reached the script's catch, the nested one outward through both emits, and no later receiver ran");
            bool threw = false;
            try { outer.emit(7); } catch (const jai::runtime_error& e) { threw = std::string(e.what()) == "lam 7"; }
            check_true(threw, "a nested emit's failure leaves the outer C++ emit too");
            check_eq((int64_t)0, eng->get_variable("seen").as_int());
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

        // Emission rights ride the registered TYPE: signal_emitter<Sig> carries
        // emit(args...), signal<Sig> stays the non-emittable consumer view — which
        // member a class exposes decides what script may do with it.
        test("script_emit_through_bound_emitter_fires_script_receiver", [this]() {
            auto eng = jai::foundry::make_engine();
            bind_signal_type<void(int)>(*eng, "SignalInt");
            dynamic_binder<emit_board>(*eng, "EmitBoard")
                .constructor<>()
                .property("onTicked", &emit_board::onTicked)
                .property("onTickedEmitter", &emit_board::onTickedSignal)
                .build();
            eng->execute("var got = -1; var s = new EmitBoard();");
            eng->execute("s.onTicked.connect(\"t\", [](var v) { got = v; });");
            eng->execute("s.onTickedEmitter.emit(23);");
            check_eq((int64_t)23, eng->execute("got").as_int());
        });

        test("script_emit_through_bound_emitter_fires_cpp_receiver", [this]() {
            auto eng = jai::foundry::make_engine();
            bind_signal_type<void(int)>(*eng, "SignalInt");
            dynamic_binder<emit_board>(*eng, "EmitBoard")
                .constructor<>()
                .property("onTickedEmitter", &emit_board::onTickedSignal)
                .build();
            auto board = std::make_shared<emit_board>();
            int got = -1;
            auto rcv = board->onTicked.connect([&got](int v) { got = v; });
            eng->add_global("s", eng->make_object(board));
            eng->execute("s.onTickedEmitter.emit(31);");
            check_eq(31, got);
        });

        test("signal_view_stays_non_emittable", [this]() {
            auto eng = jai::foundry::make_engine();
            bind_signal_type<void(int)>(*eng, "SignalInt");
            dynamic_binder<emit_board>(*eng, "EmitBoard")
                .constructor<>()
                .property("onTicked", &emit_board::onTicked)
                .build();
            eng->execute("var s = new EmitBoard();");
            check_throws([&]() { eng->execute("s.onTicked.emit(1);"); },
                "the consumer view grants no emission rights");
        });

        // A receiver may destroy the emitter: the emit stops there and returns normally (the emit state outlives the emitter)
        test("a_receiver_destroying_the_emitter_ends_the_emit", [this]() {
            auto board = std::make_unique<emit_board>();
            int later = 0;
            auto kills = board->onTicked.connect([&](int) { board.reset(); });
            auto after = board->onTicked.connect([&](int) { ++later; });
            board->onTickedSignal.emit(1);
            check_true(board == nullptr, "the receiver destroyed the emitter");
            check_eq(0, later, "the receivers after it do not run");
            check_false(kills->connected(), "its receivers are disconnected once the emit unwinds");

            auto nested = std::make_unique<emit_board>();
            int depth = 0;
            auto reenter = nested->onTicked.connect([&](int v) {
                ++depth;
                if (v > 0) { nested->onTickedSignal.emit(v - 1); } else { nested.reset(); }
            });
            nested->onTickedSignal.emit(3);
            check_eq(4, depth, "a reentrant emit destroying the emitter unwinds every level");

            signal_emitter<bool(int)>* gate = new signal_emitter<bool(int)>();
            auto stop = gate->connect([&](int) { delete gate; gate = nullptr; return true; });
            auto never = gate->connect([&](int) { ++later; return true; });
            check_true(gate->emit_while(1), "emit_while returns the destroying receiver's verdict");
            check_eq(0, later);

            auto blocked = std::make_unique<emit_board>();
            blocked->onTickedSignal.set_blocked_callback([&](int) { blocked.reset(); });
            blocked->onTickedSignal.block();
            blocked->onTickedSignal.emit(1);
            check_true(blocked == nullptr, "the blocked callback may destroy the emitter too");
        });

        test("a_script_receiver_dropping_the_last_reference_ends_the_emit", [this]() {
            auto eng = jai::foundry::make_engine();
            bind_signal_type<void(int)>(*eng, "SignalInt");
            dynamic_binder<emit_board>(*eng, "EmitBoard")
                .constructor<>()
                .property("onTicked", &emit_board::onTicked)
                .property("onTickedEmitter", &emit_board::onTickedSignal)
                .build();
            auto board = std::make_shared<emit_board>();
            std::weak_ptr<emit_board> watch = board;
            eng->add_global("s", eng->make_object(board));
            eng->execute("int seen = 0; s.onTicked.connect(\"drop\", [](var v) { s = null; }); s.onTicked.connect(\"count\", [](var v) { seen += 1; });");
            emit_board* raw = board.get();
            board.reset();
            raw->onTickedSignal.emit(5);
            check_true(watch.expired(), "the script dropped the last reference mid-emit");
            check_eq((int64_t)0, eng->execute("seen").as_int());
            check_eq((int64_t)3, eng->execute("1 + 2").as_int(), "engine stays usable");
        });

        test("a_oneshot_fires_once_when_it_reemits", [this]() {
            signal_emitter<void(int)> sig;
            int fired = 0;
            int plain = 0;
            auto once = sig.connect_oneshot([&](int v) { ++fired; if (v < 5) { sig.emit(v + 1); } });
            auto every = sig.connect([&](int) { ++plain; });
            sig.emit(1);
            check_eq(1, fired, "the nested emit skips the one-shot that is firing");
            check_eq(2, plain, "the nested emit reaches the other receivers");
            sig.emit(1);
            check_eq(1, fired);
            check_false(once->connected());
        });

        // A fired one-shot leaves while dropped receivers sit before it in the list (the cleanup erased past its own index)
        test("a_oneshot_after_dropped_receivers_leaves_cleanly", [this]() {
            signal_emitter<void(int)> sig;
            int fired = 0;
            auto first = sig.connect([](int) {});
            auto second = sig.connect([](int) {});
            auto once = sig.connect_oneshot([&](int) { ++fired; });
            auto last = sig.connect([](int) {});
            first.reset();
            second.reset();
            sig.emit(1);
            sig.emit(1);
            check_eq(1, fired);
            check_eq((size_t)1, sig.observer_count());
        });

        test("a_oneshot_an_emit_while_stopped_short_of_still_fires", [this]() {
            signal_emitter<bool(int)> sig;
            int fired = 0;
            auto stop = sig.connect([](int) { return false; });
            auto once = sig.connect_oneshot([&](int) { ++fired; return true; });
            check_false(sig.emit_while(1));
            check_eq(0, fired);
            check_true(once->connected(), "it has not fired, so it stays");
            stop.reset();
            check_true(sig.emit_while(1));
            check_eq(1, fired);
            check_true(sig.emit_while(1));
            check_eq(1, fired);
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::signal_tests);
