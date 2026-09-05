#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <limits>

namespace jai::foundry::tests {

class execution_depth_tests : public suite {
public:
	execution_depth_tests() : suite("Execution Depth") {}

	template<class Engine>
	static void configure(Engine& eng, backend_type backend, size_t depth) {
		if constexpr (requires { eng.max_call_depth(backend, depth); }) {
			eng.max_call_depth(backend, depth);
		} else {
			check_true(false, "engine::max_call_depth configuration is missing");
		}
	}

	template<class Engine>
	static size_t configured(const Engine& eng, backend_type backend) {
		if constexpr (requires { eng.max_call_depth(backend); }) {
			return eng.max_call_depth(backend);
		} else {
			check_true(false, "engine::max_call_depth query is missing");
			return 0;
		}
	}

	static std::string depth_error(engine& eng, const std::string& source) {
		std::string result;
		try { eng.execute(source); }
		catch (const std::exception& error) { result = error.what(); }
		check_true(result.find("Maximum recursion depth") != std::string::npos,
			"expected a catchable depth error, got: " + result);
		check_eq(script_int(42), eng.execute("40 + 2;").as_int());
		return result;
	}

	void forge_tests() override {
		test("defaults_are_backend_specific", [this]() {
			auto eng = make_engine();
			check_eq(size_t(256), configured(*eng, backend_type::interpreter));
			check_eq(size_t(10000), configured(*eng, backend_type::vm));
		});

		test("interpreter_default_bounds_non_tail_recursion", [this]() {
			auto eng = engine::make();
			eng->execution_budget(0);
			depth_error(*eng, "int descend(int n) { if (n == 0) { return 0; } return descend(n - 1) + 1; } descend(512);");
		});

		test("vm_default_preserves_deep_non_tail_recursion", [this]() {
			auto eng = engine::make();
			eng->set_backend(backend_type::vm);
			eng->execution_budget(0);
			check_eq(script_int(5000), eng->execute(
				"int descend(int n) { if (n == 0) { return 0; } return descend(n - 1) + 1; } descend(5000);").as_int());
		});

		test("configured_boundary_and_recovery", [this]() {
			auto eng = make_engine();
			configure(*eng, eng->get_backend_type(), 8);
			eng->execute("int descend(int n) { if (n == 0) { return 0; } return descend(n - 1) + 1; }");
			check_eq(script_int(7), eng->execute("descend(7);").as_int());
			depth_error(*eng, "descend(8);");
			check_eq(script_int(7), eng->execute("descend(7);").as_int());
			configure(*eng, eng->get_backend_type(), 32);
			check_eq(script_int(20), eng->execute("descend(20);").as_int());
			configure(*eng, eng->get_backend_type(), 4);
			depth_error(*eng, "descend(4);");
		});

		test("engine_configuration_is_independent_and_survives_backend_selection", [this]() {
			auto first = engine::make();
			auto second = engine::make();
			configure(*first, backend_type::interpreter, 12);
			configure(*first, backend_type::vm, 48);
			first->set_backend(backend_type::vm);
			check_eq(size_t(12), configured(*first, backend_type::interpreter));
			check_eq(size_t(48), configured(*first, backend_type::vm));
			check_eq(size_t(256), configured(*second, backend_type::interpreter));
			check_eq(size_t(10000), configured(*second, backend_type::vm));
		});

		test("invalid_caps_leave_configuration_unchanged", [this]() {
			auto eng = make_engine();
			configure(*eng, eng->get_backend_type(), 12);
			for (size_t invalid : {size_t(0), size_t(std::numeric_limits<int>::max()) + 1}) {
				check_throws([&]() { configure(*eng, eng->get_backend_type(), invalid); });
				check_eq(size_t(12), configured(*eng, eng->get_backend_type()));
			}
			check_throws([&]() { configure(*eng, backend_type::custom, 12); });
		});

		test("cap_error_text_is_identical_with_distinct_caps", [this]() {
			std::string messages[2];
			for (int i = 0; i < 2; ++i) {
				auto eng = engine::make();
				eng->set_backend(i == 0 ? backend_type::interpreter : backend_type::vm);
				configure(*eng, eng->get_backend_type(), i == 0 ? 8 : 16);
				messages[i] = depth_error(*eng, "int forever() { return forever() + 1; } forever();");
			}
			check_eq(messages[0], messages[1]);
			check_true(messages[0].starts_with("Maximum recursion depth exceeded - possible infinite recursion"));
		});

		test("script_can_catch_depth_error_and_continue", [this]() {
			auto eng = make_engine();
			configure(*eng, eng->get_backend_type(), 8);
			check_eq(script_int(42), eng->execute(R"(
				int forever() { return forever() + 1; }
				int result = 0;
				try { result = forever(); } catch { result = 42; }
				result;
			)").as_int());
		});

		test("method_and_constructor_bodies_obey_configuration", [this]() {
			for (const std::string source : {
				"class C { int f(int n) { if (n == 0) { return 0; } return f(n-1)+1; } } C().f(20);",
				"class C { static int f(int n) { if (n == 0) { return 0; } return C::f(n-1)+1; } } C::f(20);",
				"class C { int f(int n) { if (n == 0) { return 0; } return f(n-1)+1; } } auto c = C(); auto bound = c.f; bound(20);",
				"class C { C(int n) { if (n > 0) { auto child = C(n-1); } } } C(20);"}) {
				auto eng = make_engine();
				configure(*eng, eng->get_backend_type(), 8);
				depth_error(*eng, source);
			}
		});

		test("configuration_cannot_change_inside_execution", [this]() {
			auto eng = make_engine();
			const auto backend = eng->get_backend_type();
			configure(*eng, backend, 12);
			engine* raw = eng.get();
			eng->add_function("change_depth", [raw, backend]() { configure(*raw, backend, 24); });
			std::string message;
			try { eng->execute("change_depth();"); }
			catch (const std::exception& error) { message = error.what(); }
			check_true(message.find("Cannot change max_call_depth during execution") != std::string::npos);
			check_eq(size_t(12), configured(*eng, backend));
			check_eq(script_int(42), eng->execute("40 + 2;").as_int());
		});

		test("reused_parallel_workers_receive_updated_caps", [this]() {
			auto eng = make_engine();
			eng->parallel_thread_count(4);
			configure(*eng, eng->get_backend_type(), 64);
			eng->execute(R"(
				int descend(int n) { if (n == 0) { return 0; } return descend(n-1)+1; }
				int item(int n) { return descend(n); }
				var input = [];
				for (int i = 0; i < 256; ++i) { input.push(20); }
			)");
			const std::string source = "auto output = parallel_transform(input, item); output[0] + output[255];";
			check_eq(script_int(40), eng->execute(source).as_int());
			configure(*eng, eng->get_backend_type(), 8);
			depth_error(*eng, source);
			configure(*eng, eng->get_backend_type(), 64);
			check_eq(script_int(40), eng->execute(source).as_int());
		});

		test("vm_tail_calls_do_not_consume_additional_depth", [this]() {
			auto eng = engine::make();
			eng->set_backend(backend_type::vm);
			eng->execution_budget(0);
			configure(*eng, backend_type::vm, 4);
			check_eq(script_int(0), eng->execute(
				"int descend(int n) { if (n == 0) { return 0; } return descend(n-1); } descend(100000);").as_int());
		});
	}
};

} // namespace jai::foundry::tests

using execution_depth_tests = jai::foundry::tests::execution_depth_tests;
FOUNDRY_REGISTER(execution_depth_tests)
