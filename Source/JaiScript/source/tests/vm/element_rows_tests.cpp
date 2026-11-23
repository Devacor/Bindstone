// Element, map entry and handle reads and writes on the vm's fused rows (the index fold, the element taken in place by a declaration
// or a member read, the map rows, the handle moves): each case holds the interpreter, the vm, the vm without proofs and the
// vm-reference backend to one result, errors and destructor order included.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/engine_access.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>
#include <vector>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class element_rows_tests : public suite {
public:
	element_rows_tests() : suite("Element Rows") {}

	enum class backend { interpreter, vm, vm_no_proofs, vm_reference };

	static std::string run(backend which, const std::vector<std::string>& steps, void (*host)(engine&) = nullptr) {
		auto e = jai::foundry::make_default_engine();
		e->set_backend(which == backend::interpreter ? backend_type::interpreter : backend_type::vm);
		if (which == backend::vm_reference) { e->vm_reference_mode(true); }
		if (which == backend::vm_no_proofs) { e->type_proofs(false); }
		stdlib::register_all(*e);
		if (host) { host(*e); }
		std::string result;
		for (const auto& step : steps) {
			try { result += e->execute(step).to_string() + "|"; } catch (const std::exception& ex) { result += std::string("throw:") + ex.what() + "|"; }
		}
		return result;
	}

	// every backend gives the interpreter's result; `expected` pins it when given
	void same_everywhere(const std::vector<std::string>& steps, const std::string& expected, const std::string& what, void (*host)(engine&) = nullptr) {
		const std::string reference = run(backend::interpreter, steps, host);
		if (!expected.empty()) { check_eq(expected, reference, what + " (interpreter)"); }
		check_eq(reference, run(backend::vm, steps, host), what + " (vm)");
		check_eq(reference, run(backend::vm_no_proofs, steps, host), what + " (vm, no proofs)");
		check_eq(reference, run(backend::vm_reference, steps, host), what + " (vm-reference)");
	}

	void forge_tests() override {
		test("an_index_folds_the_int_bitwise_and_shift_operators", [this]() {
			same_everywhere({ R"JAI(
class P { int a = 0; }
function reads(int n) -> string {
	array<int> ints = []; array<var> vars = []; array<P> objs = []; array<string> strs = []; map<int, int> m = {}; string text = "abcdefgh";
	for (int i = 0; i < 8; ++i) { ints.push(i * 10); vars.push(i * 100); P p = P(); p.a = i; objs.push(p); strs.push("s" + to_string(i)); m[i * 7 + 1] = i * 1000; }
	int s = 0; string t = "";
	int one = 1; int three = 3;
	for (int i = 0; i < n; ++i) {
		s += ints[i & 7];
		s += vars[i | 1];
		s += objs[i ^ three].a;
		s += ints[i << one];
		s += vars[i >> one];
		s += m[(i & 7) * 7 + 1];
		s += ints[(i & 3) * 2 + 1];
		t += strs[i & 7];
		t += text[i & 7];
		ints[i & 7] = ints[i & 7] + 1;
		vars[i ^ 1] += 2;
		m[(i & 1) * 7 + 1] = i;
	}
	return to_string(s) + ":" + t + ":" + to_string(ints) + to_string(vars) + to_string(m[1]) + to_string(m[8]);
}
0;)JAI", "reads(4);", "reads(1);" }, "", "fold rows");
		});

		test("a_program_scope_container_takes_a_folded_index_by_what_it_holds", [this]() {
			same_everywhere({ R"JAI(
map<int, int> M = {};
array<int> A = [0, 0, 0, 0];
var V = [0, 0, 0, 0];
string S = "abcd";
function map_store(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s = (s + i * 3) % 1000003; s = s ^ (i & 255); M[i & 7] = s; } return s + M[1]; }
function map_read(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s = s + i; s += M[i & 7]; } return s; }
function array_store(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s = s + i; A[i & 3] = s; V[i ^ 1] = s; s += A[(i + 1) & 3] + V[i & 3]; } return s; }
function text_read(int n) -> string { string out = ""; for (int i = 0; i < n; ++i) { out += S[i & 3]; } return out; }
function rebound(int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s = s + i; V[i & 3] = s; } return s; }
function raises(int at) -> int { int s = 0; for (int i = 0; i < 20; ++i) { s = s + i; M[i & 3] = 100 / (i - at); s = s + 1; } return s; }
0;)JAI", "map_store(100);", "map_read(100);", "array_store(100);", "text_read(9);", "rebound(10);", "V = {}; rebound(10);", "to_string(V);", "V = \"text\"; rebound(3);",
				"raises(7);", "to_string(M[2]);", "raises(100);" }, "", "program-scope containers");
		});

		test("an_index_fold_keeps_the_general_texts", [this]() {
			same_everywhere({ R"JAI(
function shift_out(int k) -> int { array<int> a = [1, 2, 3, 4]; int s = 0; for (int i = 0; i < 2; ++i) { s += a[i << k]; } return s; }
function shift_neg(int k) -> int { array<int> a = [1, 2, 3, 4]; int s = 0; for (int i = 0; i < 2; ++i) { s += a[4 >> k]; } return s; }
function out_of_range(int k) -> int { array<int> a = [1, 2, 3, 4]; int s = 0; for (int i = 0; i < 2; ++i) { s += a[i | k]; } return s; }
function float_operand(var k) -> int { array<int> a = [1, 2, 3, 4]; int s = 0; for (int i = 0; i < 2; ++i) { s += a[i & k]; } return s; }
function map_missing(int k) -> int { map<int, int> m = {}; m[1] = 5; int s = 0; for (int i = 0; i < 2; ++i) { s += m[(i & k) + 1]; } return s; }
function var_array(int k) -> int { array<var> a = [1, 2, 3, 4]; int s = 0; for (int i = 0; i < 2; ++i) { s += a[i << k]; } return s; }
function store_out(int k) -> int { array<var> a = [1, 2, 3, 4]; for (int i = 0; i < 2; ++i) { a[(i | k) & 15] = 9; } return a[0]; }
0;)JAI", "shift_out(64);", "shift_out(1);", "shift_neg(-1);", "out_of_range(8);", "out_of_range(2);", "float_operand(1.5);", "float_operand(3);",
				"map_missing(1);", "map_missing(0);", "var_array(64);", "var_array(70);", "store_out(8);", "store_out(0);" }, "", "fold texts");
		});

		test("a_registered_bitwise_operator_over_ints_answers_in_every_lane", [this]() {
			same_everywhere({ R"JAI(
function reads(var k) -> int { array<int> a = [1, 2, 3, 40]; int s = 0; for (int i = 0; i < 4; ++i) { s += a[i & k] * (i + 1); } return s; }
function plain(var k) -> int { int s = 0; for (int i = 0; i < 4; ++i) { s += (i & k) * (i + 1); } return s; }
function typed(int k) -> int { int s = 0; for (int i = 0; i < 4; ++i) { s += (i & k) * (i + 1); } return s; }
function typed_index(int k) -> int { array<int> a = [1, 2, 3, 40]; int s = 0; for (int i = 0; i < 4; ++i) { s += a[i & k] * (i + 1); } return s; }
0;)JAI", "reads(3);", "to_string(calls());", "plain(3);", "to_string(calls());", "typed(3);", "to_string(calls());", "typed_index(3);", "to_string(calls());" },
				"0|136|4|18|8|18|12|136|16|", "registered operator", [](engine& e) {
				auto count = std::make_shared<script_int>(0);
				e.add_function("&", [count](script_int a, script_int b) { ++*count; return (a & b) ^ 1; });
				e.add_function("calls", [count]() { return *count; });
			});
		});

		test("a_declaration_takes_an_element_as_the_value_its_reference_gives", [this]() {
			same_everywhere({ R"JAI(
var LOG = "";
class P { int a = 0; P() {} P(int v) { a = v; } }
class Q { int a = 0; Q(int v) { a = v; } Q(const Q& o) { a = o.a + 100; LOG = LOG + "c" + to_string(o.a); } ~Q() { LOG = LOG + "~" + to_string(a); } }
class Base { int k = 1; function who() -> string { return "base" + to_string(k); } }
class Derived : Base { int extra = 7; override function who() -> string { return "derived" + to_string(k + extra); } }
function scalars(int n) -> string {
	array<int> ints = [1, 2, 3, 4]; array<float> floats = [1.5, 2.5, 3.5, 4.5]; array<string> strs = ["a", "b", "c", "d"]; array<bool> flags = [true, false, true, false];
	array<var> vars = [7, 2.25, "text", true, 'c', null, [1, 2], {"k": 3}];
	string out = "";
	for (int i = 0; i < n; ++i) {
		int x = ints[i & 3]; float y = floats[i & 3]; string s = strs[i & 3]; bool b = flags[i & 3]; float widened = ints[i & 3];
		var v = vars[i & 7]; auto w = vars[(i + 1) & 7]; var copy = ints[i & 3]; auto text = strs[(i + 2) & 3];
		int from_var = vars[i & 0]; float widened_var = vars[i & 0]; float float_var = vars[(i & 0) + 1]; string text_var = vars[(i & 0) + 2];
		out = out + to_string(x) + to_string(y) + s + to_string(b) + to_string(widened) + to_string(v) + to_string(w) + to_string(copy) + text +
			to_string(from_var) + to_string(widened_var) + to_string(float_var) + text_var + ";";
	}
	return out;
}
function narrowed(int n) -> int { array<var> vars = [7, 2.25]; int s = 0; for (int i = 0; i < n; ++i) { int x = vars[i & 1]; s += x; } return s; }
function values(int n) -> string {
	array<P> ps = []; array<Q> qs = []; array<var> mixed = [];
	for (int i = 0; i < 4; ++i) { ps.push(P(i)); qs.push(Q(i)); mixed.push(P(i + 10)); }
	LOG = "";
	string out = "";
	for (int i = 0; i < n; ++i) {
		P p = ps[i & 3]; p.a = p.a + 50;
		Q q = qs[i & 3]; q.a = q.a + 1;
		var m = mixed[i & 3]; m.a = m.a + 5;
		out = out + to_string(p.a) + "/" + to_string(ps[i & 3].a) + "/" + to_string(q.a) + "/" + to_string(qs[i & 3].a) + "/" + to_string(m.a) + "/" + to_string(mixed[i & 3].a) + ";";
	}
	return out + LOG;
}
function handles(int n) -> string {
	array<shared_ptr<P>> hs = []; array<shared_ptr<Base>> bases = []; array<weak_ptr<P>> weaks = []; array<var> mixed = [];
	for (int i = 0; i < 4; ++i) { shared_ptr<P> p = new P(i); hs.push(p); weaks.push(p); mixed.push(p); }
	bases.push(new Base()); bases.push(new Derived()); bases.push(null); bases.push(new Derived());
	hs.push(null);
	string out = "";
	for (int i = 0; i < n; ++i) {
		shared_ptr<P> q = hs[i & 3]; q.a = q.a + 1;
		shared_ptr<P> none = hs[4];
		shared_ptr<Base> b = bases[i & 3];
		weak_ptr<P> w = hs[i & 3]; weak_ptr<P> w2 = weaks[i & 3];
		var m = mixed[i & 3]; auto h = hs[i & 3]; shared_ptr<P> from_var = mixed[i & 3];
		out = out + to_string(q.a) + ":" + to_string(q.use_count()) + ":" + to_string(none == null) + ":" + (b == null ? "null" : b.who()) + ":" +
			to_string(w.expired()) + to_string(w2.expired()) + ":" + to_string(m.a) + to_string(h.a) + to_string(from_var.a) + ";";
	}
	shared_ptr<P> first = hs[0];
	return out + to_string(first.use_count());
}
0;)JAI", "scalars(9);", "values(5);", "handles(6);", "narrowed(1);", "narrowed(2);" }, "", "declarations");
		});

		test("a_declaration_from_an_element_keeps_its_texts", [this]() {
			same_everywhere({ R"JAI(
class P { int a = 0; }
class Other { int b = 0; }
class Base { int k = 1; }
class Derived : Base { int extra = 7; }
function out_of_range(int k) -> int { array<shared_ptr<P>> hs = [new P()]; int s = 0; for (int i = 0; i < 2; ++i) { shared_ptr<P> q = hs[i + k]; s += q.a; } return s; }
function wrong_class() -> int { array<var> mixed = [new Other()]; int s = 0; for (int i = 0; i < 2; ++i) { shared_ptr<P> q = mixed[i & 0]; s += q.a; } return s; }
function downcast() -> int { array<var> bases = [new Derived(), new Base()]; int s = 0; for (int i = 0; i < 2; ++i) { shared_ptr<Derived> d = bases[i & 1]; s += d.extra; } return s; }
function upcast() -> int { array<var> deriveds = [new Derived(), new Derived()]; int s = 0; for (int i = 0; i < 2; ++i) { shared_ptr<Base> b = deriveds[i & 1]; s += b.k; } return s; }
function from_weak() -> int { shared_ptr<P> keep = new P(); weak_ptr<P> w = keep; array<var> weaks = []; weaks.push(w); int s = 0; for (int i = 0; i < 2; ++i) { shared_ptr<P> q = weaks[i & 0]; s += q.a; } return s; }
function value_into_handle() -> int { array<var> ps = [P()]; int s = 0; for (int i = 0; i < 2; ++i) { shared_ptr<P> q = ps[i & 0]; s += q.a; } return s; }
function string_into_int() -> int { array<var> mixed = ["text"]; int s = 0; for (int i = 0; i < 2; ++i) { int x = mixed[i & 0]; s += x; } return s; }
function null_member() -> int { array<shared_ptr<P>> hs = [null]; int s = 0; for (int i = 0; i < 2; ++i) { shared_ptr<P> q = hs[i & 0]; s += q.a; } return s; }
function map_missing(int k) -> int { map<int, shared_ptr<P>> m = {}; m[1] = new P(); int s = 0; for (int i = 0; i < 2; ++i) { shared_ptr<P> q = m[i + k]; s += q.a; } return s; }
function map_string_missing(string k) -> int { map<string, int> m = {}; m["a"] = 4; int s = 0; for (int i = 0; i < 2; ++i) { int x = m[k]; s += x; } return s; }
0;)JAI", "out_of_range(0);", "out_of_range(-1);", "wrong_class();", "downcast();", "upcast();", "from_weak();", "value_into_handle();", "string_into_int();", "null_member();",
				"map_missing(1);", "map_missing(0);", "map_string_missing(\"a\");", "map_string_missing(\"b\");" }, "", "declaration texts");
		});

		test("a_declaration_from_an_element_runs_the_copy_and_the_destructors_in_order", [this]() {
			same_everywhere({ R"JAI(
var LOG = "";
var COUNTS = [];
class Q { string n = ""; Q(string s) { n = s; } Q(const Q& o) { n = o.n + "'"; LOG = LOG + "c" + n; } ~Q() { LOG = LOG + "~" + n; } }
class H { string n = ""; H(string s) { n = s; } ~H() { LOG = LOG + "~" + n; } }
function copies() -> string {
	array<Q> qs = []; qs.push(Q("a")); qs.push(Q("b"));
	LOG = "";
	for (int i = 0; i < 3; ++i) { Q q = qs[i & 1]; LOG = LOG + "|"; }
	return LOG;
}
function last_handle() -> string {
	LOG = "";
	for (int i = 0; i < 3; ++i) {
		array<shared_ptr<H>> hs = []; hs.push(new H("h" + to_string(i)));
		shared_ptr<H> q = hs[i & 0];
		hs.clear();
		LOG = LOG + "held" + to_string(q.use_count()) + "|";
	}
	return LOG;
}
function reassigned() -> string {
	LOG = "";
	array<shared_ptr<H>> hs = []; hs.push(new H("x")); hs.push(new H("y"));
	for (int i = 0; i < 4; ++i) { shared_ptr<H> q = hs[i & 1]; hs[i & 1] = new H("n" + to_string(i)); LOG = LOG + q.n + "|"; }
	return LOG;
}
0;)JAI", "copies();", "last_handle();", "reassigned();", "LOG;" }, "", "declaration order");
		});

		test("a_real_reference_to_an_element_keeps_the_reference_rules", [this]() {
			same_everywhere({ R"JAI(
class P { int a = 0; P(int v) { a = v; } }
function removed_below() -> int { array<P> ps = [P(1), P(2), P(3)]; int s = 0; for (int i = 0; i < 2; ++i) { P& r = ps[1]; ps.remove(0); s += r.a; } return s; }
function inserted_below() -> int { array<P> ps = [P(1), P(2), P(3)]; int s = 0; for (int i = 0; i < 2; ++i) { P& r = ps[1]; ps.insert(0, P(9)); s += r.a; } return s; }
function removed_above() -> int { array<P> ps = [P(1), P(2), P(3), P(4)]; int s = 0; for (int i = 0; i < 2; ++i) { P& r = ps[0]; ps.pop(); s += r.a; } return s; }
function shrunk() -> int { array<P> ps = [P(1), P(2)]; int s = 0; for (int i = 0; i < 2; ++i) { P& r = ps[1]; ps.pop(); s += r.a; } return s; }
function writes_through() -> int { array<P> ps = [P(1), P(2)]; int seen = 0; for (int i = 0; i < 4; ++i) { P& r = ps[i & 1]; r.a = r.a + 10; P copy = ps[i & 1]; copy.a = copy.a + 1000; seen += copy.a; } return ps[0].a * 100 + ps[1].a + seen; }
function handle_reference() -> int { array<shared_ptr<P>> hs = [new P(1), new P(2)]; for (int i = 0; i < 4; ++i) { shared_ptr<P>& r = hs[i & 1]; r = new P(i + 5); } return hs[0].a * 100 + hs[1].a; }
function map_reference() -> int { map<int, int> m = {}; m[1] = 4; int s = 0; for (int i = 0; i < 2; ++i) { int& r = m[1]; m.remove(1); s += r; } return s; }
0;)JAI", "removed_below();", "inserted_below();", "removed_above();", "shrunk();", "writes_through();", "handle_reference();", "map_reference();" }, "", "reference rules");
		});

		test("a_handle_moves_between_a_field_and_a_local", [this]() {
			same_everywhere({ R"JAI(
var LOG = "";
class Node { int v = 0; shared_ptr<Node> next = null; ~Node() { LOG = LOG + "~" + to_string(v); } }
function build(int n) -> shared_ptr<Node> { shared_ptr<Node> head = null; for (int i = 0; i < n; ++i) { shared_ptr<Node> nd = new Node(); nd.v = i; nd.next = head; head = nd; } return head; }
function walk(shared_ptr<Node> head) -> int { int s = 0; shared_ptr<Node> cur = head; while (cur != null) { s += cur.v; cur = cur.next; } return s; }
function walk_counts(shared_ptr<Node> head) -> string { string out = ""; shared_ptr<Node> cur = head.next; while (cur != null) { out = out + to_string(cur.use_count()); cur = cur.next; } return out; }
function drain(int n) -> string {
	LOG = "";
	shared_ptr<Node> cur = build(n);
	int s = 0;
	while (cur != null) { s += cur.v; cur = cur.next; LOG = LOG + "."; }
	return to_string(s) + LOG;
}
function var_cursor(shared_ptr<Node> head) -> int { int s = 0; var cur = head; while (cur != null) { s += cur.v; cur = cur.next; } return s; }
function self_link() -> int { shared_ptr<Node> a = new Node(); a.v = 4; a.next = a; shared_ptr<Node> cur = a; int s = 0; for (int i = 0; i < 3; ++i) { s += cur.v; cur = cur.next; } a.next = null; return s; }
function dropped() -> string { shared_ptr<Node> head = build(3); int s = walk(head); LOG = ""; head = null; return to_string(s) + LOG; }
shared_ptr<Node> H = build(6);
0;)JAI", "walk(H);", "walk_counts(H);", "drain(4);", "var_cursor(H);", "self_link();", "dropped();" }, "", "handle moves");
		});

		test("a_range_for_over_a_container_behind_a_reference_binds_each_element_as_declared", [this]() {
			same_everywhere({ R"JAI(
class E { int x = 0; int y = 0; E(int a, int b) { x = a; y = b; } }
class Sub : E { int z = 9; Sub(int a) : super(a, a + 1) {} }
class Other { int x = 5; int y = 1; }
function sum_ints(array<int>& b) -> int { int s = 0; for (int j : b) { s += j; } return s; }
function sum_floats(array<float>& b) -> float { float s = 0.0; for (float j : b) { s += j; } return s; }
function count_flags(array<bool>& b) -> int { int s = 0; for (bool j : b) { if (j) { s += 1; } } return s; }
function ints_of_var(var& b) -> int { int s = 0; for (int j : b) { s += j; } return s; }
function floats_of_var(var& b) -> float { float s = 0.0; for (float j : b) { s += j; } return s; }
function sum_refs(array<E>& es) -> int { int s = 0; for (E& e : es) { s += e.x - e.y; e.y = e.y + 1; } return s; }
function refs_of_var(var& es) -> int { int s = 0; for (E& e : es) { s += e.x * 10 + e.y; } return s; }
function skips(array<int>& b, int skip) -> int { int s = 0; for (int j : b) { if (j == skip) { continue; } if (j > 6) { break; } s += j; } return s; }
function local_reference(int rounds) -> int {
	map<int, array<int>> m = {}; m[1] = [1, 2, 3, 4];
	int s = 0;
	for (int r = 0; r < rounds; ++r) { array<int>& b = m[1]; for (int j : b) { if (j == r) { continue; } s += j; } }
	return s;
}
function neighbours(array<E>& es, array<int>& picks, int self) -> int {
	E& e = es[self];
	int s = 0;
	for (int j : picks) { if (j == self) { continue; } E& other = es[j]; int dx = e.x - other.x; int dy = e.y - other.y; if (dx * dx + dy * dy < 50) { s += dx + dy; } }
	return s;
}
function grows(array<int>& b) -> int { int s = 0; for (int j : b) { s += j; if (b.size() < 8) { b.push(j + 10); } } return s; }
function shrinks(array<int>& b) -> int { int s = 0; for (int j : b) { s += j; if (b.size() > 2) { b.pop(); } } return s; }
enum Level { low, mid, high }
function levels(array<Level>& ls) -> string { string out = ""; for (Level l : ls) { out = out + to_string(l) + (l == Level.mid ? "!" : "."); } return out; }
function levels_of_var(var& ls) -> string { string out = ""; for (Level l : ls) { out = out + to_string(l); } return out; }
array<Level> LEVELS = [Level.high, Level.mid, Level.low];
var PLAIN = [2, 1, 0];
array<int> INTS = [1, 2, 3, 4, 5, 6, 7, 8];
array<float> FLOATS = [1.5, 2.5, 3.0];
array<bool> FLAGS = [true, false, true, true];
array<E> ES = [E(1, 2), E(3, 4), E(5, 6)];
var MIXED_INTS = [1, 2, 3];
var MIXED = [1, 2.5, 3];
var TEXTS = [1, "two", 3];
var THINGS = [E(1, 2), Sub(3), E(5, 6)];
var STRANGERS = [E(1, 2), Other(), E(5, 6)];
0;)JAI", "sum_ints(INTS);", "sum_floats(FLOATS);", "count_flags(FLAGS);", "ints_of_var(MIXED_INTS);", "ints_of_var(MIXED);", "floats_of_var(MIXED);", "ints_of_var(TEXTS);",
				"sum_refs(ES);", "sum_refs(ES);", "refs_of_var(THINGS);", "refs_of_var(STRANGERS);", "skips(INTS, 3);", "local_reference(4);",
				"array<int> picks = [0, 1, 2, 1]; neighbours(ES, picks, 1);", "array<int> g = [1, 2, 3]; to_string(grows(g)) + to_string(g);",
				"array<int> sh = [1, 2, 3, 4, 5]; to_string(shrinks(sh)) + to_string(sh);", "levels(LEVELS);", "levels_of_var(LEVELS);", "levels_of_var(PLAIN);" }, "", "range-for");
		});

		test("a_member_of_an_element_reads_through_every_element_kind", [this]() {
			same_everywhere({ R"JAI(
var LOG = "";
class P { int a = 0; P(int v) { a = v; } }
class Base { int k = 1; Base(int v) { k = v; } function who() -> int { return k; } }
class Derived : Base { int extra = 7; Derived(int v) : super(v) { extra = v * 2; } override function who() -> int { return k + extra; } }
class Getter { int hidden = 3; int reads = 0; Getter(int v) { hidden = v; } function _get_a() { reads = reads + 1; return hidden * 10; } }
class Owner { int a = 0; Owner(int v) { a = v; } ~Owner() { LOG = LOG + "~" + to_string(a); } }
function values(int n) -> int { array<P> ps = [P(1), P(2), P(3), P(4)]; int s = 0; for (int i = 0; i < n; ++i) { s += ps[i & 3].a; ps[(i + 1) & 3].a = ps[i & 3].a + 1; } return s; }
function handles(int n) -> int { array<shared_ptr<P>> hs = [new P(1), new P(2), new P(3), new P(4)]; int s = 0; for (int i = 0; i < n; ++i) { s += hs[i & 3].a * 2; } shared_ptr<P> first = hs[0]; return s + first.use_count(); }
function subclasses(int n) -> int { array<shared_ptr<Base>> bs = [new Base(1), new Derived(2), new Base(3), new Derived(4)]; int s = 0; for (int i = 0; i < n; ++i) { s += bs[i & 3].k + bs[i & 3].who(); } return s; }
function getters(int n) -> string { array<Getter> gs = [Getter(1), Getter(2)]; int s = 0; for (int i = 0; i < n; ++i) { s += gs[i & 1].a; } return to_string(s) + ":" + to_string(gs[0].reads) + to_string(gs[1].reads); }
function mixed(int n) -> int { array<var> vs = [P(1), new P(2), P(3), new P(4)]; int s = 0; for (int i = 0; i < n; ++i) { s += vs[i & 3].a; } return s; }
function null_element(int n) -> int { array<shared_ptr<P>> hs = [new P(1), null]; int s = 0; for (int i = 0; i < n; ++i) { s += hs[i & 1].a; } return s; }
function not_an_object(int n) -> int { array<var> vs = [P(1), 5]; int s = 0; for (int i = 0; i < n; ++i) { s += vs[i & 1].a; } return s; }
function weak_element(int n) -> int { shared_ptr<P> keep = new P(6); array<weak_ptr<P>> ws = []; ws.push(keep); int s = 0; for (int i = 0; i < n; ++i) { s += ws[i & 0].lock().a; } return s; }
function replaced(int n) -> int { array<shared_ptr<P>> hs = [new P(1), new P(2)]; int s = 0; for (int i = 0; i < n; ++i) { s += hs[i & 1].a; hs[i & 1] = new P(i + 10); } return s; }
function last_owner(int n) -> string { LOG = ""; array<shared_ptr<Owner>> os = [new Owner(1), new Owner(2)]; int s = 0; for (int i = 0; i < n; ++i) { s += os[i & 1].a; if (i == 2) { os[0] = null; LOG = LOG + "|"; } } return to_string(s) + LOG; }
0;)JAI", "values(9);", "handles(9);", "subclasses(9);", "getters(5);", "mixed(9);", "null_element(1);", "null_element(2);", "not_an_object(1);", "not_an_object(2);",
				"weak_element(3);", "replaced(6);", "last_owner(3);", "last_owner(5);" }, "", "member of element");
		});

		test("a_var_array_takes_a_store_as_an_untyped_array_does", [this]() {
			same_everywhere({ R"JAI(
class P { int a = 0; P(int v) { a = v; } }
function plain(int n) -> string {
	array<var> typed = [1, 2.5, "s", true, 5, 6.5]; var untyped = [1, 2.5, "s", true, 5, 6.5];
	for (int i = 0; i < n; ++i) { typed[i % 6] = i; untyped[i % 6] = i; }
	string out = to_string(typed) + to_string(untyped);
	for (int i = 0; i < n; ++i) { float half = i * 0.5; typed[i % 6] = half; untyped[i % 6] = half; }
	return out + to_string(typed) + to_string(untyped);
}
function compound(int n) -> string {
	array<var> typed = [1, 2.5, 3, 4.5]; var untyped = [1, 2.5, 3, 4.5];
	for (int i = 0; i < n; ++i) { typed[i & 3] += 1; untyped[i & 3] += 1; typed[i & 3] *= 2; untyped[i & 3] -= 0.5; }
	return to_string(typed) + to_string(untyped);
}
function objects(int n) -> string {
	array<var> typed = [P(1), 2, null]; int s = 0;
	for (int i = 0; i < n; ++i) { typed[1] = i; s += typed[1]; typed[2] = i; }
	typed[0] = 7;
	return to_string(s) + to_string(typed);
}
function constrained(int n) -> string {
	array<float> floats = [1.0, 2.0]; array<int> ints = [1, 2];
	for (int i = 0; i < n; ++i) { floats[i & 1] = i; ints[i & 1] = i * 3; }
	return to_string(floats) + to_string(ints);
}
function refused(int n) -> int { array<int> ints = [1, 2]; for (int i = 0; i < n; ++i) { var half = i * 0.5; ints[i & 1] = half; } return ints[0] + ints[1]; }
0;)JAI", "plain(9);", "plain(2);", "compound(7);", "objects(4);", "constrained(5);", "refused(5);" }, "", "var array stores");
		});

		test("a_proven_map_read_in_a_loop_takes_its_entry_where_it_lives", [this]() {
			const std::string defs = R"JAI(
class P { int a = 4; }
function ints(int n) -> int { map<int, int> m = {}; for (int i = 0; i < 16; ++i) { m[i * 7 + 3] = i; } int s = 0; for (int i = 0; i < n; ++i) { s += m[(i & 15) * 7 + 3]; } return s; }
function floats(int n) -> float { map<int, float> m = {}; for (int i = 0; i < 8; ++i) { m[i] = i * 0.5; } float s = 0.0; for (int i = 0; i < n; ++i) { s += m[i & 7]; } return s; }
function flags(int n) -> int { map<int, bool> m = {}; for (int i = 0; i < 8; ++i) { m[i] = i % 3 == 0; } int s = 0; for (int i = 0; i < n; ++i) { if (m[i & 7]) { s += 1; } } return s; }
function declared(int n) -> string {
	map<int, int> m = {}; for (int i = 0; i < 8; ++i) { m[i] = i * 3; }
	int s = 0; float f = 0.0; string out = "";
	for (int i = 0; i < n; ++i) { int v = m[i & 7]; float w = m[(i + 1) & 7]; var d = m[i & 3]; s += v; f = f + w; out = out + to_string(d); }
	return to_string(s) + ":" + to_string(f) + ":" + out;
}
function mixed(int n) -> string {
	map<int, var> m = {}; m[0] = 1; m[1] = 2.5; m[2] = "text"; m[3] = true; m[4] = P(); m[5] = [1, 2]; m[6] = null; m[7] = 9;
	string out = ""; int s = 0;
	for (int i = 0; i < n; ++i) { var v = m[i & 7]; out = out + to_string(i & 7) + "=" + (i % 8 == 4 ? to_string(v.a) : to_string(v)) + ";"; if ((i & 7) == 0 || (i & 7) == 7) { s += m[i & 7]; } }
	return out + to_string(s);
}
function missing(int n) -> int { map<int, int> m = {}; m[0] = 5; m[1] = 6; int s = 0; for (int i = 0; i < n; ++i) { s += m[i]; } return s; }
function rebound(int n) -> int {
	map<int, int> m = {}; map<int, int> other = {}; for (int i = 0; i < 4; ++i) { m[i] = i; other[i] = i * 100; }
	int s = 0;
	for (int i = 0; i < n; ++i) { s += m[i & 3]; if (i == 5) { m = other; } }
	return s;
}
function also_stored(int n) -> int { map<int, int> m = {}; for (int i = 0; i < 4; ++i) { m[i] = i; } int s = 0; for (int i = 0; i < n; ++i) { s += m[i & 3]; m[(i + 1) & 3] = s; } return s; }
function shrinks(int n) -> int { map<int, int> m = {}; m[0] = 1; m[1] = 2; int s = 0; for (int i = 0; i < n; ++i) { s += m[i & 1]; if (i == 2) { m.remove(1); } s += m.size(); } return s; }
function by_reference(map<int, int>& m, int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += m[i & 3]; } return s; }
function nested(int n) -> int { map<int, array<int>> m = {}; m[0] = [1, 2]; m[1] = [3, 4]; int s = 0; for (int i = 0; i < n; ++i) { s += m[i & 1][i & 1]; } return s; }
function text_keys(int n) -> int { map<string, int> m = {}; m["a"] = 2; m["b"] = 3; array<string> ks = ["a", "b"]; int s = 0; for (int i = 0; i < n; ++i) { s += m[ks[i & 1]]; } return s; }
function overflowing(int n) -> int { map<int, int> m = {}; m[0] = 1; int s = 0; int big = 9223372036854775807; for (int i = 0; i < n; ++i) { s += m[(i & 0) * 2 + big * i]; } return s; }
function sum_ints(map<int, int> m, int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += m[(i & 3) * 2 + 1]; } return s; }
function sum_floats(map<int, float> m, int n) -> float { float s = 0.0; for (int i = 0; i < n; ++i) { s += m[i & 3]; } return s; }
function count_flags(map<int, bool> m, int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { if (m[i & 3]) { s += 1; } } return s; }
function sum_text(map<string, int> m, array<string> ks, int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { s += m[ks[i & 1]]; } return s; }
function sum_text_local(map<string, int> m, int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { string k = (i & 1) == 0 ? "a" : "b"; s += m[k]; } return s; }
function text_sizes(map<int, string> m, int n) -> int { int c = 0; for (int i = 0; i < n; ++i) { string t = m[i & 3]; c += t.size(); } return c; }
function count_open(map<int, var> m, int n) -> int { int c = 0; for (int i = 0; i < n; ++i) { if (m[i & 3]) { c += 1; } } return c; }
map<string, int> GT = {}; GT["a"] = 2; GT["b"] = 3;
array<string> KS = ["a", "b"];
map<int, string> GS = {}; GS[0] = "a"; GS[1] = "bb"; GS[2] = ""; GS[3] = "dddd";
map<int, var> GO = {}; GO[0] = true; GO[1] = 0; GO[2] = 2.5; GO[3] = 1;
map<int, var> GM = {}; GM[0] = true; GM[1] = "text"; GM[2] = false; GM[3] = [1, 2];
map<int, int> G = {}; G[0] = 1; G[1] = 2; G[2] = 3; G[3] = 4; G[5] = 6; G[7] = 8;
map<int, float> GF = {}; GF[0] = 0.5; GF[1] = 1.5; GF[2] = 2.5; GF[3] = 3.5;
map<int, bool> GB = {}; GB[0] = true; GB[1] = false; GB[2] = true; GB[3] = true;
0;)JAI";
			same_everywhere({ defs, "ints(40);", "floats(20);", "flags(20);", "declared(9);", "mixed(16);", "missing(2);", "missing(3);", "rebound(12);", "also_stored(9);", "shrinks(3);", "shrinks(5);",
				"by_reference(G, 9);", "nested(6);", "text_keys(7);", "overflowing(1);", "overflowing(3);", "sum_ints(G, 9);", "sum_floats(GF, 9);", "count_flags(GB, 9);", "sum_text(GT, KS, 9);", "sum_text_local(GT, 9);",
				"text_sizes(GS, 9);", "count_open(GO, 9);", "count_open(GM, 9);" }, "", "map reads");
			// the read is the block's own operation: the loop runs as one block with no statement parked for it and nothing missing
			auto e = jai::foundry::make_default_engine(); e->set_backend(backend_type::vm); stdlib::register_all(*e);
			e->execute(defs);
			for (const char* call : { "sum_ints(G, 400);", "sum_floats(GF, 400);", "count_flags(GB, 400);", "count_open(GO, 400);", "by_reference(G, 400);" }) {
				const cell_counters c0 = detail::engine_access::cells_counters(*e);
				e->execute(call);
				const cell_counters c1 = detail::engine_access::cells_counters(*e);
				const std::string label = std::string(call) + ": entries " + std::to_string(c1.entries - c0.entries) + ", boundaries " + std::to_string(c1.boundaries - c0.boundaries) +
				                          ", misses " + std::to_string(c1.misses - c0.misses);
				check_true(c1.entries - c0.entries >= 1, "the reading loop enters a block: " + label);
				check_eq(uint64_t{0}, c1.boundaries - c0.boundaries, "the read parks nothing: " + label);
				check_eq(uint64_t{0}, c1.misses - c0.misses, "and misses nothing: " + label);
			}
			// a map keyed by text, or one of text, is no such operation: its loop never enters a block that the key or the entry would miss out of
			for (const char* call : { "sum_text(GT, KS, 400);", "sum_text_local(GT, 400);", "text_sizes(GS, 400);" }) {
				const cell_counters c0 = detail::engine_access::cells_counters(*e);
				e->execute(call);
				const cell_counters c1 = detail::engine_access::cells_counters(*e);
				check_eq(uint64_t{0}, c1.misses - c0.misses, std::string(call) + " misses nothing");
			}
			// an open value the cells do not take costs the call one miss, not one per iteration
			{
				const cell_counters c0 = detail::engine_access::cells_counters(*e);
				e->execute("count_open(GM, 400);");
				const cell_counters c1 = detail::engine_access::cells_counters(*e);
				check_eq(uint64_t{1}, c1.misses - c0.misses, "a map of mixed values misses once in a call");
				check_eq(uint64_t{1}, c1.entries - c0.entries, "and its block is not entered again in that call");
			}
		});

		test("a_proven_map_store_in_a_loop_is_the_general_stores_own", [this]() {
			const std::string defs = R"JAI(
class P { int a = 4; }
function ints(int n) -> string { map<int, int> m = {}; for (int i = 0; i < n; ++i) { m[(i & 7) * 3 + 1] = i; } return to_string(m); }
function floats(int n) -> string { map<int, float> m = {}; for (int i = 0; i < n; ++i) { float v = i * 0.5; m[i & 3] = v; } return to_string(m); }
function flags(int n) -> string { map<int, bool> m = {}; for (int i = 0; i < n; ++i) { bool b = i % 3 == 0; m[i & 3] = b; } return to_string(m); }
function literals(int n) -> string { map<int, int> m = {}; map<int, float> f = {}; map<int, bool> b = {}; for (int i = 0; i < n; ++i) { m[i & 3] = 7; f[i & 3] = 2.5; b[i & 3] = true; } return to_string(m) + to_string(f) + to_string(b); }
function widening(int n) -> string { map<int, float> m = {}; for (int i = 0; i < n; ++i) { m[i & 3] = i; } return to_string(m); }
function narrowing(int n) -> string { map<int, int> m = {}; for (int i = 0; i < n; ++i) { var v = i * 0.5; m[i & 3] = v; } return to_string(m); }
function open_values(int n) -> string { map<int, var> m = {}; m[1] = "text"; m[2] = [1, 2]; for (int i = 0; i < n; ++i) { m[i & 3] = i; float h = i * 0.5; m[(i & 3) + 4] = h; } return to_string(m); }
function held_object(int n) -> string { map<int, var> m = {}; m[0] = P(); string out = ""; for (int i = 0; i < n; ++i) { m[i & 1] = i; out = out + to_string(m[1]); } return out + to_string(m.size()); }
function read_modify(int n) -> int { map<int, int> m = {}; for (int i = 0; i < 4; ++i) { m[i] = 0; } for (int i = 0; i < n; ++i) { m[i & 3] = m[i & 3] + i; } int s = 0; for (int i = 0; i < 4; ++i) { s += m[i] * (i + 1); } return s; }
function counting(int n) -> string { map<int, int> m = {}; for (int i = 0; i < 4; ++i) { m[i] = 0; } for (int i = 0; i < n; ++i) { int k = (i * 7) & 3; int c = m[k] + 1; m[k] = c; } return to_string(m); }
function key_kinds(int n) -> string { map<float, int> m = {}; for (int i = 0; i < n; ++i) { var k = i & 3; m[k] = i; } return to_string(m); }
function open_keys(int n) -> string { map<var, int> m = {}; m["a"] = 1; for (int i = 0; i < n; ++i) { m[i & 3] = i; } return to_string(m); }
function used_value(int n) -> int { map<int, int> m = {}; int s = 0; for (int i = 0; i < n; ++i) { int t = (m[i & 3] = i); s += t; } return s + m[1]; }
function compound(int n) -> string { map<int, int> m = {}; for (int i = 0; i < 4; ++i) { m[i] = 1; } for (int i = 0; i < n; ++i) { m[i & 3] += i; m[(i + 1) & 3] = i; } return to_string(m); }
function rebound(int n) -> string { map<int, int> m = {}; map<int, int> other = {}; for (int i = 0; i < n; ++i) { m[i & 3] = i; if (i == 5) { m = other; } } return to_string(m) + to_string(other); }
function by_value(map<int, int> m, int n) -> string { for (int i = 0; i < n; ++i) { m[i & 3] = i * 10; } return to_string(m); }
function by_reference(map<int, int>& m, int n) -> int { for (int i = 0; i < n; ++i) { m[i & 3] = i; } return m.size(); }
function fill(map<int, int> m, int n) -> int { for (int i = 0; i < n; ++i) { m[(i & 15) * 5 + 2] = i; } return m.size(); }
function tally(map<int, int> m, int n) -> int { int s = 0; for (int i = 0; i < n; ++i) { int k = i & 3; int c = m[k] + 1; m[k] = c; s += c; } return s; }
function widen(map<int, float> m, int n) -> string { for (int i = 0; i < n; ++i) { m[i & 3] = i; } return to_string(m); }
function truths(map<int, bool> m, int n) -> string { for (int i = 0; i < n; ++i) { var v = i; m[i & 3] = v; } return to_string(m); }
function text_fill(map<int, string> m, int n) -> int { for (int i = 0; i < n; ++i) { m[i & 3] = "t"; } return m.size(); }
map<int, bool> GB = {}; GB[0] = true;
map<int, string> GS = {}; GS[0] = "a";
map<int, int> G = {}; G[0] = 1; G[1] = 2; G[2] = 3; G[3] = 4;
map<int, float> GF = {}; GF[0] = 0.5;
0;)JAI";
			same_everywhere({ defs, "ints(20);", "floats(9);", "flags(9);", "literals(6);", "widening(6);", "narrowing(1);", "narrowing(3);", "open_values(6);", "held_object(4);", "read_modify(13);",
				"counting(21);", "key_kinds(3);", "open_keys(5);", "used_value(7);", "compound(9);", "rebound(9);", "by_value(G, 6);", "to_string(G);", "by_reference(G, 7);", "to_string(G);",
				"fill(G, 40);", "tally(G, 9);", "to_string(G);", "widen(GF, 7);", "truths(GB, 7);", "text_fill(GS, 7);", "to_string(GS);" }, "", "map stores");
			// the store, and a read beside it, are the block's own operations
			auto e = jai::foundry::make_default_engine(); e->set_backend(backend_type::vm); stdlib::register_all(*e);
			e->execute(defs);
			for (const char* call : { "fill(G, 400);", "tally(G, 400);", "widen(GF, 400);", "by_reference(G, 400);" }) {
				const cell_counters c0 = detail::engine_access::cells_counters(*e);
				e->execute(call);
				const cell_counters c1 = detail::engine_access::cells_counters(*e);
				const std::string label = std::string(call) + ": entries " + std::to_string(c1.entries - c0.entries) + ", boundaries " + std::to_string(c1.boundaries - c0.boundaries) +
				                          ", misses " + std::to_string(c1.misses - c0.misses);
				check_true(c1.entries - c0.entries >= 1, "the storing loop enters a block: " + label);
				check_eq(uint64_t{0}, c1.boundaries - c0.boundaries, "the store parks nothing: " + label);
				check_eq(uint64_t{0}, c1.misses - c0.misses, "and misses nothing: " + label);
			}
			// a value the map's type converts is the general store's: one miss in the call, and a map of text is never the block's
			{
				const cell_counters c0 = detail::engine_access::cells_counters(*e);
				e->execute("truths(GB, 400);");
				const cell_counters c1 = detail::engine_access::cells_counters(*e);
				check_eq(uint64_t{1}, c1.misses - c0.misses, "a converting store misses once in a call");
				e->execute("text_fill(GS, 400);");
				const cell_counters c2 = detail::engine_access::cells_counters(*e);
				check_eq(uint64_t{0}, c2.misses - c1.misses, "a store into a map of text misses nothing");
			}
		});

		test("a_map_store_in_a_block_meets_the_memory_cap_where_the_general_store_does", [this]() {
			const std::vector<std::string> steps = { R"JAI(
map<int, int> G = {};
map<int, float> GF = {};
function fill(map<int, int>& m, int n) { for (int i = 0; i < n; ++i) { m[i * 3] = i; } }
function fill_floats(map<int, float>& m, int n) { for (int i = 0; i < n; ++i) { float v = i * 0.5; m[i] = v; } }
function capped(int n) -> string { try { fill(G, n); } catch (e) { return "caught|" + e + "|" + to_string(G.size()); } return "no cap " + to_string(G.size()); }
function capped_floats(int n) -> string { try { fill_floats(GF, n); } catch (e) { return "caught|" + e + "|" + to_string(GF.size()); } return "no cap " + to_string(GF.size()); }
0;)JAI", "capped(50);", "capped(1000000);", "G.size();", "capped_floats(1000000);", "GF.size();" };
			// the vm's general store charges an insert its entry alone and the interpreter's charges more, so the count at the cap is the vm's own:
			// the block, the general form with proofs off and the reference form stop at the same store
			auto cap = [](engine& e) { e.memory_cap(262144); };
			const std::string reference = run(backend::vm_reference, steps, cap);
			check_eq(reference, run(backend::vm, steps, cap), "the cap inside a filling loop (vm)");
			check_eq(reference, run(backend::vm_no_proofs, steps, cap), "the cap inside a filling loop (vm, no proofs)");
			check(reference.find("caught|Script memory cap exceeded") != std::string::npos, "the cap raised inside the loop and was caught: " + reference);
			check(run(backend::interpreter, steps, cap).find("caught|Script memory cap exceeded") != std::string::npos, "and on the interpreter");
		});

		test("a_map_keeps_its_iteration_and_store_rules", [this]() {
			same_everywhere({ R"JAI(
class P { int a = 0; P(int v) { a = v; } }
function erase_while_iterating() -> string { map<int, int> m = {}; for (int i = 0; i < 6; ++i) { m[i] = i * 10; } string out = ""; for (auto kv : m) { out = out + to_string(kv.first); if (kv.first % 2 == 0) { m.remove(kv.first + 1); } } return out + ":" + to_string(m.size()); }
function inserts(int n) -> string { map<int, int> m = {}; map<string, int> names = {}; for (int i = 0; i < n; ++i) { m[(i & 3) * 5] = i; m[(i & 3) * 5] += 1; names["k" + to_string(i & 1)] = i; } return to_string(m) + to_string(names); }
function key_conversion() -> string { map<float, int> m = {}; for (int i = 0; i < 3; ++i) { var k = i; m[k] = i; m[i + 0.5] = i * 2; } var one = 1; return to_string(m) + to_string(m[one]) + to_string(m[1.0]); }
function wrong_key() -> int { map<string, int> m = {}; int s = 0; for (int i = 0; i < 2; ++i) { var k = i; m[k] = 1; s += 1; } return s; }
function wrong_key_read() -> int { map<int, int> m = {}; m[1] = 4; int s = 0; for (int i = 0; i < 2; ++i) { var k = "1"; s += m[k]; } return s; }
function objects(int n) -> int { map<int, shared_ptr<P>> m = {}; for (int i = 0; i < 4; ++i) { m[i] = new P(i); } int s = 0; for (int i = 0; i < n; ++i) { shared_ptr<P> p = m[i & 3]; s += p.a + m[i & 3].a; } return s; }
function missing_default(int n) -> int { map<string, int> m = {}; int s = 0; for (int i = 0; i < n; ++i) { string k = "k" + to_string(i & 1); int c = m.get(k, 0) + 1; m[k] = c; s += m[k]; } return s; }
0;)JAI", "erase_while_iterating();", "inserts(9);", "key_conversion();", "wrong_key();", "wrong_key_read();", "objects(7);", "missing_default(6);" }, "", "maps");
		});
	}
};

}

using element_rows_tests = jai::foundry::tests::element_rows_tests;
FOUNDRY_REGISTER(element_rows_tests)
