#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/testing/foundry.hpp>
#include <string>

namespace jai::foundry::tests {

class numeric_operator_transition_tests : public suite {
	void install_operator(engine& a_engine) {
		a_engine.add_function("+", [](script_int a_left, script_int a_right) {
			return a_left * a_right;
		});
		a_engine.set_has_custom_numeric_operators(true);
	}

public:
	numeric_operator_transition_tests() : suite("Numeric Operator Transitions") {}

	void forge_tests() override {
		// The native callback changes the operator table while this same script frame
		// is active. The next typed operation must observe the new operator, including
		// when a future region keeps scalar temporaries unboxed around calls.
		test("registered_mid_function_before_next_typed_operation", [this]() {
			for (bool proofs : {true, false}) {
				for (bool temporary : {false, true}) {
					auto eng = make_engine();
					eng->type_proofs(proofs);
					eng->add_function("install", [this, raw = eng.get()]() {
						install_operator(*raw);
					});
					eng->add_function("pack", [](script_int a_first, script_int a_second) {
						return a_first * 100 + a_second;
					});
					const std::string expression = temporary ? "(a + b) * 10" : "a + b";
					const std::string script = "int calc(int a, int b) { int first = " +
						expression + "; install(); int second = " + expression +
						"; return pack(first, second); } calc(1, 2);";
					const script_int expected = temporary ? 3020 : 302;
					check_eq(expected, eng->execute(script).as_int(), script);
				}
			}
		});
		// var operands in frame slots take the dynamic slot-operand ops (op_d_*): every kind pair, landing and failure
		// reads as the interpreter reads it, with proofs and without
		test("dynamic_slot_operand_ops_match_the_interpreter", [this]() {
			const std::string script =
				"function f(var a, var b) {\n"
				"    var t = a + b;\n"
				"    var u = 0; u = a * b;\n"
				"    var w = (a - b) * 2;\n"
				"    float g = a + b;\n"
				"    if (a < b) { t = t + 1; }\n"
				"    if (a == b) { t = t - 1; }\n"
				"    return to_string(t) + \",\" + to_string(u) + \",\" + to_string(w) + \",\" + to_string(g) + \",\" + type_of(u);\n"
				"}\n"
				"function bits(var a, var b) { var r = (a & b) | (a ^ b); r = r << 2; var s = r >> 1; return to_string(r) + \",\" + to_string(s); }\n"
				"function fail(var a, var b, var k) {\n"
				"    try {\n"
				"        if (k == 0) { var q = a / b; return to_string(q); }\n"
				"        if (k == 1) { var q = a % b; return to_string(q); }\n"
				"        if (k == 2) { var q = a * b; return to_string(q); }\n"
				"        if (k == 3) { var q = a << b; return to_string(q); }\n"
				"        if (k == 5) { var q = a + b; return to_string(q); }\n"
				"        var q = a - b; return to_string(q);\n"
				"    } catch (e) { return \"caught: \" + e; }\n"
				"}\n"
				"var out = f(3, 4) + \"|\" + f(2.5, 4) + \"|\" + f(7, 2.0) + \"|\" + f(6, 6) + \"|\" + bits(12, 10) + \"|\" + fail(\"x\", \"y\", 5);\n"
				"out = out + \"|\" + fail(1, 0, 0) + \"|\" + fail(1.0, 0.0, 0) + \"|\" + fail(5, 0, 1) + \"|\" + fail(9223372036854775807, 2, 2);\n"
				"out = out + \"|\" + fail(1, 64, 3) + \"|\" + fail(-9223372036854775807, 2, 4) + \"|\" + fail(\"s\", 1, 4);\n"
				"out;\n";
			auto run = [&](backend_type type, bool proofs) {
				auto eng = engine::make();
				eng->set_backend(type);
				eng->type_proofs(proofs);
				jai::stdlib::register_all(*eng);
				return eng->execute(script).as_string();
			};
			const std::string oracle = run(backend_type::interpreter, true);
			check_true(oracle.find("caught") != std::string::npos);
			for (bool proofs : {true, false}) {
				check_eq(oracle, run(backend_type::vm, proofs));
			}
		});
		// operands that reach a binary through the stack (call results, member reads): the numeric rows, the failures and an
		// operator registered mid-run read as the interpreter reads them
		test("stack_operand_binaries_match_the_interpreter", [this]() {
			const std::string script =
				"class P { var a = 0; var b = 0; P(var x, var y) { a = x; b = y; } }\n"
				"function id(var v) { return v; }\n"
				"function rows(var x, var y) {\n"
				"    var p = P(x, y);\n"
				"    var out = [];\n"
				"    out.push(p.a + p.b); out.push(p.a - p.b); out.push(p.a * p.b); out.push(id(x) < id(y)); out.push(id(x) >= id(y));\n"
				"    out.push(id(x) == id(y)); out.push(id(x) != id(y)); out.push(p.a <= p.b); out.push(p.a > p.b);\n"
				"    try { out.push(id(x) / id(y)); } catch (e) { out.push(\"caught: \" + e); }\n"
				"    try { out.push(id(x) % id(y)); } catch (e) { out.push(\"caught: \" + e); }\n"
				"    return to_string(out);\n"
				"}\n"
				"function big(var x) { try { return to_string(id(x) * id(x)); } catch (e) { return \"caught: \" + e; } }\n"
				"rows(7, 3) + \"|\" + rows(2.5, 4) + \"|\" + rows(9, 0) + \"|\" + rows(1.0, 0.0) + \"|\" + rows(-4, 2.0) + \"|\" + big(4000000000) + \"|\" + big(3.0);\n";
			auto run = [&](backend_type type, bool proofs) {
				auto eng = engine::make();
				eng->set_backend(type);
				eng->type_proofs(proofs);
				jai::stdlib::register_all(*eng);
				return eng->execute(script).as_string();
			};
			const std::string oracle = run(backend_type::interpreter, true);
			check_true(oracle.find("caught") != std::string::npos);
			for (bool proofs : {true, false}) {
				check_eq(oracle, run(backend_type::vm, proofs));
			}
		});
		// the census rows: float compounds, string compares and concats through the dynamic ops, string appends, environment stores
		test("general_form_string_float_and_environment_rows_match_the_interpreter", [this]() {
			const std::string script =
				"var g = 0; int gi = 1; float gf = 0.5; var gs = \"a\"; string gt = \"t\";\n"
				"function floats(float x, float y) { float s = 0.0; for (int i = 0; i < 4; ++i) { s += x; s -= y * 0.5; s *= 1.5; } return s; }\n"
				"function strs(var a, var b) {\n"
				"    var out = \"\";\n"
				"    if (a < b) { out += \"lt\"; } if (a == b) { out += \"eq\"; } if (a != b) { out += \"ne\"; } if (a >= b) { out += \"ge\"; }\n"
				"    var c = a + b; string d = a + b; var e = \"\"; e = c + d; var n = null;\n"
				"    if (n == a) { out += \"null\"; }\n"
				"    for (int i = 0; i < 3; ++i) { out += a; out += \"-\"; }\n"
				"    return out + \"|\" + c + \"|\" + d + \"|\" + e;\n"
				"}\n"
				"function globals() {\n"
				"    for (int i = 0; i < 5; ++i) { g = i; gi = i * 2; gf = i + 0.25; gs = \"s\" + to_string(i); gt = gs; }\n"
				"    g = 2.5; g = true; g = 'c';\n"
				"    var r = to_string(g) + \",\" + to_string(gi) + \",\" + to_string(gf) + \",\" + gs + \",\" + gt + \",\" + type_of(g) + type_of(gi) + type_of(gf);\n"
				"    var bad = \"no\"; var frac = 2.75;\n"
				"    try { gi = bad; } catch (e) { r = r + \",caught: \" + e; }\n"
				"    try { gi = frac; } catch (e) { r = r + \",caught: \" + e; }\n"
				"    return r + \",\" + to_string(gi);\n"
				"}\n"
				"g = 9; gi = 3;\n"
				"floats(2.0, 1.0) + \"|\" + strs(\"x\", \"y\") + \"|\" + strs(\"q\", \"q\") + \"|\" + globals() + \"|\" + to_string(g) + to_string(gi);\n";
			auto run = [&](backend_type type, bool proofs) {
				auto eng = engine::make();
				if (type != backend_type::interpreter) { eng->set_backend(type); }
				eng->type_proofs(proofs);
				jai::stdlib::register_all(*eng);
				return eng->execute(script).as_string();
			};
			const std::string oracle = run(backend_type::interpreter, true);
			check_true(oracle.find("caught") != std::string::npos);
			for (bool proofs : {true, false}) {
				check_eq(oracle, run(backend_type::vm, proofs));
			}
		});
		// a numeric operator registered while a dynamic op's frame runs is consulted by its next execution
		test("dynamic_slot_operand_ops_see_an_operator_registered_mid_function", [this]() {
			for (bool proofs : {true, false}) {
				auto eng = make_engine();
				eng->type_proofs(proofs);
				eng->add_function("install", [this, raw = eng.get()]() { install_operator(*raw); });
				eng->add_function("pack", [](script_int a_first, script_int a_second) { return a_first * 100 + a_second; });
				const std::string script = "function calc(var a, var b) { var first = a + b; install(); var second = a + b; return pack(first, second); } calc(3, 4);";
				check_eq(static_cast<script_int>(712), eng->execute(script).as_int(), script);
			}
		});
	}
};

} // namespace jai::foundry::tests

using numeric_operator_transition_tests_alias = jai::foundry::tests::numeric_operator_transition_tests;
FOUNDRY_REGISTER(numeric_operator_transition_tests_alias)
