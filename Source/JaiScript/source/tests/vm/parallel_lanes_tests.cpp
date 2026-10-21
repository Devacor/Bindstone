// The serial vm's fast paths run on workers through each worker's own rows of the shared chunks: call-site callees, inline
// guards, method grafts, constructors entered in the loop, this-field stores in methods. Each case holds both backends to one
// result and to the serial loop's, across regions and across a redefinition between them.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class parallel_lanes_tests : public suite {
public:
	parallel_lanes_tests() : suite("Parallel Lanes") {}

	static std::string run(bool vm, size_t workers, const std::vector<std::string>& steps) {
		auto e = engine::make();
		e->set_backend(vm ? backend_type::vm : backend_type::interpreter);
		stdlib::register_all(*e);
		e->parallel_thread_count(workers);
		std::string result;
		for (const auto& step : steps) {
			try { result += e->execute(step).to_string() + "|"; } catch (const std::exception& ex) { result += std::string("throw:") + ex.what() + "|"; }
		}
		return result;
	}

	// each step's value on both backends with 1 and 4 workers: one answer everywhere, and it is the expected one when given
	void same_everywhere(const std::vector<std::string>& steps, const std::string& expected, const std::string& what) {
		const std::string reference = run(false, 1, steps);
		if (!expected.empty()) { check_eq(expected, reference, what + ": the value"); }
		for (size_t workers : { size_t(1), size_t(4) }) {
			for (bool vm : { false, true }) {
				check_eq(reference, run(vm, workers, steps), what + (vm ? " (vm, " : " (interpreter, ") + std::to_string(workers) + " workers)");
			}
		}
	}

	void forge_tests() override {
		test("a_workers_call_rows_follow_a_redefinition_between_regions", [this]() {
			same_everywhere({ R"JAI(
function sq(int x) -> int { return x * x; }
function cube(int x) -> int { return x * x * x; }
function body(var& v) -> void { int acc = 0; for (int k = 0; k < 6; ++k) { if (v % 3 == 0) { acc = acc + cube(k + v % 5); } else { acc = acc + sq(k + v % 5); } } v = acc; }
function serial(int n) -> string { array<int> xs = []; for (int i = 0; i < n; ++i) { xs.push(i); } for (auto& v : xs) { body(v); } return to_string(xs); }
function par(int n) -> string { array<int> xs = []; for (int i = 0; i < n; ++i) { xs.push(i); } parallel_for (auto& v : xs) { body(v); } return to_string(xs); }
0;)JAI", "par(300) == serial(300);", "par(300) == serial(300);", "function sq(int x) -> int { return x + 1000; } 0;", "par(300) == serial(300);" },
				"0|true|true|0|true|", "callee rows");
		});

		test("a_workers_method_graft_follows_a_class_reload_between_regions", [this]() {
			same_everywhere({ R"JAI(
class V { float x = 0.0; float y = 0.0; function len2() -> float { return x * x + y * y; } }
function fill(int n) -> array<V> { array<V> vs = []; for (int i = 0; i < n; ++i) { V v = V(); v.x = i % 7; v.y = i % 5; vs.push(v); } return vs; }
function serial(int n) -> float { array<V> vs = fill(n); float s = 0.0; for (auto& v : vs) { for (int k = 0; k < 4; ++k) { v.x = v.x + v.len2() * 0.001; } s = s + v.x; } return s; }
function par(int n) -> float { array<V> vs = fill(n); parallel_for (auto& v : vs) { for (int k = 0; k < 4; ++k) { v.x = v.x + v.len2() * 0.001; } } float s = 0.0; for (auto& v : vs) { s = s + v.x; } return s; }
0;)JAI", "par(400) == serial(400);", "par(400) == serial(400);",
				"class V { float x = 0.0; float y = 0.0; function len2() -> float { return x - y; } } 0;", "par(400) == serial(400);", "par(400) == serial(400);" },
				"0|true|true|0|true|true|", "method grafts");
		});

		test("constructions_in_a_body_enter_their_constructors_in_the_loop", [this]() {
			same_everywhere({ R"JAI(
class P { float x = 0.0; float y = 1.5; }
class Q { float a; float b; int made = 7; Q(float a0, float b0) { a = a0; b = b0 * 2.0; } function len2() -> float { return a * a + b * b; } }
class R { Q q; int k = 0; R(int n) { q = Q(n, n + 1); k = n; } }
function body(var& v) -> void {
	float s = 0.0;
	for (int k = 0; k < 6; ++k) { P p = P(); p.x = k; Q q = Q(p.x, p.y + v % 3); R r = R(k + v % 4); s = s + q.len2() + r.q.b + r.k + q.made; }
	v = math::itrunc(s);
}
function serial(int n) -> string { array<int> xs = []; for (int i = 0; i < n; ++i) { xs.push(i); } for (auto& v : xs) { body(v); } return to_string(xs); }
function par(int n) -> string { array<int> xs = []; for (int i = 0; i < n; ++i) { xs.push(i); } parallel_for (auto& v : xs) { body(v); } return to_string(xs); }
0;)JAI", "par(200) == serial(200);", "par(5);" },
				"", "constructors");
		});

		test("method_field_stores_run_on_workers_as_on_the_engine", [this]() {
			same_everywhere({ R"JAI(
class Body {
	float x = 0.0; float v = 1.0; int hits = 0; int tag = 0; int last = 0;
	function step(float dt) -> void { x = x + v * dt; if (x > 1.0) { x = x - 1.0; hits = hits + 1; tag = hits * 2; } v = v + 1; }
	function run(int n) -> void { for (int k = 0; k < n; ++k) { this.step(0.01); } last = hits + n; }
}
function fill(int n) -> array<Body> { array<Body> bs = []; for (int i = 0; i < n; ++i) { Body b = Body(); b.v = (i % 17) * 0.3; bs.push(b); } return bs; }
function sum(var& bs) -> string { int h = 0; float xs = 0.0; int tags = 0; int t = 0; for (auto& b : bs) { h = h + b.hits; xs = xs + b.x; tags = tags + b.tag; t = t + b.last; } return to_string(h) + " " + to_string(xs) + " " + to_string(tags) + " " + to_string(t); }
function serial(int n) -> string { array<Body> bs = fill(n); for (auto& b : bs) { b.run(25); } return sum(bs); }
function par(int n) -> string { array<Body> bs = fill(n); parallel_for (auto& b : bs) { b.run(25); } return sum(bs); }
0;)JAI", "par(300) == serial(300);", "par(300) == serial(300);" },
				"0|true|true|", "field stores");
		});
	}
};

}

using parallel_lanes_tests = jai::foundry::tests::parallel_lanes_tests;
FOUNDRY_REGISTER(parallel_lanes_tests)
