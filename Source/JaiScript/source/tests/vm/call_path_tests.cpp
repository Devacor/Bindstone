// The call path's exits: a frame pops what it opened (a try, a range-for, a counted loop, a parked callee) on every way out (a return,
// a throw through it, a depth limit, a spent budget, a tail call's splice, a coroutine's end), and a destructor that runs while a frame
// pops finds the stacks as its own calls need them. Every shape runs the same on the interpreter, the vm and the vm-reference backend.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/engine_access.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class call_path_tests : public suite {
public:
	call_path_tests() : suite("Call Path") {}

	enum class which { interp, vm, vm_reference };

	static std::shared_ptr<engine> make_for(which w) {
		auto e = jai::foundry::make_default_engine();
		e->set_backend(w == which::interp ? backend_type::interpreter : backend_type::vm);
		if (w == which::vm_reference) { e->vm_reference_mode(true); }
		e->execution_budget(0);
		stdlib::register_all(*e);
		return e;
	}

	static std::string run(which w, const std::string& script) {
		auto e = make_for(w);
		try { return e->execute(script).to_string(); }
		catch (const std::exception& ex) { return std::string("throw:") + ex.what(); }
	}

	void all(const std::string& script, const std::string& expected, const std::string& label) {
		check_eq(expected, run(which::interp, script), label + ": interpreter");
		check_eq(expected, run(which::vm, script), label + ": vm");
		check_eq(expected, run(which::vm_reference, script), label + ": vm-reference");
	}

	void forge_tests() override {
		test("a_throw_through_a_counted_loop_leaves_the_callers_loop", [this]() {
			all(R"(
				function thrower(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += i; if (i == 3) { throw "boom"; } } return s; }
				function outer() -> string {
					string log = "";
					for (int k = 0; k < 4; ++k) {
						try { log += to_string(thrower(k + 2)); } catch (e) { log += "!" + e; }
						log += ",";
					}
					return log;
				}
				outer();
			)", "1,3,!boom,!boom,", "counted loop");
		});

		test("a_throw_through_a_range_for_leaves_the_callers_iteration", [this]() {
			all(R"(
				function walk(array<int> xs, int stop) -> int { int s = 0; for (int x : xs) { if (x == stop) { throw "stop" + to_string(x); } s += x; } return s; }
				function outer() -> string {
					string log = "";
					array<int> stops = [9, 2, 9, 3];
					for (int stop : stops) {
						try { log += to_string(walk([1, 2, 3, 4], stop)); } catch (e) { log += e; }
						log += ",";
					}
					return log;
				}
				outer();
			)", "10,stop2,10,stop3,", "range-for");
		});

		test("a_rethrow_through_a_try_and_parked_callees_unwinds_in_order", [this]() {
			all(R"(
				function inner(int n) -> int { if (n > 2) { throw "deep" + to_string(n); } return n; }
				function middle(int n) -> int {
					int r = 0;
					try { r = inner(n); } catch (e) { throw "re:" + e; }
					return r + 10;
				}
				function add(int a, int b) -> int { return a + b; }
				function outer() -> string {
					string log = "";
					for (int k = 1; k < 5; ++k) {
						try { log += to_string(add(middle(k), add(k, middle(k + 1)))); } catch (e) { log += e; }
						log += ",";
					}
					return log;
				}
				outer();
			)", "24,re:deep3,re:deep3,re:deep4,", "try and parked callees");
		});

		test("a_leaf_method_that_throws_leaves_its_callers_loop_and_try", [this]() {
			all(R"(
				class Acc {
					int total = 0;
					int leaf(int x) { if (x == 3) { throw "leaf3"; } return x * 2; }
					int pick(int a, int b) { return a + b; }
					int run(int n) {
						for (int i = 0; i < n; ++i) {
							try { total += pick(leaf(i), leaf(i + 1)); } catch (e) { total += 1000; }
						}
						return total;
					}
				}
				shared_ptr<Acc> a = new Acc();
				string out = to_string(a.run(5));
				out + "," + to_string(a.run(2));
			)", "2026,2034", "leaf method");
		});

		test("a_depth_limit_at_each_entry_unwinds_to_a_catch_inside_loops", [this]() {
			// a raw typed frame, a lean method frame and a plain frame each run into the cap under a range-for, a counted loop and a try
			const std::string script = R"(
				int grow(int n) { return grow(n + 1) + 1; }
				class R { int deep(int n) { return this.deep(n + 1) + 1; } int ok() { return 7; } }
				function deepfn(n) -> auto { return deepfn(n + 1) + 1; }
				function probe() -> string {
					string log = "";
					shared_ptr<R> r = new R();
					array<int> order = [0, 1, 2, 0, 1, 2];
					for (int which : order) {
						for (int i = 0; i < 2; ++i) {
							try {
								if (which == 0) { log += to_string(grow(0)); }
								if (which == 1) { log += to_string(r.deep(0)); }
								if (which == 2) { log += to_string(deepfn(0)); }
							} catch (e) { log += "E"; }
						}
						log += to_string(r.ok());
					}
					return log;
				}
				probe();
			)";
			for (const which w : {which::interp, which::vm, which::vm_reference}) {
				auto e = make_for(w);
				e->max_call_depth(backend_type::interpreter, 200);
				e->max_call_depth(backend_type::vm, 200);
				check_eq(std::string("EE7EE7EE7EE7EE7EE7"), e->execute(script).to_string(), "depth limit under loops");
			}
		});

		test("a_spent_budget_inside_nested_loops_leaves_the_engine_usable", [this]() {
			const std::string spin = R"(
				class Spinner { int n = 0; int turn(int k) { for (int i = 0; i < k; ++i) { n += i; } return n; } }
				function forever() -> int {
					shared_ptr<Spinner> s = new Spinner();
					int total = 0;
					for (int v : [1, 2, 3]) {
						try { while (true) { total += s.turn(v); } } catch (e) { total = -1; }
					}
					return total;
				}
				forever();
			)";
			const std::string after = R"(
				function thrower(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += i; if (i == 3) { throw "boom"; } } return s; }
				string log = "";
				for (int k = 0; k < 4; ++k) { try { log += to_string(thrower(k + 2)); } catch (e) { log += "!" + e; } log += ","; }
				log;
			)";
			for (const which w : {which::interp, which::vm, which::vm_reference}) {
				auto e = make_for(w);
				e->execution_budget(0.05);
				check_throws([&]() { e->execute(spin); }, "the budget stops the loop");
				e->execution_budget(0);
				check_eq(std::string("1,3,!boom,!boom,"), e->execute(after).to_string(), "the next run starts clean");
			}
		});

		test("a_coroutine_frame_keeps_its_own_loops_and_tries", [this]() {
			all(R"(
				coroutine int gen(int n) {
					for (int i = 0; i < n; ++i) {
						try { if (i == 2) { throw "g2"; } yield i; } catch (e) { yield 100; }
					}
				}
				function sum_gen(int n) -> int { int s = 0; for (int v : gen(n)) { for (int k = 0; k < 2; ++k) { s += v; } } return s; }
				coroutine int bad(int n) { for (int i = 0; i < n; ++i) { if (i == 1) { throw "bad1"; } yield i; } }
				function drain() -> string {
					string log = "";
					for (int round = 0; round < 3; ++round) {
						try { for (int v : bad(3)) { log += to_string(v); } } catch (e) { log += e; }
						log += "|" + to_string(sum_gen(4)) + ";";
					}
					return log;
				}
				drain();
			)", "0bad1|208;0bad1|208;0bad1|208;", "coroutine frames");
		});

		test("a_destructor_that_loops_and_catches_while_a_frame_pops", [this]() {
			// the last handle dies in a popping frame (a local) and at a statement's end (a temporary receiver); the destructor opens a range-for,
			// counted loops and a try of its own
			all(R"(
				string log = "";
				function note(int k) -> int { int s = 0; for (int i = 0; i < k; ++i) { s += i; } try { if (k == 3) { throw "n3"; } } catch (e) { s += 100; } return s; }
				class D {
					int k = 0;
					D(int n) { k = n; }
					~D() { for (int v : [1, 2]) { log += "d" + to_string(k) + ":" + to_string(note(k + v)) + " "; } }
					int get() { return k; }
				}
				function make(int n) -> shared_ptr<D> { return new D(n); }
				function use(int n) -> int { shared_ptr<D> local = new D(n); int s = 0; for (int i = 0; i < 2; ++i) { s += local.get(); } return s; }
				function outer() -> int {
					int s = 0;
					for (int i = 0; i < 3; ++i) { s += make(i).get(); s += use(i + 10); }
					return s;
				}
				to_string(outer()) + " " + log;
			)", "69 d0:0 d0:1 d10:55 d10:66 d1:1 d1:103 d11:66 d11:78 d2:103 d2:6 d12:78 d12:91 ", "destructor during a pop");
		});

		test("a_tail_call_after_a_loop_splices_a_clean_frame", [this]() {
			all(R"(
				function countdown(int n, int acc) -> int {
					for (int i = 0; i < 2; ++i) { acc += 1; }
					if (n == 0) { return acc; }
					return countdown(n - 1, acc);
				}
				function countdown2(int n, int acc) -> int {
					if (n == 0) { return acc; }
					return countdown2(n - 1, acc + 2);
				}
				function both() -> string { string s = ""; for (int k = 0; k < 2; ++k) { s += to_string(countdown(60, 0)) + "," + to_string(countdown2(60, 1)) + ";"; } return s; }
				both();
			)", "122,121;122,121;", "tail call");
		});

		// a plain function's by-value shared_ptr parameter: a handle or null already tagged with the parameter's type is the parameter as
		// it stands, any other argument binds through the general rows (a derived handle, an untyped null, a var's handle, a wrong class)
		test("a_function_takes_a_handle_of_its_parameters_type_as_it_is", [this]() {
			all(R"(
				class Node { int v = 0; shared_ptr<Node> next = null; Node(int n) { v = n; } }
				class Leaf : Node { Leaf(int n) : super(n * 10) {} }
				int value_of(shared_ptr<Node> n) { if (n == null) { return -1; } return n.v; }
				int sum2(shared_ptr<Node> a, shared_ptr<Node> b, int k) { return value_of(a) + value_of(b) + k; }
				void relink(shared_ptr<Node> from, shared_ptr<Node>& slot) { slot = from; }
				void reseat(shared_ptr<Node> n) { n = new Node(99); }
				int chain(shared_ptr<Node> n, int depth) { if (n == null) { return depth; } return chain(n.next, depth + 1); }
				function run() -> string {
					shared_ptr<Node> a = new Node(1);
					shared_ptr<Node> b = new Node(2);
					shared_ptr<Node> none = null;
					shared_ptr<Leaf> leaf = new Leaf(3);
					string out = to_string(sum2(a, b, 5)) + " " + to_string(value_of(none)) + " " + to_string(value_of(null)) + " " + to_string(value_of(leaf));
					relink(b, a.next);
					out += " " + to_string(a.next.v);
					shared_ptr<Node> tmp = null;
					relink(a, tmp);
					out += " " + to_string(tmp.v);
					reseat(a);
					out += " " + to_string(a.v);
					var loose = b;
					out += " " + to_string(value_of(loose)) + " " + to_string(value_of(new Node(8))) + " " + to_string(chain(a, 0));
					return out;
				}
				run() + " | " + run();
			)", "8 -1 -1 30 2 1 1 2 8 2 | 8 -1 -1 30 2 1 1 2 8 2", "handle parameters");
		});

		test("a_handle_argument_of_another_class_raises_as_the_general_bind_does", [this]() {
			const std::string script = R"(
				class Node { int v = 1; }
				class Other { int v = 7; }
				int value_of(shared_ptr<Node> n) { return n.v; }
				function run() -> string {
					string log = "";
					for (int i = 0; i < 3; ++i) {
						var o = i == 1 ? new Other() : new Node();
						try { log += to_string(value_of(o)); } catch (e) { log += "[" + e + "]"; }
					}
					return log;
				}
				run();
			)";
			const std::string oracle = run(which::interp, script);
			check_true(oracle.find("[") != std::string::npos, "the wrong class is refused: " + oracle);
			check_eq(oracle, run(which::vm, script), "another class: vm");
			check_eq(oracle, run(which::vm_reference, script), "another class: vm-reference");
		});

		test("a_handle_parameter_dies_at_its_functions_exit", [this]() {
			all(R"(
				string log = "";
				class D { int k = 0; D(int n) { k = n; } ~D() { log += "d" + to_string(k) + " "; } }
				int peek(shared_ptr<D> d) { return d.k; }
				int take(shared_ptr<D> d, shared_ptr<D>& out) { out = d; return d.k; }
				function run() -> string {
					shared_ptr<D> keep = null;
					int s = 0;
					for (int i = 0; i < 3; ++i) { s += peek(new D(i)); log += "| "; }
					for (int i = 3; i < 5; ++i) { s += take(new D(i), keep); log += "| "; }
					return to_string(s) + " ";
				}
				run() + log;
			)", "10 d0 | d1 | d2 | | d3 | d4 ", "a handle parameter's lifetime");
		});

		test("out_parameters_over_fields_and_locals_rebuild_a_tree", [this]() {
			// the treap's shape: by-value handles beside shared_ptr<T>& out-parameters bound to fields and to the caller's locals
			all(R"(
				class Node { int x = 0; int y = 0; shared_ptr<Node> left = null; shared_ptr<Node> right = null; }
				int SEED = 7;
				int next_priority() { SEED = (SEED * 1103 + 12345) % 65536; return SEED; }
				void merge(shared_ptr<Node> lower, shared_ptr<Node> greater, shared_ptr<Node>& result) {
					if (lower == null) { result = greater; }
					else if (greater == null) { result = lower; }
					else if (lower.y < greater.y) { result = lower; merge(lower.right, greater, lower.right); }
					else { result = greater; merge(lower, greater.left, greater.left); }
				}
				void split(shared_ptr<Node> orig, shared_ptr<Node>& lower, shared_ptr<Node>& greater_or_equal, int value) {
					if (orig == null) { lower = null; greater_or_equal = null; }
					else if (orig.x < value) { lower = orig; split(orig.right, orig.right, greater_or_equal, value); }
					else { greater_or_equal = orig; split(orig.left, lower, orig.left, value); }
				}
				void insert(shared_ptr<Node>& root, int x) {
					shared_ptr<Node> lower = null;
					shared_ptr<Node> equal_greater = null;
					shared_ptr<Node> equal = null;
					shared_ptr<Node> greater = null;
					split(root, lower, equal_greater, x);
					split(equal_greater, equal, greater, x + 1);
					if (equal == null) { equal = new Node(); equal.x = x; equal.y = next_priority(); }
					shared_ptr<Node> tmp = null;
					merge(lower, equal, tmp);
					merge(tmp, greater, root);
				}
				void erase(shared_ptr<Node>& root, int x) {
					shared_ptr<Node> lower = null;
					shared_ptr<Node> equal_greater = null;
					shared_ptr<Node> equal = null;
					shared_ptr<Node> greater = null;
					split(root, lower, equal_greater, x);
					split(equal_greater, equal, greater, x + 1);
					merge(lower, greater, root);
				}
				string walk(shared_ptr<Node> n) { if (n == null) { return ""; } return walk(n.left) + to_string(n.x) + " " + walk(n.right); }
				function run() -> string {
					shared_ptr<Node> root = null;
					int cur = 5;
					for (int i = 1; i <= 60; ++i) {
						cur = (cur * 57 + 43) % 101;
						if (i % 3 == 1) { erase(root, cur % 23); } else { insert(root, cur % 23); }
					}
					return walk(root);
				}
				run();
			)", "1 2 5 7 8 10 12 16 18 19 22 ", "a treap over out-parameters");
		});

		test("a_method_reads_a_reference_argument_into_its_by_value_parameter", [this]() {
			// an element or a field read through a reference alias arrives as a reference; the parameter takes its value as the general
			// bind does, and an element out of range raises as it does
			all(R"(
				class Acc {
					int total = 0;
					int add(int x, int y) { switch (x) { default: total += x + y; return total; } }
					float scale(float f, int k) { switch (k) { default: return f * k; } }
					string tag(string s, int n) { switch (n) { default: return s + to_string(n); } }
					int peek(shared_ptr<Acc> other, var k) { switch (1) { default: return other.total + k; } }
					int bump(int x) { switch (x) { default: total += 1; return x; } }
					int shift(int x, string s) { switch (x) { default: GA[0] = GA[0] + 100; GS[0] = GS[0] + "!"; return x * 1000 + s.size(); } }
				}
				array<int> GA = [1, 2, 3, 4];
				array<string> GS = ["ab", "cd"];
				function run() -> string {
					array<float> f = [0.5, 1.5];
					array<string> s = ["x", "y"];
					array<shared_ptr<Acc>> hs = [new Acc(), new Acc()];
					auto& a = GA;
					auto& fr = f;
					auto& sr = s;
					auto& hr = hs;
					shared_ptr<Acc> acc = new Acc();
					string out = "";
					for (int i = 0; i < 4; ++i) { out += to_string(acc.add(a[i], a[3 - i])) + " "; }
					out += to_string(acc.scale(fr[1], a[2])) + " " + acc.tag(sr[1], a[0]) + " ";
					hr[1].total = 40;
					out += to_string(acc.peek(hr[1], a[1])) + " ";
					a[0] = 7;
					out += to_string(acc.add(a[0], acc.bump(a[0]))) + " " + to_string(acc.total);
					// the parameter is the element's value at the call: the callee writing the element does not reach it (the site's later
					// calls are the ones a cached method row answers)
					auto& gs = GS;
					for (int r = 0; r < 3; ++r) { out += " " + to_string(acc.shift(a[0], gs[0])); }
					out += " " + to_string(a[0]) + gs[0];
					try { out += to_string(acc.add(a[7], 1)); } catch (e) { out += " [" + e + "]"; }
					return out;
				}
				run();
			)", "5 10 15 20 4.5 y1 42 35 35 7002 107003 207004 307ab!!! [Array index 7 out of bounds for array of size 4]", "reference arguments of a method");
		});

		// a call from a typed loop (a cell block) into a callee that stays a call: its arguments are the block's values, its result lands in
		// the block, and everything the general call does (order, conversions, limits, errors) is the general call's
		test("a_block_calls_a_by_value_callee_and_lands_its_result", [this]() {
			all(R"(
				int G = 0;
				int g1(int x) { switch (x) { default: return x + 1; } }
				int g3(int x, int y, int z) { switch (x) { default: return x + y * 2 + z * 3; } }
				float gf(float a, int b, bool c) { switch (b) { default: return c ? a + b : a - b; } }
				void bump(int x) { switch (x) { default: G = G + x; } }
				int gvar(x, int y) { switch (y) { default: return x + y; } }
				function basic(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += g1(i); s += g3(i, 2, s & 7); } return s; }
				function floats(int n) -> float { float s = 0.5; for (int i = 0; i < n; ++i) { s = s + gf(s, i, (i & 1) == 0); } return s; }
				function statements(int n) -> int { for (int i = 0; i < n; ++i) { bump(i); } return G; }
				function untyped(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += gvar(i, 3); } return s; }
				function nested(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += g3(g1(i), g1(g1(i)), i); } return s; }
				function reads_global(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { bump(1); s += G; } return s; }
				string out = to_string(basic(20)) + " " + to_string(floats(12)) + " " + to_string(statements(11)) + " " + to_string(untyped(13));
				out + " " + to_string(nested(15)) + " " + to_string(reads_global(10)) + " " + to_string(basic(5));
			)", "639 1589.0 55 117 705 605 72", "by-value callees");
		});

		test("a_block_call_whose_argument_converts_or_binds_by_reference_takes_the_general_call", [this]() {
			all(R"(
				float gf(float a, int b, bool c) { switch (b) { default: return c ? a + b : a - b; } }
				int byref(int& x, int y) { switch (y) { default: x = x + y; return x; } }
				int slen(string s, int i) { switch (i) { default: return s.size() + i; } }
				int dflt(int x, int y = 5) { switch (x) { default: return x + y; } }
				function converts(int n) -> float { float s = 0.0; for (int i = 0; i < n; ++i) { s = s + gf(i, i, true); } return s; }
				function binds(int n, string tag) -> int { int acc = 0; int s = 0; for (int i = 0; i < n; ++i) { s += byref(acc, i); s += acc; } return s * 1000 + acc + tag.size(); }
				function strings(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += slen("abc", i); } return s; }
				function defaults(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += dflt(i); s += dflt(i, i); } return s; }
				// the second run of binds meets its site's cache armed, in a frame whose slots hold its locals: its first call is still the general one
				to_string(converts(9)) + " " + to_string(binds(9, "")) + " " + to_string(binds(7, "")) + " " + to_string(strings(7)) + " " + to_string(defaults(6));
			)", "72.0 240036 112021 42 75", "general callees");
		});

		test("a_block_callee_that_throws_leaves_the_callers_values_as_written", [this]() {
			all(R"(
				int gthrow(int x) { switch (x) { default: if (x == 5) { throw "five"; } return x * 2; } }
				function caught(int n) -> string {
					string log = "";
					int s = 0;
					for (int i = 0; i < n; ++i) {
						int before = s + i;
						try { s += gthrow(i); } catch (e) { log += e + ":" + to_string(before) + ":" + to_string(s) + ";"; }
						s += 1;
					}
					return log + to_string(s);
				}
				function inner(int n) -> int { int s = 0; int t = 1; for (int i = 0; i < n; ++i) { t = t + i; s += gthrow(i) + t; } return s; }
				function outer() -> string { string log = ""; for (int k = 4; k < 8; ++k) { try { log += to_string(inner(k)); } catch (e) { log += e; } log += ","; } return log; }
				caught(9) + " " + outer();
			)", "five:30:25;71 26,45,five,five,", "a throwing callee");
		});

		test("a_block_callee_rebound_to_a_reference_taker_binds_the_callers_local", [this]() {
			// the first run's call is still in flight when its callee throws through the caller; the next run of the same loop calls a function
			// that writes its argument's variable
			all(R"(
				int inc_val(int x) { switch (x) { default: if (x == 3) { throw "t3"; } return x + 1; } }
				int inc_ref(int& x) { switch (x) { default: x = x + 10; return 1; } }
				var step = inc_val;
				function mid(int n) -> int { int s = 0; int v = 0; for (int i = 0; i < n; ++i) { v = i; s += step(v); s += v; } return s; }
				string log = to_string(mid(3));
				try { log += "," + to_string(mid(6)); } catch (e) { log += "," + e; }
				step = inc_ref;
				log += "," + to_string(mid(4));
				step = inc_val;
				log + "," + to_string(mid(3));
			)", "9,t3,50,9", "a rebound callee");
		});

		test("a_block_callee_rebound_by_another_callee_is_the_new_function_at_the_next_call", [this]() {
			all(R"(
				int g1(int x) { switch (x) { default: return x + 1; } }
				int alt(int x) { switch (x) { default: return x * 100; } }
				int flip(int x) { switch (x) { default: if (x == 5) { g1 = alt; } return 0; } }
				function rebinds(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += g1(i); s += flip(i); } return s; }
				function long_run(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += g1(i & 7); s = s & 65535; } return s; }
				to_string(long_run(5000)) + " " + to_string(rebinds(10)) + " " + to_string(long_run(5000));
			)", "22500 3021 46064", "a callee rebound by another call of the loop");
		});

		test("a_depth_limit_and_a_spent_budget_at_a_block_call_raise_as_the_general_call", [this]() {
			const std::string deep = R"(
				int deep(int n) { switch (n) { default: return deep(n + 1) + 1; } }
				int leaf(int x) { switch (x) { default: return x + 1; } }
				function chain(int d, int n) -> int { if (d > 0) { return chain(d - 1, n) + 0; } int s = 0; for (int i = 0; i < n; ++i) { s += leaf(i); } return s; }
				function probe(int n) -> string { string log = ""; int s = 0; for (int i = 0; i < n; ++i) { try { s += deep(i); } catch (e) { log += "D"; } s += i; } return log + to_string(s); }
				string out = probe(3);
				// the loop's own call is the one that meets the cap
				for (int d = 180; d < 200; ++d) { try { out += "," + to_string(chain(d, 4)); } catch (e) { out += ",E"; } }
				out;
			)";
			const std::string spin = R"(
				int leaf(int x) { switch (x) { default: return x + 1; } }
				function forever() -> int { int s = 0; int i = 0; while (true) { s = (s + leaf(i)) & 1023; i = (i + 1) & 1023; } return s; }
				forever();
			)";
			const std::string after = R"(
				int leaf(int x) { switch (x) { default: return x + 1; } }
				function sum(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += leaf(i); } return s; }
				sum(10);
			)";
			std::string general_form;   // the vm with every optimization off: the frame that meets the cap is the same one
			for (const which w : {which::vm_reference, which::vm, which::interp}) {
				auto e = make_for(w);
				e->max_call_depth(backend_type::interpreter, 200);
				e->max_call_depth(backend_type::vm, 200);
				const std::string got = e->execute(deep).to_string();
				if (w == which::vm_reference) { general_form = got; }
				if (w == which::vm) { check_eq(general_form, got, "the depth limit at a block call"); }
				check_true(got.rfind("DDD3,10,", 0) == 0 && got.find(",E") != std::string::npos, "the cap is met inside the loop: " + got);
				auto b = make_for(w);
				b->execution_budget(0.05);
				check_throws([&]() { b->execute(spin); }, "the budget stops a loop of block calls");
				b->execution_budget(0);
				check_eq(std::string("55"), b->execute(after).to_string(), "the next run starts clean");
			}
		});

		test("a_raw_entered_function_calls_a_general_callee_from_its_loop", [this]() {
			all(R"(
				int leaf(int x) { switch (x) { default: return x * 3; } }
				int work(int n) { int s = 0; for (int i = 0; i < n; ++i) { s += leaf(i); } return s; }
				int twice(int n) { return work(n) + work(n + 1); }
				function run(int n) -> int { int t = 0; for (int k = 0; k < n; ++k) { t += twice(k); t += work(3); } return t; }
				run(12);
			)", "1626", "a general callee under raw frames");
		});

		test("the_lean_door_takes_a_block_call_whose_arguments_bind_by_value_as_they_are", [this]() {
			// vm only: the count of block calls entered at their arguments instruction. A site's first call arms its cache and the callee's
			// frame shape on the general door
			auto lean_calls = [](const std::string& script) {
				auto e = make_for(which::vm);
				e->execute(script);
				return detail::engine_access::cells_counters(*e).lean_calls;
			};
			const unsigned long long by_value = lean_calls(R"(
				int g3(int x, float y, bool z) { switch (x) { default: return z ? x : 0; } }
				function run(int n) -> int { int s = 0; float f = 0.5; for (int i = 0; i < n; ++i) { s += g3(i, f, true); } return s; }
				run(50);
			)");
			check_true(by_value >= 48 && by_value <= 50, "by-value scalars enter lean: " + std::to_string(by_value));
			const unsigned long long untyped = lean_calls(R"(
				int gvar(x, int y) { switch (y) { default: return x + y; } }
				function run(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += gvar(i, 3); } return s; }
				run(50);
			)");
			check_true(untyped >= 48 && untyped <= 50, "a var parameter takes a scalar as it is: " + std::to_string(untyped));
			check_eq(0ull, lean_calls(R"(
				float gf(float a) { switch (1) { default: return a + 1.0; } }
				function run(int n) -> float { float s = 0.0; for (int i = 0; i < n; ++i) { s = s + gf(i); } return s; }
				run(50);
			)"), "an int for a float parameter converts in the binder");
			check_eq(0ull, lean_calls(R"(
				int byref(int& x, int y) { switch (y) { default: x = x + y; return x; } }
				function run(int n) -> int { int acc = 0; int s = 0; for (int i = 0; i < n; ++i) { s += byref(acc, i); } return s; }
				run(50);
			)"), "a reference parameter binds the caller's variable");
			check_eq(0ull, lean_calls(R"(
				int dflt(int x, int y = 5) { switch (x) { default: return x + y; } }
				function run(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += dflt(i, i); } return s; }
				run(50);
			)"), "a parameter with a default takes the general frame");
			// a push that would be the budget's 1024th tick declines, and the general door polls the budget; the loop goes on through it
			const unsigned long long many = lean_calls(R"(
				int g1(int x) { switch (x) { default: return x + 1; } }
				function run(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += g1(i); s = s & 65535; } return s; }
				run(5000);
			)");
			check_true(many > 4900 && many < 5000, "a push the plain frame declines takes the general door for that call: " + std::to_string(many));
		});

		test("a_namespace_function_and_a_method_frames_block_call_keep_their_resolution", [this]() {
			all(R"(
				namespace util { int step(int x) { switch (x) { default: return x + 2; } } }
				int step(int x) { switch (x) { default: return x + 1; } }
				class Holder {
					int base = 100;
					int step(int x) { switch (x) { default: return x + base; } }
					int own(int n) { int s = 0; for (int i = 0; i < n; ++i) { s += step(i); } return s; }
					int spaced(int n) { int s = 0; for (int i = 0; i < n; ++i) { s += util::step(i); } return s; }
				}
				function plain(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += step(i); s += util::step(i); } return s; }
				Holder h = Holder();
				to_string(plain(6)) + " " + to_string(h.own(6)) + " " + to_string(h.spaced(6));
			)", "48 615 27", "name resolution at the door");
		});
	}
};

}

using call_path_tests = jai::foundry::tests::call_path_tests;
FOUNDRY_REGISTER(call_path_tests)
