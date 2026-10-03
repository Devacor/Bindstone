// Spec oracles for the robustness pass: every expected value comes from the specification (CLAUDE.md, docs/, C++ where
// the docs name it), never from a backend. Every row runs on the interpreter and the VM, proofs on and off, VM passes off,
// the copy elisions off, and with injected cell misses when the build carries the lever.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

namespace robustness {

struct run_config {
	bool vm = false;
	bool proofs = true;
	bool passes = true;
	bool alias = true;
	uint32_t miss = 0;
	const char* label = "";
};

inline std::vector<run_config> configs() {
	std::vector<run_config> out = {
		{false, true, true, true, 0, "interp"},
		{false, false, true, true, 0, "interp proofs off"},
		{false, true, true, false, 0, "interp alias off"},
		{true, true, true, true, 0, "vm"},
		{true, false, true, true, 0, "vm proofs off"},
		{true, true, false, true, 0, "vm passes off"},
		{true, true, true, false, 0, "vm alias off"},
	};
	if (engine::miss_injection_available()) {
		out.push_back({true, true, true, true, 1, "vm miss 1"});
		out.push_back({true, true, true, true, 3, "vm miss 3"});
	}
	return out;
}

inline std::shared_ptr<engine> make(const run_config& c, check_mode mode) {
	auto e = engine::make();
	if (c.vm) { e->set_backend(backend_type::vm); }
	stdlib::register_all(*e);
	e->static_checking(mode);
	if (!c.proofs) { e->type_proofs(false); }
	if (!c.passes) { e->ir_passes(false); }
	if (!c.alias) { e->parameter_alias(false); e->local_alias(false); }
	if (c.miss) { e->cells_miss_every(c.miss); }
	return e;
}

inline std::string render(const script_value& v) {
	if (v.is_string()) { return v.as_string(); }
	if (v.is_null()) { return "<null>"; }
	return "<" + v.to_string() + ">";
}

} // namespace robustness

class robustness_oracle_tests : public suite {
public:
	robustness_oracle_tests() : suite("Robustness Oracles") {}

	void expect(const std::string& label, const std::string& src, const std::string& expected, check_mode mode = check_mode::strict) {
		for (const auto& c : robustness::configs()) {
			auto e = robustness::make(c, mode);
			std::string got;
			try { got = robustness::render(e->execute(src)); }
			catch (const std::exception& ex) { got = std::string("throw:") + ex.what(); }
			check_eq(expected, got, label + " [" + c.label + "]");
		}
	}

	void forge_tests() override {
		destructors();
		sorting();
	}

private:
	// C++ [class.dtor]: a class without a user destructor has an implicit one that destroys its bases
	void destructors() {
		test("a_derived_class_without_a_destructor_runs_its_bases_destructors", [this]() {
			expect("implicit destructor", R"(
				string log = "";
				class A { ~A() { log += "~A"; } }
				class B : A { }
				class C : A { ~C() { log += "~C"; } }
				class E : A { E() { } }
				class F : B { int k = 0; }
				{ A a; } log += "|";
				{ B b; } log += "|";
				{ B b = B(); } log += "|";
				{ C c; } log += "|";
				{ E e; } log += "|";
				{ F f; } log += "|";
				{ auto p = new B(); } log += "|";
				function scoped() { B b; B copy = b; } scoped(); log += "|";
				log;
			)", "~A|~A|~A|~C~A|~A|~A|~A|~A~A|");
		});
	}

	// docs/site/reference/api.html: sort(predicate) orders by the predicate, equal items may be reordered; a predicate that
	// answers inconsistently still leaves a permutation of the elements (never a crash or a lost element)
	void sorting() {
		test("a_predicate_writing_the_elements_it_compares_leaves_a_permutation", [this]() {
			expect("writing predicate", R"(
				class T { int v = 0; T(int k) { v = k; } }
				function run() -> string {
					array<T> la = [];
					for (int i = 0; i < 40; i++) { la.push(T(i % 9)); }
					int calls = 0;
					la.sort((a, b) => { calls++; la[calls % 40].v = (calls * 7) % 13; return a.v < b.v; });
					return "" + la.size();
				}
				function run2() -> string {
					array<T> la = [T(1), T(2)];
					la.sort((a, b) => { la[0].v = 7; return a.v < b.v; });
					return "" + la.size();
				}
				run() + "|" + run2();
			)", "40|2");
		});
		test("a_stateful_predicate_leaves_the_same_multiset", [this]() {
			expect("stateful predicate", R"(
				function run() -> string {
					array<int> xs = [];
					for (int i = 0; i < 300; i++) { xs.push((i * 37) % 17); }
					int sum = 0; for (auto x : xs) { sum += x; }
					int n = 0;
					xs.sort((a, b) => { n++; return (n * 7919) % 5 < 2; });
					int after = 0; for (auto x : xs) { after += x; }
					array<int> ys = array::sort(xs, (a, b) => { n++; return n % 3 == 0; });
					int copied = 0; for (auto y : ys) { copied += y; }
					return "" + xs.size() + "," + (sum == after) + "," + ys.size() + "," + (sum == copied);
				}
				run();
			)", "300,true,300,true");
		});
	}
};

} // namespace jai::foundry::tests

using robustness_oracle_tests = jai::foundry::tests::robustness_oracle_tests;
FOUNDRY_REGISTER(robustness_oracle_tests)
