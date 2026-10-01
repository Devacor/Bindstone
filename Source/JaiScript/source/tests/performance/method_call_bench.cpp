// Method-call cost cases: ns per call over the bare loop on both backends, best of 7 runs of a 200000-iteration typed loop.
// The rows split a method call by receiver and callee: a script class's method through a var, a typed local, a handle, an
// inherited and an overridden method, a method that touches no field, a sibling call and this.m() inside a method, a static
// method, and a host class's bound method (var and typed receivers, no arguments). A before/after of a method-call change is
// one run of this suite: compare the printed METHODCALL rows.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <chrono>
#include <cstdio>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace method_call_bench_hosts {
	struct tally {
		double total = 0;
		double add(double k) { total += k; return total; }
		double get() const { return total; }
	};
}

namespace jai::foundry::tests {

class method_call_bench : public suite {
public:
	method_call_bench() : suite("Method Call Bench") {}

	static std::shared_ptr<jai::engine> host(bool use_vm) {
		auto e = jai::engine::make();
		if (use_vm) { e->set_backend(jai::backend_type::vm); }
		e->execution_budget(0);
		jai::stdlib::register_all(e);
		dynamic_binder<method_call_bench_hosts::tally>(*e, "Tally").constructor<>()
			.method("add", &method_call_bench_hosts::tally::add).method("get", &method_call_bench_hosts::tally::get).build();
		e->execute(
			"class S { float v = 0.0; "
			"  float add(float k) { v += k; return v; } "
			"  float idf(float k) { return k + 1.0; } "
			"  float twice(float k) { return k * 2.0; } "
			"  static float sadd(float k) { return k + 1.0; } "
			"  float run_sibling(int n, float k) { float s = 0.0; for (int i = 0; i < n; i++) { s += idf(k); } return s; } "
			"  float run_this(int n, float k) { float s = 0.0; for (int i = 0; i < n; i++) { s += this.idf(k); } return s; } "
			"  float run_loop(int n, float k) { float s = 0.0; for (int i = 0; i < n; i++) { s += k; } return s; } "
			"} "
			"class D : S { override float twice(float k) { return k * 3.0; } } "
			"function sfs(float k) -> float { return k + 1.0; }");
		return e;
	}

	void forge_tests() override {
		test("method_call_costs", [this]() {
			struct row { const char* name; const char* decl; const char* body; };
			const row rows[] = {
				{"loop", "", "s += k;"},
				{"script_fn", "", "s += sfs(k);"},
				{"script_method_var", "var o = S();", "s += o.idf(k);"},
				{"script_method_typed", "S o = S();", "s += o.idf(k);"},
				{"script_method_field", "S o = S();", "s += o.add(k);"},
				{"script_method_handle", "shared_ptr<S> o = new S();", "s += o.idf(k);"},
				{"script_inherited", "D o = D();", "s += o.idf(k);"},
				{"script_overridden", "S o = D();", "s += o.twice(k);"},
				{"script_static", "", "s += S::sadd(k);"},
				{"host_method_var", "var o = Tally();", "s += o.add(k);"},
				{"host_method_typed", "Tally o = Tally();", "s += o.add(k);"},
				{"host_method_noargs", "Tally o = Tally();", "s += o.get();"},
			};
			// rows whose loop runs inside a method (the call is a sibling call or this.m())
			const row in_method[] = {
				{"in_method_loop", "", "run_loop"},
				{"sibling_call", "", "run_sibling"},
				{"this_call", "", "run_this"},
			};
			for (bool use_vm : {false, true}) {
				auto e = host(use_vm);
				auto time = [&](const std::string& src) {
					double best = 1e30;
					for (int r = 0; r < 7; ++r) {
						const auto t0 = std::chrono::steady_clock::now();
						e->execute(src);
						const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / 200000.0;
						if (ns < best) best = ns;
					}
					return best;
				};
				double loop_ns = 0;
				for (const auto& r : rows) {
					const double ns = time(std::string("function f(int n) -> float { ") + r.decl + " float s = 0.0; float k = 1.5; for (int i = 0; i < n; i++) { " + r.body + " } return s; } f(200000);");
					if (std::string(r.name) == "loop") loop_ns = ns;
					std::printf("METHODCALL %s %-22s %7.1f ns/iter %7.1f over the loop\n", use_vm ? "vm    " : "interp", r.name, ns, ns - loop_ns);
				}
				double method_loop_ns = 0;
				for (const auto& r : in_method) {
					const double ns = time(std::string("S o = S(); o.") + r.body + "(200000, 1.5);");
					if (std::string(r.name) == "in_method_loop") method_loop_ns = ns;
					std::printf("METHODCALL %s %-22s %7.1f ns/iter %7.1f over the loop\n", use_vm ? "vm    " : "interp", r.name, ns, ns - method_loop_ns);
				}
			}
			check(true, "timings printed");
		});
	}
};

} // namespace jai::foundry::tests

using method_call_bench = jai::foundry::tests::method_call_bench;
FOUNDRY_REGISTER(method_call_bench)
