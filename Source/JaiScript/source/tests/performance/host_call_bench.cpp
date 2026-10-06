// Host-call cost cases: ns per call over the bare loop on both backends, best of 7 runs of a 200000-iteration typed loop.
// The rows split a host call into its parts: a plain script_function global (the call mechanics), the binder's
// conversions without an overload set, a typed add_function (one overload), a real overload set, a statement-position
// call, a host method on a bound class, and the math adapters' vector argument and result. A before/after of a host-call
// change is one run of this suite: compare the printed HOSTCALL rows.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/math/adapter.hpp>
#include <chrono>
#include <cstdio>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace host_call_bench_hosts {
	struct counter {
		double total = 0;
		double add(double k) { total += k; return total; }
	};
}

namespace jai::foundry::tests {

class host_call_bench : public suite {
public:
	host_call_bench() : suite("Host Call Bench") {}

	static std::shared_ptr<jai::engine> host(bool use_vm) {
		auto e = jai::engine::make();
		e->set_backend(use_vm ? jai::backend_type::vm : jai::backend_type::interpreter);
		e->execution_budget(0);
		jai::stdlib::register_all(e);
		engine* eng = e.get();
		e->add_functionWithArity("fraw", [eng](const std::vector<script_value>& a) -> checked_result<script_value> {
			return script_value(a[0].unchecked_as_float() + 1.0, eng);
		}, 1);
		e->add_functionWithArity("fbound", make_functionBinder([](double k) { return k + 1.0; }, eng).bind(), 1);
		e->add_function("fs", [](double k) { return k + 1.0; });
		e->add_function("fs2", [](double a, double b) { return a + b; });
		e->add_function("fset", [](double k) { return k + 1.0; });
		e->add_function("fset", [](const std::string& s) { return static_cast<double>(s.size()); });
		e->add_function("fv", [](jai::vec3 p) { return static_cast<double>(p.x + p.y + p.z); });
		e->add_function("vf", [](double k) { return jai::vec3{static_cast<float>(k), 2.0f, 3.0f}; });
		dynamic_binder<host_call_bench_hosts::counter>(*e, "Counter").constructor<>().method("add", &host_call_bench_hosts::counter::add).build();
		e->execute("function sfs(float k) -> float { return k + 1.0; }");
		return e;
	}

	void forge_tests() override {
		test("host_call_costs", [this]() {
			const std::pair<const char*, const char*> rows[] = {
				{"loop", "s += k;"},
				{"plain_fn", "s += fraw(k);"},
				{"binder_only", "s += fbound(k);"},
				{"typed_one_overload", "s += fs(k);"},
				{"typed_two_args", "s += fs2(k, k);"},
				{"typed_overload_set", "s += fset(k);"},
				{"typed_statement", "fs(k);"},
				{"bound_method", "s += c.add(k);"},
				{"vec_arg", "s += fv(p);"},
				{"vec_result", "s += vf(k).x;"},
				{"script_fn", "s += sfs(k);"},
			};
			for (bool use_vm : {false, true}) {
				auto e = host(use_vm);
				double loop_ns = 0;
				for (const auto& [name, body] : rows) {
					const std::string src = std::string("function f(int n) -> float { math::vec3 p = math::vec3(1, 2, 3); var c = Counter(); float s = 0.0; float k = 1.5; "
					                                    "for (int i = 0; i < n; i++) { ") + body + " } return s; } f(200000);";
					double best = 1e30;
					for (int r = 0; r < 7; ++r) {
						const auto t0 = std::chrono::steady_clock::now();
						e->execute(src);
						const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / 200000.0;
						if (ns < best) best = ns;
					}
					if (std::string(name) == "loop") loop_ns = best;
					std::printf("HOSTCALL %s %-20s %7.1f ns/iter %7.1f over the loop\n", use_vm ? "vm    " : "interp", name, best, best - loop_ns);
				}
			}
			check(true, "timings printed");
		});
	}
};

} // namespace jai::foundry::tests

using host_call_bench = jai::foundry::tests::host_call_bench;
FOUNDRY_REGISTER(host_call_bench)
