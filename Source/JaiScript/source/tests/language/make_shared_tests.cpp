#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/testing/foundry.hpp>

namespace jai::foundry::tests {

struct MakeSharedVec {
	int x = 0;
	MakeSharedVec() = default;
	explicit MakeSharedVec(int a_x) : x(a_x) {}
};

class make_shared_tests : public suite {
public:
	make_shared_tests() : suite("Make Shared") {}

	std::string outcome(const std::string& a_source) {
		auto eng = make_engine();
		dynamic_binder<MakeSharedVec>(*eng, "Vec")
			.constructor<>()
			.constructor<int>()
			.property("x", &MakeSharedVec::x)
			.build();
		std::string result;
		try {
			auto value = eng->execute(a_source);
			result = value.is_null() ? std::string("<null>") : value.to_string();
		} catch (const std::exception& e) {
			result = std::string("error: ") + e.what();
		}
		return result;
	}

	void same_as_new(const std::string& a_make_shared, const std::string& a_new, const std::string& a_expected) {
		auto spelled = outcome(a_make_shared);
		check_eq(a_expected, spelled);
		check_eq(outcome(a_new), spelled);
	}

	void forge_tests() override {
		test("bound_type_shares_like_new", [this]() {
			same_as_new(
				"shared_ptr<Vec> m = make_shared<Vec>(1); var n = m; n.x = 7; m.x",
				"shared_ptr<Vec> m = new Vec(1); var n = m; n.x = 7; m.x", "7");
		});
		test("bound_type_zero_args", [this]() {
			same_as_new("auto m = make_shared<Vec>(); m.x", "auto m = new Vec(); m.x", "0");
		});
		test("script_class_with_constructor", [this]() {
			same_as_new(
				"class P { int a = 0; P(int v) { a = v; } } auto p = make_shared<P>(4); auto q = p; q.a = 9; p.a",
				"class P { int a = 0; P(int v) { a = v; } } auto p = new P(4); auto q = p; q.a = 9; p.a", "9");
		});
		test("checker_types_it_as_shared_ptr", [this]() {
			same_as_new(
				"class P { int a = 2; } auto m = make_shared<P>(); shared_ptr<P> n = m; n.a",
				"class P { int a = 2; } auto m = new P(); shared_ptr<P> n = m; n.a", "2");
			check(outcome("class P { } auto m = make_shared<P>(); m = 5;").find("cannot assign 'int' to 'm' declared 'shared_ptr<P>'") != std::string::npos);
		});
		test("namespaced_type", [this]() {
			same_as_new(
				"namespace ns { class A { int x = 3; } } auto a = make_shared<ns::A>(); a.x",
				"namespace ns { class A { int x = 3; } } auto a = new ns::A(); a.x", "3");
		});
		test("nested_template_argument", [this]() {
			check_eq(outcome("auto a = new array<int>(); 1"), outcome("auto a = make_shared<array<int>>(); 1"));
		});
		test("unknown_type_errors_like_new", [this]() {
			auto spelled = outcome("auto a = make_shared<Nope>(); 1");
			check(spelled != "1");
			check_eq(outcome("auto a = new Nope(); 1"), spelled);
		});
		test("constructor_mismatch_errors_like_new", [this]() {
			check_eq(outcome("auto a = new Vec(1, 2, 3); 1"), outcome("auto a = make_shared<Vec>(1, 2, 3); 1"));
		});
		test("missing_arguments_is_a_parse_error", [this]() {
			check_eq(std::string("error: Parse error: <script>:1:27: Expected '(' after make_shared<T>"), outcome("auto a = make_shared<Vec>; 1"));
		});
		test("user_function_named_make_shared_stays_callable", [this]() {
			check_eq(std::string("6"), outcome("int make_shared(int v) { return v * 2; } make_shared(3)"));
			check_eq(std::string("5"), outcome("int make_shared(int v) { return v; } auto a = make_shared<Vec>(5); a.x"));
		});
	}
};

}

using make_shared_tests = jai::foundry::tests::make_shared_tests;
FOUNDRY_REGISTER(make_shared_tests)
