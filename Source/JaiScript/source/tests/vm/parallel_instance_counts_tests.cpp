// Class instances workers reach while a parallel region runs: element objects read, called, built and landed in outputs, and one
// C++ object (its wrapper, its script layer) every worker wraps through a host call. Each case runs many regions on 8 workers and
// holds the result to the serial loop's.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/core/script_extensible.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <functional>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace {
struct shared_probe : jai::script_extensible {
	virtual ~shared_probe() = default;
	int64_t v = 5;
	int64_t read() const { return v; }
};
}

namespace jai::foundry::tests {

class parallel_instance_counts_tests : public suite {
public:
	parallel_instance_counts_tests() : suite("Parallel Instance Counts") {}

	static std::string run(const std::string& program, size_t workers, const std::function<void(engine&)>& bind = {}) {
		auto e = make_engine();
		stdlib::register_all(*e);
		if (bind) { bind(*e); }
		e->parallel_thread_count(workers);
		e->execution_budget(0);
		try { return e->execute(program).to_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
	}

	void forge_tests() override {
		test("element_objects_read_call_build_and_return_through_many_regions", [this]() {
			// a zipped region reads each element's fields, runs its methods, builds and drops temporaries and lands a new object
			// in its output element; 150 regions of 512 elements, the serial loop's checksum on 1 and 8 workers
			const std::string defs = R"JAI(
class Vec { int x = 0; int y = 0; Vec(int a, int b) { x = a; y = b; } int dot(const Vec& o) { return x * o.x + y * o.y; } }
class Body {
	int id = 0; Vec p = Vec(0, 0); Vec v = Vec(1, 2);
	int step() { Vec t = Vec(p.x + v.x, p.y + v.y); p = t; if (p.x > 1000) { p = Vec(0, 0); } return p.dot(v) + id; }
}
function make_bodies(int n) -> array<Body> { array<Body> bs = []; for (int i = 0; i < n; ++i) { Body b = Body(); b.id = i; b.v = Vec(i % 7, i % 5); bs.push(b); } return bs; }
function make_outs(int n) -> array<Vec> { array<Vec> os = []; for (int i = 0; i < n; ++i) { os.push(Vec(0, 0)); } return os; }
function sum(var& os) -> int { int s = 0; for (auto& o : os) { s = (s * 31 + o.x + o.y) % 1000000007; } return s; }
)JAI";
			const std::string serial = defs + R"JAI(
array<Body> bodies = make_bodies(512); array<Vec> outs = make_outs(512);
for (int r = 0; r < 150; ++r) { for (int i = 0; i < 512; ++i) { int s = bodies[i].step(); Vec made = Vec(s, bodies[i].id); outs[i] = made; } }
sum(outs);
)JAI";
			const std::string parallel = defs + R"JAI(
array<Body> bodies = make_bodies(512); array<Vec> outs = make_outs(512);
for (int r = 0; r < 150; ++r) { parallel_for (auto& b, auto& o : bodies, outs) { int s = b.step(); Vec made = Vec(s, b.id); o = made; } }
sum(outs);
)JAI";
			const std::string expected = run(serial, 1);
			check(expected.rfind("throw:", 0) != 0, "the serial loop runs: " + expected);
			for (size_t workers : { size_t(1), size_t(8) }) {
				check_eq(expected, run(parallel, workers), "zipped objects on " + std::to_string(workers) + " workers");
			}
		});

		test("a_script_layer_every_worker_reaches_through_a_host_call", [this]() {
			// every worker wraps the same C++ object's script layer, over and over: the layer's counts and its holder room are shared
			// a fresh object per engine: an object keeps its layer past its engine, so one object across engines tests something else
			auto bind = [](engine& e) {
				auto object = std::make_shared<shared_probe>();
				dynamic_binder<shared_probe>(e, "Probe").constructor<>().method("read", &shared_probe::read).build();
				e.add_function("probe", [object]() { return object; });
			};
			const std::string program = R"JAI(
class Layered : Probe { int extra = 2; Layered() : super() {} }
var held = probe();
extend(held, Layered);
var warm = probe();   // the wrap names shared_ptr<Layered> once before a region (a region interns no new name)
array<int> xs = []; array<int> out = [];
for (int i = 0; i < 256; ++i) { xs.push(i); out.push(0); }
int total = 0;
for (int r = 0; r < 300; ++r) {
	parallel_for (int x, auto& o : xs, out) { var p = probe(); var q = probe(); o = 5 + q.extra + x; }
	for (int v : out) { total = (total + v) % 1000000007; }
}
total;
)JAI";
			int64_t expected = 0;
			for (int r = 0; r < 300; ++r) { for (int64_t x = 0; x < 256; ++x) { expected = (expected + 5 + 2 + x) % 1000000007; } }
			for (size_t workers : { size_t(1), size_t(8) }) {
				check_eq(std::to_string(expected), run(program, workers, bind), "a shared layer on " + std::to_string(workers) + " workers");
			}
		});

		test("a_bound_object_every_worker_wraps_again_through_a_host_call", [this]() {
			// a C++ object the script already wrapped, returned by a host call on every worker: each wrap finds the one wrapper and
			// reads its native holder (a copy of that holder raced its count across workers)
			// a fresh object per engine: an object keeps its layer past its engine, so one object across engines tests something else
			auto bind = [](engine& e) {
				auto object = std::make_shared<shared_probe>();
				dynamic_binder<shared_probe>(e, "Probe").constructor<>().method("read", &shared_probe::read).build();
				e.add_function("probe", [object]() { return object; });
			};
			const std::string program = "var warm = probe(); array<int> xs = []; array<int> out = []; for (int i = 0; i < 256; ++i) { xs.push(i); out.push(0); } "
			                            "for (int r = 0; r < 300; ++r) { parallel_for (int x, auto& o : xs, out) { var p = probe(); var q = p; o = x; } } out[5] + out[255];";
			for (size_t workers : { size_t(1), size_t(8) }) {
				check_eq(std::string("260"), run(program, workers, bind), "rewrapped on " + std::to_string(workers) + " workers");
			}
		});

		test("objects_a_region_returns_outlive_it_and_die_on_the_calling_thread", [this]() {
			// a region builds objects (with nested objects and strings) into its outputs; they are the caller's after the join, copied,
			// dropped and rebuilt across 200 regions; the class declares a destructor, so every death runs the chain
			const std::string program = R"JAI(
class Tag { string name = ""; Tag(string n) { name = n; } }
class Box { int n = 0; Tag tag = Tag("t"); Box(int k) { n = k; tag = Tag("box" + to_string(k % 9)); } ~Box() { } }
array<int> xs = []; array<Box> out = [];
for (int i = 0; i < 300; ++i) { xs.push(i); out.push(Box(0)); }
int total = 0;
for (int r = 0; r < 200; ++r) {
	parallel_for (int x, auto& o : xs, out) { Box b = Box(x + r); Box c = b; o = c; }
	array<Box> kept = out;
	for (auto& k : kept) { total = (total + k.n + k.tag.name.size()) % 1000000007; }
}
total;
)JAI";
			int64_t expected = 0;
			for (int r = 0; r < 200; ++r) { for (int64_t x = 0; x < 300; ++x) { expected = (expected + x + r + 4) % 1000000007; } }
			for (size_t workers : { size_t(1), size_t(8) }) {
				check_eq(std::to_string(expected), run(program, workers), "returned objects on " + std::to_string(workers) + " workers");
			}
		});
	}
};

}

using parallel_instance_counts_tests = jai::foundry::tests::parallel_instance_counts_tests;
FOUNDRY_REGISTER(parallel_instance_counts_tests)
