#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

namespace jai::foundry::tests {

class construction_depth_tests : public suite {
public:
	construction_depth_tests() : suite("Construction Depth") {}

	template<class Engine>
	static void configure(Engine& eng, size_t depth) {
		if constexpr (requires { eng.max_call_depth(eng.get_backend_type(), depth); }) {
			eng.max_call_depth(eng.get_backend_type(), depth);
		} else {
			check_true(false, "engine::max_call_depth configuration is missing");
		}
	}

	static void expect_depth_error(engine& eng, const std::string& source) {
		std::string message;
		std::string outcome;
		try {
			auto result = eng.execute(source);
			outcome = "returned " + std::to_string(result.as_int());
		} catch (const std::exception& error) {
			message = error.what();
			outcome = "raised " + message;
		}
		check_eq(script_int(42), eng.execute("40 + 2;").as_int());
		check_true(message.find("Maximum recursion depth") != std::string::npos,
			"expected a catchable depth error, but " + outcome);
	}

	void forge_tests() override {
		test("initialization_and_body_use_one_constructor_depth", [this]() {
			for (const std::string& source : {
				"class C { int value = 1 + 2; C() { value += 4; } } C().value;",
				"class B { int value = 1 + 2; B() { value += 4; } } class C : B { C() : super() {} } C().value;"}) {
				auto eng = make_engine();
				configure(*eng, 1);
				check_eq(script_int(7), eng->execute(source).as_int());
			}
		});

		test("implicit_constructor_field_recursion_obeys_cap", [this]() {
			auto eng = make_engine();
			configure(*eng, 8);
			// No script function body is entered. The counter bounds old binaries
			// to exactly 20 constructions rather than probing unbounded recursion.
			expect_depth_error(*eng, R"(
				int remaining = 20;
				class C { var child = --remaining > 0 ? C() : null; }
				auto root = C();
				remaining;
			)");
		});

		test("explicit_constructor_field_recursion_obeys_cap", [this]() {
			auto eng = make_engine();
			configure(*eng, 8);
			// All recursive field initialization precedes the first guarded body.
			expect_depth_error(*eng, R"(
				int remaining = 20;
				class C {
					var child = --remaining > 0 ? C() : null;
					C() {}
				}
				auto root = C();
				remaining;
			)");
		});

		test("super_chain_field_recursion_preserves_depth_error", [this]() {
			auto eng = make_engine();
			configure(*eng, 8);
			expect_depth_error(*eng, R"(
				int remaining = 20;
				class Base {
					var child = --remaining > 0 ? C() : null;
					Base() {}
				}
				class C : Base { C() : super() {} }
				auto root = C();
				remaining;
			)");
		});

		test("script_catches_field_depth_and_keeps_member_scope", [this]() {
			auto eng = make_engine();
			configure(*eng, 8);
			check_eq(script_int(42), eng->execute(R"(
				int remaining = 20;
				string message = "";
				class C { var child = --remaining > 0 ? C() : null; }
				class Outer {
					int value = 41;
					int run() {
						try { C(); }
						catch (error) { message = error; return value + 1; }
						return 0;
					}
				}
				Outer().run();
			)").as_int());
			check_true(eng->execute("message;").as<std::string>().find("Maximum recursion depth") != std::string::npos);
			check_eq(script_int(42), eng->execute("40 + 2;").as_int());
		});

		test("constructor_default_argument_recursion_obeys_cap", [this]() {
			auto eng = make_engine();
			configure(*eng, 8);
			// Defaults already run inside call_function/call_script_function. This
			// control separates that path from pre-body field initializer recursion.
			expect_depth_error(*eng, R"(
				int remaining = 20;
				class C { C(var child = --remaining > 0 ? C() : null) {} }
				auto root = C();
				remaining;
			)");
		});

		test("native_constructor_default_reentry_obeys_cap", [this]() {
			auto eng = make_engine();
			configure(*eng, 8);
			engine* raw = eng.get();
			eng->add_function("native_default", [raw]() -> script_int {
				return raw->execute("--remaining > 0 ? C().value : 0;").as_int();
			});
			expect_depth_error(*eng, R"(
				int remaining = 20;
				class C {
					int value = 0;
					C(int n = native_default()) { value = n; }
				}
				auto root = C();
				remaining;
			)");
		});
	}
};

} // namespace jai::foundry::tests

using construction_depth_tests = jai::foundry::tests::construction_depth_tests;
FOUNDRY_REGISTER(construction_depth_tests)
