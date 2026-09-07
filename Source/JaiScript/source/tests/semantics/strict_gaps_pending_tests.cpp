// Disabled evidence battery for docs/review-2026-09-06/STRICT_GAPS.md.
// No suite registration. Enable proposed rejection cases individually only after
// the documented policy questions are settled. Controls preserve current contracts.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

namespace jai::foundry::tests {

class strict_gaps_pending_tests : public suite {
public:
	strict_gaps_pending_tests() : suite("Strict Gaps Pending") {}

	void forge_tests() override {

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
			check_false(report.has_errors());
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

		test("pending_reject_auto_subscript", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(auto x = 1; array<string> a = ["bad"]; x = a[0]; x;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_auto_null", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(auto x = null; x = 1; x = "bad"; x;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_auto_no_initializer", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(auto x; x = 1; x = "bad"; x;)jai";
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

		test("pending_reject_container_float_overflow", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(array<int> a = [1e30]; a;)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("pending_reject_function_call_argument", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(function f = [](int x) -> int { return x; }; f("bad");)jai";
			const auto report = engine->check(source);
			check_true(report.has_errors());
		});

		test("control_function_slot_any", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(function f = 42; f;)jai";
			const auto report = engine->check(source);
			check_false(report.has_errors());
		});

		test("control_reference_write", [this]() {
			auto engine = make_engine();
			const std::string source = R"jai(string s = "bad"; int& r = s; r = 1; s;)jai";
			const auto report = engine->check(source);
			check_false(report.has_errors());
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
};

// No FOUNDRY_REGISTER or factory instance: this suite is intentionally disabled.
} // namespace jai::foundry::tests
