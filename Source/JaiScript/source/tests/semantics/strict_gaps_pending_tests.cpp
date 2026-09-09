// Approved strict-rule battery. Register accepted cases individually.
// Rules 1-3 and the two explicitly deferred proposals remain separate.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/jaibite.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

namespace jai::foundry::tests {

class strict_gaps_pending_tests : public suite {
public:
	strict_gaps_pending_tests() : suite("Strict Rule Acceptance") {}

	void forge_tests() override {
		rules_456_and_controls();
		// element_rules();                 // rules 1-3: next increment
		// deferred_proposals();            // not approved
	}

private:
	void rules_456_and_controls() {
		test("control_bool_any", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(bool b = [1]; b;)jai";
			const auto report = engine->check(source);
			check_false(report.has_errors());
		});

		test("control_string_any", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(string s = 42; s;)jai";
			const auto report = engine->check(source);
			check_false(report.has_errors());
		});

		test("control_shared_lenient", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(shared_ptr<int> p = "bad"; p;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_weak_lenient", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(weak_ptr<int> p = "bad"; p;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("control_reference_lenient", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(string s = "bad"; int& r = s; r;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("control_var_any", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(var v = 1; v = "bad"; v;)jai";
			const auto report = engine->check(source);
			check_false(report.has_errors());
		});

		test("control_var_typed_array_read", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<int> a = [1]; var v = a; string x = v[0]; x;)jai";
			const auto report = engine->check(source);
			check_false(report.has_errors());
		});

		test("control_var_typed_array_write", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<int> a = [1]; var v = a; v[0] = "bad"; v[0];)jai";
			const auto report = engine->check(source);
			check_false(report.has_errors());
		});

		test("control_auto_null", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(auto x = null; x = 1; x = "bad"; x;)jai";
			const auto report = engine->check(source);
			check_false(report.has_errors());
		});

		test("control_int_direct_control", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(int x = "bad"; x;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("control_float_direct_control", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(float x = "bad"; x;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("control_char_direct_control", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(char c = 65; c;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("control_object_direct_control", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(class C {} class D {} C c = D(); c;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("control_auto_direct_control", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(auto x = 1; x = "bad"; x;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("control_array_kind_control", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<int> a = 1; a;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("control_container_finite_float", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<int> a = [3.9]; a[0];)jai";
			const auto report = engine->check(source);
			check_false(report.has_errors());
		});

		test("control_function_slot_any", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(function f = 42; f;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("control_reference_write", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(string s = "bad"; int& r = s; r = 1; s;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("control_var_copy_original", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<int> a = [1]; var v = a; v[0] = "bad"; a[0];)jai";
			const auto report = engine->check(source);
			check_false(report.has_errors());
		});

		test("control_var_demoted_retyped", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<int> a = [1]; var v = a; v[0] = "bad"; array<int> b = v; b;)jai";
			const auto report = engine->check(source);
			check_false(report.has_errors());
		});

		test("control_var_nested_demotion", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<array<int>> a = [[1]]; var v = a; v[0][0] = "bad"; v;)jai";
			const auto report = engine->check(source);
			check_false(report.has_errors());
		});
		test("rules_456_known_type_rejections", [this]() {
			const char* sources[] = {
				"class A {} class B {} shared_ptr<B> h = new B(); weak_ptr<A> w = h;",
				"class A {} A a = A(); weak_ptr<A> w = a;",
				"shared_ptr<float> h = new float(1.0); weak_ptr<int> w = h;",
				"int i = 1; float& f = i;",
				"int i = 1; string& s = i;",
				"string s = \"s\"; string& a = s; int& b = a;",
				"class A {} class B { B(A a) {} } A a = A(); B& b = a;",
				"array<int> a = [1]; array<float>& b = a;",
				"function f = null; int i = 4; f = i;",
				"function f; f = 42;",
				"class C { function f = 42; }",
				"class C { function f = null; } C c = C(); c.f = 42;",
				"namespace N { function f = 42; }",
				"class C { static function f = null; } C::f = 42;",
				"class C { function f = null; void bad() { f = 42; } }",
				"class C { static function f = null; } C::f = 42;",
				"namespace N { function f = 42; }",
				"class A {} function f = A();"
			};
			for (const char* source : sources) {
				auto eng = make_engine();
				check_report report;
				try { report = eng->check(source); }
				catch (const std::exception& error) { throw test_failure(std::string(source) + "\n" + error.what()); }
				check_true(report.has_errors(), source);
				bool rejected = false;
				try { eng->execute(source); }
				catch (const static_check_error&) { rejected = true; }
				check_true(rejected, "strict checking rejects before execution");
			}
		});

		test("rules_456_valid_and_dynamic_sources", [this]() {
			const char* sources[] = {
				"class A {} class B : A {} shared_ptr<B> h = new B(); weak_ptr<A> w = h; weak_ptr<A> copy = w;",
				"weak_ptr<int> w = null;",
				"class A {} weak_ptr<A> w = new A();",
				"class A {} weak_ptr<A> w = shared_ptr<A>();",
				"int i = 1; int& r = i; int& s = r;",
				"class A {} class B : A {} B b = B(); A& a = b;",
				"array<int> a = [1]; array<int>& b = a;",
				"var v = \"dynamic\"; int& r = v;",
				"var v = \"dynamic\"; weak_ptr<int> w = v;",
				"function f = null; f = []() { return 1; };",
				"int original() { return 1; } function f = original;",
				"class C { int value() { return 1; } } C c = C(); function f = c.value;",
				"var v = 42; function f = v;",
				"auto f = 42; f = 43;",
				"function f; f = []() { return 1; };",
				"namespace N { function f = []() { return 1; }; }",
				"void g(function cb) {} g(42);",
				"class C { function f = null; } C c = C(); c.f = []() { return 1; }; function copy = c.f;",
				"function f = [](int x) -> int { return x; }; f(\"bad\");"
			};
			for (const char* source : sources) {
				auto eng = make_engine();
				const auto report = eng->check(source);
				check_false(report.has_errors(), std::string(source) + "\n" + report.format(source));
			}
		});

		test("rules_456_warn_regions_and_unchecked_runtime", [this]() {
			for (const char* source : {
				"weak_ptr<int> w = \"bad\";",
				"string s = \"s\"; int& r = s;",
				"function f = 42;"
			}) {
				for (bool block : {false, true}) {
					auto eng = make_engine();
					const std::string unit = block
						? std::string("{ [[warn_mode]]; ") + source + " }"
						: std::string("[[warn_mode]];\n") + source;
					const auto report = eng->check(unit);
					check_false(report.has_errors(), report.format(unit));
					check_true(!report.diagnostics.empty(), "rule is downgraded, not omitted");
				}
			}
			auto eng = make_engine();
			eng->static_checking(check_mode::off);
			check_eq(script_int{1}, eng->execute("string s = \"s\"; int& r = s; r = 1; s;").as_int());
			check_eq(script_int{42}, eng->execute("function f = 42; f;").as_int());
			check_eq(script_int{1}, eng->execute(
				"class A {} class B {} shared_ptr<B> owner = new B(); weak_ptr<B> wb = owner; weak_ptr<A> wa = wb; 1;").as_int());
		});

		test("rules_456_valid_runtime_boundaries", [this]() {
			for (const char* source : {
				"int i = 1; int& r = i; r = 7; i;",
				"class A { int x = 1; } class B : A {} B b = B(); A& r = b; r.x = 7; b.x;",
				"class A { int x = 7; } class B : A {} shared_ptr<B> owner = new B(); weak_ptr<A> weak = owner; auto locked = weak.lock(); locked.x;",
				"function f = []() { return 1; }; f = null; f = []() { return 7; }; f();"
			}) {
				auto eng = make_engine();
				const auto report = eng->check(source);
				check_false(report.has_errors(), std::string(source) + "\n" + report.format(source));
				check_eq(script_int{7}, eng->execute(source).as_int(), source);
			}
		});

		test("rules_456_member_assignment_result_types", [this]() {
			for (const char* source : {
				"class C { int n = 0; } C c = C(); auto x = (c.n = 3.9); int& r = x; r == 3;",
				"class C { int n = 1; } C c = C(); auto x = (c.n += 2.9); int& r = x; r == 3;",
				"class C { static int n = 1; } auto x = (C::n += 2.9); int& r = x; r == 3;",
				"class C { static int n = 0; } auto x = (C::n = 3.9); int& r = x; r == 3;",
				"int n = 0; auto x = (n = 3.9); int& r = x; r == 3;",
				"int n = 1; auto x = (n += 2.9); int& r = x; r == 3;",
				"class C { int n = 1; bool test() { auto x = (n += 2.9); int& r = x; return r == 3; } } C c = C(); c.test();",
				"class C { int n = 1; int _get_x() { return n; } void _set_x(int v) { n = v; } } C c = C(); auto x = (c.x += 2.9); int& r = x; r == 3;",
				"class C { string s = \"\"; } C c = C(); map<string,int> m = {{\"a\",1}}; for (auto kv : m) { auto x = (c.s = kv.first); string& r = x; r = \"b\"; return kv.first == \"a\" && r == \"b\"; } false;"
			}) {
				auto eng = make_engine();
				jai::stdlib::register_all(*eng);
				const auto report = eng->check(source);
				check_false(report.has_errors(), std::string(source) + "\n" + report.format(source));
				check_true(eng->execute(source).as<bool>(), source);
			}
		});

		test("rules_456_saved_declarations_recheck", [this]() {
			for (const char* source : {
				"function f = 42;",
				"class C { function f = 42; }",
				"class C { function f = null; } C c = C(); c.f = 42;",
				"namespace N { function f = 42; }",
				"class C { static function f = null; } C::f = 42;"
			}) {
				auto author = make_engine();
				author->static_checking(check_mode::off);
				auto bytes = author->jaibite(source).save_bytes();
				auto loader = make_engine();
				bool rejected = false;
				try { auto bite = loader->jaibite_load_bytes(bytes); }
				catch (const static_check_error&) { rejected = true; }
				check_true(rejected, source);
			}
		});

	}

	void element_rules() {
		test("pending_reject_int_subscript", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<string> a = ["bad"]; int x = a[0]; x;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_float_subscript", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<string> a = ["bad"]; float x = a[0]; x;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_char_subscript", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<int> a = [65]; char c = a[0]; c;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_array_element", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<int> a = ["bad"]; a;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_map_value", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(map<string, int> m = {{"key", "bad"}}; m;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_object_subscript", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(class C {} class D {} array<D> a = [D()]; C c = a[0]; c;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_auto_subscript", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(auto x = 1; array<string> a = ["bad"]; x = a[0]; x;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_nested_array", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<array<int>> a = [[1, "bad"]]; a;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_nested_map_value", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(map<string, array<int>> m = {{"key", [1, "bad"]}}; m;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_nested_map_key", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(map<int, array<int>> m = {{"bad", [1]}}; m;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_nested_push", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<array<int>> a = [[1]]; a.push(["bad"]); a;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_nested_subscript_write", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<array<int>> a = [[1]]; a[0][0] = "bad"; a;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_container_float_overflow", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<int> a = [1e30]; a;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_int_return_subscript", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<string> a = ["bad"]; int f() { return a[0]; } f();)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_int_parameter_subscript", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<string> a = ["bad"]; int f(int x) { return x; } f(a[0]);)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_int_field_subscript", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(class C { int x = 0; } array<string> a = ["bad"]; C c = C(); c.x = a[0]; c.x;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});
	}

	void deferred_proposals() {
		test("pending_reject_auto_no_initializer", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(auto x; x = 1; x = "bad"; x;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_function_call_argument", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(function f = [](int x) -> int { return x; }; f("bad");)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});
	}
};

} // namespace jai::foundry::tests

using strict_rule_acceptance_tests = jai::foundry::tests::strict_gaps_pending_tests;
FOUNDRY_REGISTER(strict_rule_acceptance_tests)
