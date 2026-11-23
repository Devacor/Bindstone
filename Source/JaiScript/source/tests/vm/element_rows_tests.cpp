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
	}
};

}

using element_rows_tests = jai::foundry::tests::element_rows_tests;
FOUNDRY_REGISTER(element_rows_tests)
