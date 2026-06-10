// Regression tests for bugs found during the 2026-06 release review.
// Each test names the review finding it pins. Phase 1 batch: typed-null values,
// environment-pool corruption, closure slot collisions, throw fallthrough,
// switch scope leaks, and compound-assignment overflow.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/signals/signal.hpp>
#include <jaiscript/signals/signal_binding.hpp>
#include <map>
#include <memory>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Mirrors the game's scripted-entity glue: assignable std::function hooks,
// a signal member exposed as a read-only property, and string-keyed state.
struct scripted_thing {
	jai::signal_emitter<void(int)> pingEmitter;
	jai::signal<void(int)> onPing{pingEmitter};
	std::function<void(int)> hook;
	std::map<std::string, jai::script_value> vars;
};

class release_review_regression_tests : public suite {
public:
	release_review_regression_tests() : suite("Release Review Regressions") {}

	void forge_tests() override {
		// ---- #20 typed-null: `int x;` must not crash or mis-compare ----
		test("typed_null_equality_no_crash", [this]() {
			auto e = engine::make();
			check(e->execute("int a; int b; a == b;").as_bool());  // both null -> equal
			check_eq((int64_t)4, e->execute("2 + 2").as_int());    // engine stays usable
		});
		test("typed_null_equality_symmetric", [this]() {
			auto e = engine::make();
			check_false(e->execute("int a; a == 5;").as_bool());
			check_false(e->execute("int a; 5 == a;").as_bool());
			check(e->execute("int a; a != 5;").as_bool());
		});
		test("typed_null_string_compare_no_crash", [this]() {
			auto e = engine::make();
			check_false(e->execute("string s; \"hi\" == s;").as_bool());
			check_false(e->execute("string s; s == \"hi\";").as_bool());
		});

		// ---- #10 env pool: static method call + block must not corrupt the pool ----
		test("static_method_then_block_no_hang", [this]() {
			auto e = engine::make();
			auto r = e->execute(R"(
				class S { static int get() { return 42; } }
				int f() {
					int b = S::get();
					{ int c = 1; }
					return b;
				}
				f();
			)");
			check_eq((int64_t)42, r.as_int());
		});

		// ---- #11 closures: outer local colliding with the lambda's own slot range ----
		test("lambda_captures_colliding_outer_param_inline", [this]() {
			auto e = engine::make();
			auto r = e->execute(R"(
				int make2(int base) {
					auto g = [=](int x) { return x + base; };
					return g(1);
				}
				make2(10);
			)");
			check_eq((int64_t)11, r.as_int());
		});
		test("lambda_captures_colliding_outer_param_escaping", [this]() {
			auto e = engine::make();
			auto r = e->execute(R"(
				auto make_adder(int base) -> auto {
					return [=](int x) { return x + base; };
				}
				auto a1 = make_adder(10);
				auto a2 = make_adder(100);
				a1(1) * 1000 + a2(1);
			)");
			check_eq((int64_t)11101, r.as_int());  // 11 * 1000 + 101
		});

		// ---- #12 statements after a top-level throw in a function body must not run ----
		test("no_execution_after_throw_in_function_body", [this]() {
			auto e = engine::make();
			auto r = e->execute(R"(
				var ran = 0;
				int f() {
					throw "err";
					ran = 1;
					return 1;
				}
				var caught = 0;
				try { f(); } catch (ex) { caught = 1; }
				ran * 10 + caught;
			)");
			check_eq((int64_t)1, r.as_int());  // caught, but `ran = 1` never executed
		});

		// ---- #13 switch case scopes: destructors run at case exit, pool is released ----
		// (declarations directly in a case body are a separate parser gap — finding #4 —
		// so the case-local lives in a block; the dtor must still fire before `log` reads)
		test("switch_case_scope_runs_destructors", [this]() {
			auto e = engine::make();
			auto r = e->execute(R"(
				var log = "";
				class D { ~D() { log += "x"; } }
				switch (1) {
					case 1: {
						D d = D();
						log += "b";
					}
				}
				log;
			)");
			check_eq(std::string("bx"), r.as_string());  // dtor fired when the case scope closed
		});
		test("switch_in_loop_does_not_leak", [this]() {
			auto e = engine::make();
			auto r = e->execute(R"(
				var total = 0;
				for (int i = 0; i < 10000; ++i) {
					switch (i % 3) {
						case 0: total += 1;
						case 1: { int t = 2; total += t; }
						default: total += 3;
					}
				}
				total;
			)");
			check(r.as_int() > 0);
		});

		// ---- #15 compound assignment honours the checked-overflow policy ----
		test("compound_add_overflow_raises", [this]() {
			auto e = engine::make();
			if (!e->throw_on_overflow()) { return; }  // wrap build: policy off by design
			check_throws([&]() { e->execute("var x = 9223372036854775807; x += 1;"); });
		});
		test("compound_sub_overflow_raises", [this]() {
			auto e = engine::make();
			if (!e->throw_on_overflow()) { return; }
			check_throws([&]() { e->execute("var x = -9223372036854775807 - 1; x -= 1;"); });
		});
		test("compound_mul_overflow_raises", [this]() {
			auto e = engine::make();
			if (!e->throw_on_overflow()) { return; }
			check_throws([&]() { e->execute("var x = 9223372036854775807; x *= 2;"); });
		});
		test("compound_div_min_by_minus_one_no_crash", [this]() {
			auto e = engine::make();
			if (!e->throw_on_overflow()) { return; }
			check_throws([&]() { e->execute("var y = -9223372036854775807 - 1; y /= -1;"); });
			check_eq((int64_t)4, e->execute("2 + 2").as_int());  // host survived
		});
		test("compound_add_overflow_identifier_rhs", [this]() {
			auto e = engine::make();
			if (!e->throw_on_overflow()) { return; }
			check_throws([&]() { e->execute("var x = 9223372036854775807; var one = 1; x += one;"); });
		});
		test("compound_ops_still_correct", [this]() {
			auto e = engine::make();
			auto r = e->execute("var a = 5; a += 2; a -= 1; a *= 3; a /= 2; a;");
			check_eq((int64_t)9, r.as_int());
		});

		// ---- #56 signal emit must not move-consume rvalue args before later receivers ----
		test("signal_emit_rvalue_reaches_all_receivers", [this]() {
			jai::signal_emitter<void(std::shared_ptr<int>)> emitter;
			jai::signal<void(std::shared_ptr<int>)> sig(emitter);
			int sum = 0;
			auto r1 = sig.connect([&](std::shared_ptr<int> v) { sum += v ? *v : -1000; });
			auto r2 = sig.connect([&](std::shared_ptr<int> v) { sum += v ? *v : -1000; });
			emitter(std::make_shared<int>(21));  // temporary: both receivers must see it
			check_eq(42, sum);
		});
		test("signal_emit_rvalue_string_reaches_all_receivers", [this]() {
			jai::signal_emitter<void(std::string)> emitter;
			jai::signal<void(std::string)> sig(emitter);
			std::string seen;
			auto r1 = sig.connect([&](std::string s) { seen += s; });
			auto r2 = sig.connect([&](std::string s) { seen += s; });
			emitter(std::string("ab"));
			check_eq(std::string("abab"), seen);
		});

		// ---- #0/#14 parser depth guards cover statements and grouping parens ----
		test("deep_if_nesting_no_crash", [this]() {
			auto e = engine::make();
			std::string src;
			for (int i = 0; i < 5000; ++i) { src += "if (true) "; }
			src += "1;";
			check_throws([&]() { e->execute(src); });            // guard fires, no stack overflow
			check_eq((int64_t)3, e->execute("1 + 2").as_int());  // engine stays usable
		});
		test("deep_block_nesting_no_crash", [this]() {
			auto e = engine::make();
			std::string src(5000, '{');
			check_throws([&]() { e->execute(src); });
			check_eq((int64_t)3, e->execute("1 + 2").as_int());
		});
		test("deep_paren_nesting_no_crash", [this]() {
			auto e = engine::make();
			std::string src(200, '(');
			src += "1";
			src += std::string(200, ')');
			check_throws([&]() { e->execute(src); });
			check_eq((int64_t)3, e->execute("1 + 2").as_int());
		});
		test("modest_paren_nesting_still_works", [this]() {
			auto e = engine::make();
			std::string src(20, '(');
			src += "1 + 2";
			src += std::string(20, ')');
			check_eq((int64_t)3, e->execute(src).as_int());
		});

		// ---- scripted-entity glue: hook properties + signal connect + getVar/setVar ----
		test("game_pattern_hooks_signals_vars", [this]() {
			auto e = engine::make();
			jai::bind_signal_type<void(int)>(*e, "SignalInt");
			{
				jai::dynamic_binder<scripted_thing> builder(*e, "ScriptedThing");
				builder.property("hook", &scripted_thing::hook);
				builder.property("onPing", [](scripted_thing& s) -> jai::signal<void(int)>& { return s.onPing; }, nullptr);
				builder.method("setVar", [](scripted_thing& s, const std::string& k, script_value v) {
					s.vars[k] = std::move(v);
				});
				builder.method("getVar", [eng = e.get()](scripted_thing& s, const std::string& k) {
					auto it = s.vars.find(k);
					return it != s.vars.end() ? it->second : script_value(std::monostate{}, eng);
				});
				builder.build();
			}
			scripted_thing thing;
			auto self = e->make_object(std::shared_ptr<scripted_thing>(&thing, [](scripted_thing*) {}));
			e->add_global("self", self);
			e->execute(R"(
				self.setVar("count", 0);
				self.hook = [=](int amount) -> void {
					self.setVar("count", self.getVar("count") + amount);
				};
				self.onPing.connect("test", [=](int v) -> void {
					self.setVar("count", self.getVar("count") + v * 100);
				});
			)");
			check(static_cast<bool>(thing.hook));               // script lambda landed in the std::function
			thing.hook(3);                                      // C++ invokes the script hook
			thing.pingEmitter(2);                               // C++ emit reaches the script receiver
			check_eq((int64_t)203, e->execute("self.getVar(\"count\");").as_int());
		});

		// ---- port-scout G1: compound assignment through a map subscript ----
		test("map_subscript_compound_assign", [this]() {
			auto e = engine::make();
			auto r = e->execute(R"(
				var m = {"k": 10.0};
				m["k"] -= 2.5;
				m["k"] *= 2.0;
				m["k"];
			)");
			check_near(15.0, r.as_float(), 1e-9);
		});

		// ---- port-scout G2: subscript-inserted keys must carry the engine (clone poisoning) ----
		test("map_subscript_insert_is_cloneable", [this]() {
			auto e = engine::make();
			auto r = e->execute(R"(
				var m = {"v": 1};
				m["f"] = [=]() { return 7; };
				m["n"] = 2;
				var call_it = [=](var s) { return s["f"]() + s["n"]; };
				call_it(m);
			)");
			check_eq((int64_t)9, r.as_int());
		});

		// ---- #39 coroutine resume surfaces runtime errors instead of stale yields ----
		test("coroutine_resume_propagates_runtime_error", [this]() {
			auto e = engine::make();
			check_throws([&]() {
				e->execute(R"(
					coroutine int co() {
						yield 5;
						int z = 0;
						int x = 1 / z;
						yield x;
					}
					auto c = co();
					auto a = c.resume();
					c.resume();
				)");
			});
			check_eq((int64_t)4, e->execute("2 + 2").as_int());
		});

		// ---- #40 nested execute() inside a running coroutine must not corrupt it ----
		test("nested_execute_inside_coroutine_keeps_yield_machinery", [this]() {
			auto e = engine::make();
			engine* raw = e.get();
			e->add_function("nested_exec", [raw]() -> script_int {
				raw->execute("1 + 1");
				return 0;
			});
			auto r = e->execute(R"(
				coroutine int co() {
					yield 1;
					nested_exec();
					yield 3;
					return 0;
				}
				auto c = co();
				auto a = c.resume();
				auto b = c.resume();
				a * 10 + b;
			)");
			check_eq((int64_t)13, r.as_int());
		});

		// ---- #1/#91 parse errors carry the parser's real diagnostic ----
		test("parse_error_carries_location_detail", [this]() {
			auto e = engine::make();
			bool threw = false;
			std::string msg;
			try {
				e->execute("42 +;");
			} catch (const std::exception& ex) {
				threw = true;
				msg = ex.what();
			}
			check(threw);
			// The boundary must surface the parser's file:line:col detail,
			// not just the generic category string.
			check(msg.find("1:") != std::string::npos);
		});
	}
};

} // namespace jai::foundry::tests

using release_review_regression_tests = jai::foundry::tests::release_review_regression_tests;
FOUNDRY_REGISTER(release_review_regression_tests)
