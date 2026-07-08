// Regressions surfaced by the GLOOM dogfooding project (examples/gloom,
// REFERENCE.md "language-feel notes"). Each test runs on BOTH backends and
// asserts the intended behavior.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

namespace {

std::shared_ptr<jai::engine> gloom_engine(bool use_vm) {
	auto e = jai::engine::make();
	if (use_vm) { e->set_backend(jai::backend_type::vm); }
	jai::stdlib::register_all(e);
	return e;
}

std::string backend_tag(bool use_vm) { return use_vm ? "[vm] " : "[interp] "; }

} // namespace

class gloom_feedback_tests : public suite {
public:
	gloom_feedback_tests() : suite("GLOOM Feedback") {}

	void forge_tests() override {

		// ============================================================
		// Item D: ternary over element reads into a typed local.
		// `int x = c ? a[i] : b[j];` errored "Type mismatch in assignment"
		// (element reads arrive as reference wrappers; ternaries hid them from
		// the decl path's lvalue detection) while if/else worked.
		// ============================================================

		test("ternary_element_reads_into_typed_local", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					var a = [1, 2, 3];
					var b = [10, 20, 30];
					bool c = true;
					int x = c ? a[1] : b[2];
					int y = !c ? a[1] : b[2];
					x * 100 + y;
				)");
				check_eq((int64_t)230, r.as_int(), backend_tag(use_vm) + "typed local from ternary element reads");
			}
		});

		test("ternary_element_reads_typed_variants", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					var a = [1, 2, 3];
					var b = [10, 20, 30];
					var m = {"k": 7};
					bool c = true;
					float f = c ? a[1] : b[2];        // float target converts
					int mixed = c ? a[1] : 99;         // TAKEN branch is the element read
					int mk = c ? m["k"] : a[0];        // map-entry read
					int nested = c ? (c ? a[0] : a[1]) : b[0];
					to_string(f) + " " + to_string(mixed) + " " + to_string(mk) + " " + to_string(nested);
				)");
				check_eq(std::string("2.000000 2 7 1"), r.as<std::string>(), backend_tag(use_vm) + "float/mixed/map/nested ternary element reads");
			}
		});

		test("ternary_element_reads_in_function_slots", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					var a = [1, 2, 3];
					var b = [10, 20, 30];
					function pick(bool c) -> int {
						int x = c ? a[1] : b[2];
						return x;
					}
					pick(true) * 100 + pick(false);
				)");
				check_eq((int64_t)230, r.as_int(), backend_tag(use_vm) + "slot-local typed decl from ternary element reads");
			}
		});

		// The pre-fix behavior for var/auto decls was WORSE than an error: the
		// stored local silently ALIASED the element (writes through the array
		// changed the local; assigning the local wrote into the array).
		test("ternary_element_decl_never_aliases", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					var a = [1, 2, 3];
					var b = [10, 20, 30];
					bool c = true;
					var t = c ? a[1] : b[2];
					auto u = c ? a[1] : b[2];
					a[1] = 777;                       // must not change t or u
					var w = c ? a[0] : b[0];
					w = 42;                            // must not write into a[0]
					to_string(t) + " " + to_string(u) + " " + to_string(a[0]);
				)");
				check_eq(std::string("2 2 1"), r.as<std::string>(), backend_tag(use_vm) + "ternary element decl copies, never aliases");
			}
		});

		test("ternary_element_decl_deep_copies_containers", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					var rows = [[1, 2], [3, 4]];
					bool c = true;
					var row = c ? rows[0] : rows[1];
					row.push(99);                      // must not grow rows[0]
					rows[0].size() * 10 + row.size();
				)");
				check_eq((int64_t)23, r.as_int(), backend_tag(use_vm) + "container element through ternary deep-copies (value semantics)");
			}
		});

		test("typed_shared_ptr_decl_from_element_read", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					class Mob { int hp = 10; void hurt(int d) { hp = hp - d; } }
					var arr = [];
					arr.push(new Mob());
					shared_ptr<Mob> t = arr[0];        // used to error: "Cannot initialize shared_ptr with this type"
					t.hurt(3);
					arr[0].hp;
				)");
				check_eq((int64_t)7, r.as_int(), backend_tag(use_vm) + "shared_ptr<T> decl from element read shares");
			}
		});

		test("weak_ptr_decl_from_element_read", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					class Mob { int hp = 10; }
					var arr = [];
					arr.push(new Mob());
					weak_ptr<Mob> w = arr[0];
					auto locked = w.lock();
					locked.hp;
				)");
				check_eq((int64_t)10, r.as_int(), backend_tag(use_vm) + "weak_ptr<T> decl from element read observes");
			}
		});

		// Plain value decls from reference-producing initializers COPY (C++'s
		// `int x = f();` for `int& f()`); alias binding stays the auto&/ref-decl
		// spelling (pinned in vm_backend_tests ref_return_*).
		test("value_decl_from_ref_return_copies", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					int g = 5;
					int& get() { return g; }
					var x = get();
					x = 9;                             // must not write through into g
					g * 10 + x;
				)");
				check_eq((int64_t)59, r.as_int(), backend_tag(use_vm) + "plain decl from ref return copies the value");
			}
		});
	}
};

} // namespace jai::foundry::tests

using gloom_feedback_tests = jai::foundry::tests::gloom_feedback_tests;
FOUNDRY_REGISTER(gloom_feedback_tests)
