// Element, map entry and handle reads and writes on the vm's fused rows (the index fold, the element taken in place by a declaration
// or a member read, the map rows, the handle moves): each case holds the interpreter, the vm, the vm without proofs and the
// vm-reference backend to one result, errors and destructor order included.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
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
