// `T(args)` calls a script class's constructor by the class's name. The vm caches the constructor's value at the call site and
// constructs through it (vm_backend::construct_at_site) while the name still binds that value; each case runs its calls in a loop,
// so the cached path runs, and holds both backends to one result: construction order, copies, destructor timing, a constructor
// that throws, a name bound to another class and a class declared again.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>
#include <vector>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class class_name_call_tests : public suite {
public:
	class_name_call_tests() : suite("Class Name Calls") {}

	static std::string run_on(bool vm, const std::string& script, size_t workers = 0) {
		auto e = jai::foundry::make_default_engine();
		e->set_backend(vm ? backend_type::vm : backend_type::interpreter);
		stdlib::register_all(*e);
		e->execution_budget(0);
		if (workers) { e->parallel_thread_count(workers); }
		try { return e->execute(script).to_string(); } catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
	}

	void both(const std::string& script, const std::string& expected, const std::string& what, size_t workers = 0) {
		const std::string interp = run_on(false, script, workers), vm = run_on(true, script, workers);
		check_eq(interp, vm, what + ": both backends");
		if (!expected.empty()) { check_eq(expected, vm, what + ": the value"); }
		check(vm.rfind("throw:", 0) != 0, what + ": runs (" + vm + ")");
	}

	void forge_tests() override {
		test("a_class_without_a_constructor_is_made_fresh_each_call", [this]() {
			both(R"JAI(
class P { int a = 1; int b = 2; }
function run() -> string {
	int s = 0;
	for (int i = 0; i < 500; ++i) { P q = P(); q.a = q.a + i; s += q.a + q.b; }
	auto kept = P();
	kept.b = 9;
	P other = P();
	return to_string(s) + " " + to_string(kept.b) + " " + to_string(other.b);
}
run()
)JAI", "126250 9 2", "no constructor");
		});

		test("constructors_run_base_first_with_their_initializer_lists", [this]() {
			both(R"JAI(
string LOG = "";
class A { int a = 0; A(int x) : a(x) { LOG += "A" + to_string(a) + " "; } }
class B : A { int b = 0; int c = 0; B(int x, int y) : A(x), b(y), c(b + 1) { LOG += "B" + to_string(b) + to_string(c) + " "; } }
class D { int v = 7; D() { LOG += "D "; } D(int x) { v = x; LOG += "D" + to_string(x) + " "; } }
function run() -> string {
	int s = 0;
	for (int i = 0; i < 3; ++i) { B q = B(i, i * 2); s += q.a + q.b + q.c; D d = D(); D e = D(i); s += d.v + e.v; }
	return LOG + "| " + to_string(s);
}
run()
)JAI", "A0 B01 D D0 A1 B23 D D1 A2 B45 D D2 | 42", "order");
		});

		test("one_argument_of_the_class_copies_and_a_temporary_is_the_object", [this]() {
			both(R"JAI(
string LOG = "";
class T {
	int v = 0;
	T(int x) : v(x) { LOG += "make" + to_string(x) + " "; }
	T(const T& o) : v(o.v + 100) { LOG += "copy" + to_string(o.v) + " "; }
}
function run() -> string {
	int s = 0;
	for (int i = 0; i < 3; ++i) {
		T a = T(i);
		T b = T(a);
		T c = T(T(i + 10));
		s += a.v + b.v + c.v;
	}
	return LOG + "| " + to_string(s);
}
run()
)JAI", "make0 copy0 make10 make1 copy1 make11 make2 copy2 make12 | 339", "copies");
		});

		test("objects_die_at_their_scope_and_temporaries_at_their_statement", [this]() {
			both(R"JAI(
string LOG = "";
class T { int v = 0; T(int x) : v(x) { LOG += "+" + to_string(x); } ~T() { LOG += "-" + to_string(v); } int twice() { return v * 2; } }
function take(T t) -> int { return t.v; }
function make(int x) -> T { return T(x); }
function run() -> string {
	int s = 0;
	for (int i = 0; i < 3; ++i) {
		T a = T(i);
		s += T(i + 10).twice();
		LOG += ".";
		s += take(T(i + 20));
		T b = make(i + 30);
		LOG += ";";
	}
	return LOG + " | " + to_string(s);
}
run()
)JAI", "", "lifetimes");
		});

		test("a_constructor_that_throws_leaves_no_object_and_the_site_constructs_again", [this]() {
			both(R"JAI(
string LOG = "";
class Part { int id = 0; Part(int x) : id(x) { LOG += "p" + to_string(x); } ~Part() { LOG += "~p" + to_string(id); } }
class Whole {
	Part first;
	Part second;
	Whole(int x) : first(Part(x)), second(Part(x + 1)) { if (x % 2 == 1) { throw "odd " + to_string(x); } LOG += "w" + to_string(x); }
	~Whole() { LOG += "~w"; }
}
function run() -> string {
	int made = 0;
	for (int i = 0; i < 4; ++i) {
		try { Whole w = Whole(i); made += 1; LOG += "."; } catch (e) { LOG += "!" + to_string(e) + " "; }
	}
	return LOG + " | " + to_string(made);
}
run()
)JAI", "", "a throwing constructor");
		});

		test("a_name_bound_to_another_classs_constructor_constructs_that_class", [this]() {
			both(R"JAI(
class P { int v = 1; }
class Q { int v = 0; Q() { v = 50; } }
var make = P;
function swap() { make = Q; return 0; }
function run() -> string {
	int s = 0;
	for (int i = 0; i < 400; ++i) { auto o = make(); s += o.v; if (i == 199) { swap(); } }
	return to_string(s);
}
run()
)JAI", "10200", "a rebound name");
		});

		test("a_class_declared_again_constructs_its_new_declaration", [this]() {
			for (bool vm : { false, true }) {
				auto e = jai::foundry::make_default_engine();
				e->set_backend(vm ? backend_type::vm : backend_type::interpreter);
				stdlib::register_all(*e);
				e->execution_budget(0);
				e->disable(jai::permissions::filesystem);
				const std::string path = "D:/virtual/class_name_calls/unit.jai";
				const std::string tag = vm ? "vm" : "interpreter";
				std::vector<std::string> got;
				try {
					e->execute_file_source(path, "class T { int v = 1; T(int x) { v = v + x; } }\nfunction total(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { T t = T(i); s += t.v; } return s; }");
					got.push_back(e->execute("total(100)").to_string());
					e->execute_file_source(path, "class T { int v = 1000; int w = 5; T(int x) { v = v + x + w; } }\nfunction total(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { T t = T(i); s += t.v; } return s; }");
					got.push_back(e->execute("total(100)").to_string());
				} catch (const std::exception& ex) { got.push_back(std::string("throw:") + ex.what()); }
				check_eq((size_t)2, got.size(), tag + ": both runs complete" + (got.empty() ? std::string() : " (" + got.back() + ")"));
				if (got.size() == 2) {
					check_eq(std::string("5050"), got[0], tag + ": the first declaration");
					check_eq(std::string("105450"), got[1], tag + ": the declaration that replaced it");
				}
			}
		});

		test("a_constructor_that_constructs_its_own_class_reenters_the_site", [this]() {
			both(R"JAI(
int COUNT = 0;
class Node {
	int depth = 0;
	int below = 0;
	Node(int d) : depth(d) { COUNT += 1; if (d > 0) { Node child = Node(d - 1); below = child.below + child.depth; } }
}
function run() -> string {
	int s = 0;
	for (int i = 0; i < 40; ++i) { Node n = Node(i % 6); s += n.below; }
	return to_string(s) + " " + to_string(COUNT);
}
run()
)JAI", "", "recursion");
		});

		test("a_class_a_block_declares_constructs_and_the_blocks_values_die_at_its_end", [this]() {
			both(R"JAI(
string LOG = "";
class Tracer { int id = 0; Tracer(int x) : id(x) { LOG += "+" + to_string(x); } ~Tracer() { LOG += "-" + to_string(id); } }
for (int round = 0; round < 3; ++round) {
	Tracer outer = Tracer(round);
	class Blocky { int v = 5; Blocky(int x) { v = x; } }
	for (int i = 0; i < 3; ++i) { Blocky q = Blocky(i); LOG += to_string(q.v); }
	LOG += ".";
}
namespace game {
	class Unit { int hp = 3; Unit(int x) { hp = x; } }
	function spawn(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { Unit u = Unit(i); s += u.hp; } return s; }
}
LOG + "|" + to_string(game::spawn(50))
)JAI", "+0012.-0+1012.-1+2012.-2|1225", "a block's class");
		});

		test("a_parallel_body_constructs_by_name", [this]() {
			for (size_t workers : { size_t(1), size_t(4) }) {
				both(R"JAI(
class V { float x = 0.0; float y = 0.0; V(float a, float b) : x(a), y(b) {} float dot(V o) { return x * o.x + y * o.y; } }
function work(int i) -> float { float s = 0.0; for (int k = 0; k < 20; ++k) { V a = V(i * 1.0, k * 1.0); V b = V(k * 0.5, 2.0); s += a.dot(b); } return s; }
array<int> xs = [];
for (int i = 0; i < 48; ++i) { xs.push(i); }
var out = parallel_transform(xs, work);
float total = 0.0;
for (auto v : out) { total += v; }
float serial = 0.0;
for (int x : xs) { serial += work(x); }
to_string(total == serial) + " " + to_string(total)
)JAI", "", "a parallel body (" + std::to_string(workers) + " workers)", workers);
			}
		});
	}
};

}

using class_name_call_tests = jai::foundry::tests::class_name_call_tests;
FOUNDRY_REGISTER(class_name_call_tests)
