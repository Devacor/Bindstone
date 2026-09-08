#include <jaiscript/core/engine.hpp>
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
	}
};

} // namespace jai::foundry::tests

using numeric_operator_transition_tests_alias = jai::foundry::tests::numeric_operator_transition_tests;
FOUNDRY_REGISTER(numeric_operator_transition_tests_alias)
