#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <functional>
#include <memory>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

struct capture_probe { int v = 7; int value() const { return v; } };

// MV::Script::eval shape: a converted callback runs a nested execute() while the outer execute() is live.
class reentrant_callback_tests : public suite {
public:
	reentrant_callback_tests() : suite("Reentrant Callbacks") {}

	void forge_tests() override {
		test("nested_execute_in_converted_callback_keeps_outer_run", [this]() {
			auto eng = make_engine();
			engine* raw = eng.get();
			std::vector<std::string> host_trace;
			eng->add_function("host_eval", [raw](std::string src) -> bool {
				try { (void)raw->execute(src); return true; }
				catch (const std::exception&) { return false; }
			});
			eng->add_function("invoke", [&host_trace](script_value cb) {
				auto fn = convert_script_function<void()>(cb);
				host_trace.push_back("before");
				fn();
				host_trace.push_back("after");
			});
			eng->execute("var g_nested = 0;");
			auto r = eng->execute(R"(
				var outer = 1;
				invoke([&]() -> void {
					if (host_eval("g_nested = g_nested + 41;")) { outer = outer + 1; }
				});
				outer * 100 + g_nested;
			)");
			check_eq((int64_t)241, r.as_int(), "outer local and nested global both landed");
			check_eq((size_t)2, host_trace.size(), "C++ caller continued past the callback");
			check_eq((int64_t)7, eng->execute("3 + 4").as_int(), "engine usable afterwards");
		});

		test("nested_execute_failure_in_converted_callback_reports_nonempty_error", [this]() {
			auto eng = make_engine();
			engine* raw = eng.get();
			std::vector<std::string> reported;
			eng->set_script_error_handler([&reported](const std::string& msg) { reported.push_back(msg); });
			bool host_continued = false;
			eng->add_function("host_eval_unguarded", [raw](std::string src) { (void)raw->execute(src); });
			eng->add_function("invoke", [&host_continued](script_value cb) {
				convert_script_function<void()>(cb)();
				host_continued = true;
			});
			eng->execute("var g_boom = \"nested boom\"; var g_after = 0;");
			auto r = eng->execute(R"(
				invoke([]() -> void {
					host_eval_unguarded("throw g_boom;");
					g_after = 1;
				});
				g_after = g_after + 10;
				g_after;
			)");
			check_eq((int64_t)10, r.as_int(), "callback unwound at the failed nested execute; outer run continued");
			check_true(host_continued, "converted callback returned to its C++ caller");
			check_false(reported.empty(), "callback failure reached the error handler");
			for (const auto& msg : reported) {
				check_false(msg.empty(), "reported message is never empty");
			}
			check_eq((int64_t)7, eng->execute("3 + 4").as_int(), "engine usable afterwards");
		});

		test("callback_error_after_swallowed_nested_failure_reports_nonempty_error", [this]() {
			auto eng = make_engine();
			engine* raw = eng.get();
			std::vector<std::string> reported;
			eng->set_script_error_handler([&reported](const std::string& msg) { reported.push_back(msg); });
			bool host_saw_failure = false;
			eng->add_function("host_eval", [raw, &host_saw_failure](std::string src) -> bool {
				try { (void)raw->execute(src); return true; }
				catch (const std::exception& e) {
					host_saw_failure = std::string(e.what()).find("nested boom") != std::string::npos;
					return false;
				}
			});
			eng->add_function("host_throw", []() { throw std::runtime_error("callback boom"); });
			eng->add_function("invoke", [](script_value cb) { convert_script_function<void()>(cb)(); });
			eng->execute("var g_boom = \"nested boom\"; var g_after = 0;");
			auto r = eng->execute(R"(
				invoke([]() -> void {
					if (!host_eval("throw g_boom;")) { host_throw(); }
					g_after = 1;
				});
				g_after = g_after + 10;
				g_after;
			)");
			check_true(host_saw_failure, "host caught the nested failure with its text");
			check_eq((int64_t)10, r.as_int(), "callback unwound at its own error; outer run continued");
			check_false(reported.empty(), "callback error reached the error handler");
			for (const auto& msg : reported) {
				check_false(msg.empty(), "reported message is never empty after a scrubbed nested failure");
			}
			check_eq((int64_t)7, eng->execute("3 + 4").as_int(), "engine usable afterwards");
		});

		test("swallowed_nested_failure_leaves_callback_and_outer_run_intact", [this]() {
			auto eng = make_engine();
			engine* raw = eng.get();
			std::vector<std::string> reported;
			eng->set_script_error_handler([&reported](const std::string& msg) { reported.push_back(msg); });
			eng->add_function("host_eval", [raw](std::string src) -> bool {
				try { (void)raw->execute(src); return true; }
				catch (const std::exception&) { return false; }
			});
			eng->add_function("invoke", [](script_value cb) { convert_script_function<void()>(cb)(); });
			eng->execute("var g_boom = \"nested boom\";");
			auto r = eng->execute(R"(
				var outer = 1;
				invoke([&]() -> void {
					if (!host_eval("throw g_boom;")) { outer = outer + 5; }
					outer = outer + 1;
				});
				outer;
			)");
			check_eq((int64_t)7, r.as_int(), "callback ran to completion on the outer frame's local");
			check_true(reported.empty(), "host handled the nested failure; nothing to report");
			check_eq(std::string("none"),
				eng->execute("var got = \"none\"; try { invoke([]() -> void { host_eval(\"throw g_boom;\"); }); } catch (e) { got = \"\" + e; } got;").as<std::string>(),
				"an unrelated outer try/catch does not see the swallowed nested throw");
		});

		// Login/initialize.script shape: a [self] capture of a C++-bound object, fired later from a C++ signal.
		test("captured_cpp_bound_object_survives_into_converted_callback", [this]() {
			auto eng = make_engine();
			dynamic_binder<capture_probe> binder(*eng, "CaptureProbe");
			binder.method("value", &capture_probe::value);
			binder.build();
			auto obj = std::make_shared<capture_probe>();
			std::vector<std::function<void()>> stored;
			int64_t marked = -1;
			std::string err;
			eng->set_script_error_handler([&err](const std::string& m) { err = m; });
			eng->add_function("stash", [&stored](script_value cb) { stored.push_back(convert_script_function<void()>(cb)); });
			eng->add_function("mark", [&marked](script_int v) { marked = v; });
			eng->add_global("probeObj", eng->make_object(obj));
			eng->execute(R"(
				var init = [&](var& self) -> void { stash([self]() -> void { mark(self.value()); }); };
				init(probeObj);
			)");
			check_eq(size_t(1), stored.size(), "callback stashed");
			stored.front()();
			check_true(err.empty(), "callback error: " + err);
			check_eq((int64_t)7, marked, "captured C++-bound object is callable from the converted callback");
		});

		// Interface::initialize shape: C++ invokes a hook with T& and the script captures that parameter for later.
		test("captured_by_reference_callback_parameter_outlives_the_call", [this]() {
			auto eng = make_engine();
			dynamic_binder<capture_probe> binder(*eng, "CaptureProbe");
			binder.method("value", &capture_probe::value);
			binder.build();
			capture_probe live;
			std::vector<std::function<void()>> stored;
			int64_t marked = -1;
			std::string err;
			eng->set_script_error_handler([&err](const std::string& m) { err = m; });
			eng->add_function("stash", [&stored](script_value cb) { stored.push_back(convert_script_function<void()>(cb)); });
			eng->add_function("mark", [&marked](script_int v) { marked = v; });
			auto hook = eng->execute("[=](var& self) -> void { stash([self]() -> void { mark(self.value()); }); };");
			auto initialize = convert_script_function<void(capture_probe&)>(hook);
			initialize(live);
			check_true(err.empty(), "hook error: " + err);
			check_eq(size_t(1), stored.size(), "callback stashed");
			stored.front()();
			check_true(err.empty(), "callback error: " + err);
			check_eq((int64_t)7, marked, "captured by-reference parameter is still the live object");
		});

		// MV::Script::eval shape: a global 'self' exists when the hook is created and is nulled afterwards; the hook's own
		// var& parameter shadows it and the inner [self] capture must bind the parameter, not the (later null) global.
		test("captured_parameter_shadowing_a_nulled_global_binds_the_parameter", [this]() {
			auto eng = make_engine();
			dynamic_binder<capture_probe> binder(*eng, "CaptureProbe");
			binder.method("value", &capture_probe::value);
			binder.build();
			capture_probe live;
			auto other = std::make_shared<capture_probe>();
			other->v = 99;
			std::vector<std::function<void()>> stored;
			int64_t marked = -1;
			std::string err;
			eng->set_script_error_handler([&err](const std::string& m) { err = m; });
			eng->add_function("stash", [&stored](script_value cb) { stored.push_back(convert_script_function<void()>(cb)); });
			eng->add_function("mark", [&marked](script_int v) { marked = v; });
			eng->add_global("self", eng->make_object(other));
			auto hook = eng->execute("[=](var& self) -> void { stash([self]() -> void { mark(self.value()); }); };");
			eng->add_global("self", script_value(std::monostate{}, eng.get()));
			auto initialize = convert_script_function<void(capture_probe&)>(hook);
			initialize(live);
			check_true(err.empty(), "hook error: " + err);
			check_eq(size_t(1), stored.size(), "callback stashed");
			stored.front()();
			check_true(err.empty(), "callback error: " + err);
			check_eq((int64_t)7, marked, "inner capture bound the parameter (7), not the global (99/null)");
		});

		// Creature AI shape: a running script calls C++ which SYNCHRONOUSLY invokes a script callback; inside it, a
		// nested lambda explicitly captures both the callback's own capture and the callback's parameter.
		test("nested_external_callback_captures_its_parameter_and_own_capture", [this]() {
			auto eng = make_engine();
			dynamic_binder<capture_probe> binder(*eng, "CaptureProbe");
			binder.method("value", &capture_probe::value);
			binder.build();
			capture_probe live;
			std::vector<std::function<void()>> stored;
			int64_t marked = -1;
			std::string err;
			eng->set_script_error_handler([&err](const std::string& m) { err += m + " | "; });
			eng->add_function("stash", [&stored](script_value cb) { stored.push_back(convert_script_function<void()>(cb)); });
			eng->add_function("mark", [&marked](script_int v) { marked = v; });
			eng->add_function("with_probe", [&live](script_value cb) { convert_script_function<void(capture_probe&)>(cb)(live); });
			eng->execute(R"(
				var run = [=](int outer) -> void {
					with_probe([outer](var& probe) -> void { mark(outer); });
				};
				run(3);
			)");
			check_true(err.empty(), "stage A (callback's own capture read inside the external call): " + err);
			check_eq((int64_t)3, marked, "stage A value");
			err.clear(); marked = -1;
			eng->execute(R"(
				var run2 = [=](int outer) -> void {
					with_probe([outer](var& probe) -> void {
						stash([probe]() -> void { mark(probe.value()); });
					});
				};
				run2(3);
			)");
			check_true(err.empty(), "stage B (nested captures only the parameter): " + err);
			err.clear(); stored.clear(); marked = -1;
			eng->execute(R"(
				var run3 = [=](int outer) -> void {
					with_probe([outer](var& probe) -> void {
						stash([outer, probe]() -> void { mark(probe.value() * 10 + outer); });
					});
				};
				run3(3);
			)");
			check_true(err.empty(), "stage C (nested captures the callback's own capture): " + err);
			check_true(err.empty(), "errors: " + err);
			check_eq(size_t(1), stored.size(), "callback stashed");
			stored.front()();
			check_true(err.empty(), "errors: " + err);
			check_eq((int64_t)73, marked, "nested lambda saw the callback parameter (7) and the callback's capture (3)");
		});

		test("capture_variant_precreated_callback", [this]() {
			auto eng = make_engine();
			dynamic_binder<capture_probe> binder(*eng, "CaptureProbe");
			binder.method("value", &capture_probe::value);
			binder.build();
			capture_probe live;
			int64_t marked = -1;
			std::string err;
			eng->set_script_error_handler([&err](const std::string& m) { err += m + " | "; });
			eng->add_function("mark", [&marked](script_int v) { marked = v; });
			eng->add_function("with_probe", [&live](script_value cb) { convert_script_function<void(capture_probe&)>(cb)(live); });
			try { eng->execute(R"(
				var run = [=](int outer) -> void { var cb = [outer](var& probe) -> void { mark(outer); }; with_probe(cb); };
				run(3);
			)"); } catch (const std::exception& e) { err += std::string("execute threw: ") + e.what(); }
			check_true(err.empty(), "errors: " + err);
			check_eq((int64_t)3, marked, "value");
		});

		test("capture_variant_default_capture_inline", [this]() {
			auto eng = make_engine();
			dynamic_binder<capture_probe> binder(*eng, "CaptureProbe");
			binder.method("value", &capture_probe::value);
			binder.build();
			capture_probe live;
			int64_t marked = -1;
			std::string err;
			eng->set_script_error_handler([&err](const std::string& m) { err += m + " | "; });
			eng->add_function("mark", [&marked](script_int v) { marked = v; });
			eng->add_function("with_probe", [&live](script_value cb) { convert_script_function<void(capture_probe&)>(cb)(live); });
			try { eng->execute(R"(
				var run = [=](int outer) -> void { with_probe([=](var& probe) -> void { mark(outer); }); };
				run(3);
			)"); } catch (const std::exception& e) { err += std::string("execute threw: ") + e.what(); }
			check_true(err.empty(), "errors: " + err);
			check_eq((int64_t)3, marked, "value");
		});

		test("capture_variant_function_not_lambda", [this]() {
			auto eng = make_engine();
			dynamic_binder<capture_probe> binder(*eng, "CaptureProbe");
			binder.method("value", &capture_probe::value);
			binder.build();
			capture_probe live;
			int64_t marked = -1;
			std::string err;
			eng->set_script_error_handler([&err](const std::string& m) { err += m + " | "; });
			eng->add_function("mark", [&marked](script_int v) { marked = v; });
			eng->add_function("with_probe", [&live](script_value cb) { convert_script_function<void(capture_probe&)>(cb)(live); });
			try { eng->execute(R"(
				function run(int outer) -> void { with_probe([outer](var& probe) -> void { mark(outer); }); }
				run(3);
			)"); } catch (const std::exception& e) { err += std::string("execute threw: ") + e.what(); }
			check_true(err.empty(), "errors: " + err);
			check_eq((int64_t)3, marked, "value");
		});

		// Text-receiver shape fired from inside a script (Clickable::onAccept via press()).
		test("a_native_getters_exception_in_a_fused_push_argument_is_the_scripts_in_that_frame", [this]() {
			// a property reference stored in an array<var> element through the native array view; its getter throws on its
			// third (or first) call: dst.push(SRC[0]) is a call whose argument the fused push reads itself, so the exception
			// converts in the calling frame and the function's own try catches it, on both backends, proofs on and off
			// (Astra's acceptance case, 2026-09-14; the unfused call's argument zone already did this)
			struct source_t { engine* eng = nullptr; int throw_at = 3; bool armed = false; int calls = 0; };
			auto run = [&](bool vm, bool proofs, int throw_at) -> std::string {
				auto e = engine::make();
				if (vm) { e->set_backend(backend_type::vm); }
				if (!proofs) { e->type_proofs(false); }
				stdlib::register_all(*e);
				source_t source{ e.get(), throw_at };
				dynamic_binder<source_t>(*e, "Source").property("number", [](source_t& s) -> script_int {
					if (s.armed && ++s.calls == s.throw_at) { throw std::runtime_error("boom"); }
					return 1;
				}, nullptr).build();
				e->add_global_ref("box", source);
				(void)e->execute("var& r = box.number; var SRC = [0];");
				auto reference = e->get_global_environment()->get_ref("r");
				if (!reference) { return "no reference"; }
				auto src = e->get_variable("SRC");
				src.as_array()[0] = reference.value().get();
				source.armed = true; source.calls = 0;
				std::string out;
				try { out = e->execute("string probe() { array<int> dst = []; int i = 0; try { while (i < 4) { dst.push(SRC[0]); i++; } } catch (err) { return \"caught:\" + err + \"|\" + to_string(i) + \"|\" + to_string(dst.size()); } return \"ok:\" + to_string(dst.size()); } probe();").to_string(); }
				catch (const std::exception& ex) { out = std::string("throw:") + ex.what(); }
				return out + "|calls=" + std::to_string(source.calls);
			};
			for (bool proofs : { true, false }) {
				for (int at : { 3, 1 }) {
					const std::string i = run(false, proofs, at), v = run(true, proofs, at);
					check_eq(i, v, "the fused push's argument: both backends");
					const std::string want = at == 3 ? "caught:boom|2|2|calls=3" : "caught:boom|0|0|calls=1";
					check_eq(want, v, "caught by the pushing function's own try");
				}
			}
		});
		test("nested_execute_with_instance_vars_in_converted_callback", [this]() {
			auto eng = make_engine();
			engine* raw = eng.get();
			std::vector<std::string> nested_failures;
			eng->add_function("host_eval_with", [raw, &nested_failures](std::string src, script_int n) -> bool {
				instance_variables locals;
				locals["n"] = script_value(n, raw);
				try { (void)raw->execute(src, locals); return true; }
				catch (const std::exception& e) { nested_failures.push_back(e.what()); return false; }
			});
			eng->add_function("invoke", [](script_value cb) { convert_script_function<void()>(cb)(); });
			eng->execute("var g_sum = 0;");
			auto r = eng->execute(R"(
				var outer = 0;
				invoke([&]() -> void {
					host_eval_with("g_sum = g_sum + n;", 5);
					host_eval_with("g_sum = g_sum + n;", 6);
					outer = outer + 1;
				});
				outer * 100 + g_sum;
			)");
			check_true(nested_failures.empty(), nested_failures.empty() ? std::string() : "nested execute could not see its instance vars: " + nested_failures.front());
			check_eq((int64_t)111, r.as_int(), "instance-scoped nested runs landed and left the outer frame intact");
		});
	}
};

FOUNDRY_REGISTER(jai::foundry::tests::reentrant_callback_tests);

} // namespace jai::foundry::tests
