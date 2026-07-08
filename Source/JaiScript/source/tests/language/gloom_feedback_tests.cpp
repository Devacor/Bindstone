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

		// ============================================================
		// Item C: implicit copies of shared_ptr values must SHARE the handle
		// (guide ch03: "assignment shares instead of deep-copying"; clone() stays
		// the explicit deep copy). GLOOM: `var e = arr[i]; e.hurt()` silently
		// mutated a deep copy. The full store-boundary matrix is pinned here;
		// value-semantic elements must keep deep-copying.
		// ============================================================

		test("shared_ptr_element_reads_share", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					class Mob { int hp = 10; void hurt(int d) { hp = hp - d; } }
					var arr = [];
					arr.push(new Mob());
					var m = {};
					m["k"] = new Mob();
					var e1 = arr[0];   e1.hurt(1);      // element decl
					auto e2 = arr[0];  e2.hurt(1);      // auto element decl
					var e3 = null;     e3 = arr[0];  e3.hurt(1);   // element assign
					var mv = m["k"];   mv.hurt(1);      // map-value decl
					var mv2 = null;    mv2 = m["k"]; mv2.hurt(1);  // map-value assign
					arr[0].hp * 100 + m["k"].hp;
				)");
				check_eq((int64_t)708, r.as_int(), backend_tag(use_vm) + "element/map reads share shared_ptr handles (decl + assign)");
			}
		});

		test("shared_ptr_ident_assign_shares", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					class Mob { int hp = 10; void hurt(int d) { hp = hp - d; } }
					auto p = new Mob();
					var q = null;
					q = p;                    // assign into an any/null-typed var keeps the handle marker
					q.hurt(1);
					function f() {
						auto p2 = new Mob();
						var q2 = null;
						q2 = p2;              // slot path twin
						q2.hurt(1);
						return p2.hp;
					}
					p.hp * 100 + f();
				)");
				check_eq((int64_t)909, r.as_int(), backend_tag(use_vm) + "ident assign shares shared_ptr handles (env + slot)");
			}
		});

		test("shared_ptr_field_and_element_stores_share", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					class Mob { int hp = 10; void hurt(int d) { hp = hp - d; } }
					class Holder { var fx = null; }
					auto h = new Holder();
					var arr = [];
					arr.push(new Mob());
					h.fx = arr[0];            // field assign from element
					h.fx.hurt(1);
					var arr2 = [null];
					auto p = new Mob();
					arr2[0] = p;              // element store of a handle
					arr2[0].hurt(1);
					arr[0].hp * 100 + p.hp;
				)");
				check_eq((int64_t)909, r.as_int(), backend_tag(use_vm) + "field assign and element store share shared_ptr handles");
			}
		});

		test("shared_ptr_loop_destructure_args_share", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					class Mob { int hp = 10; void hurt(int d) { hp = hp - d; } }
					var arr = [];
					arr.push(new Mob());
					arr.push(new Mob());
					for (auto en : arr) { en.hurt(1); }          // copy range-for binding shares handles
					var [x, y] = arr;                             // destructuring shares handles
					x.hurt(1);
					function take(en) { en.hurt(1); }
					take(arr[1]);                                 // element read as by-value arg
					var m = {};
					m["a"] = new Mob();
					for (auto kv : m) { kv.second.hurt(1); }      // map iteration value shares
					arr[0].hp * 1000 + arr[1].hp * 100 + m["a"].hp;
				)");
				check_eq((int64_t)8809, r.as_int(), backend_tag(use_vm) + "range-for/destructure/args/map-iter share shared_ptr handles");
			}
		});

		test("value_semantics_still_deep_copy_at_stores", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					class Pt { int v = 1; }
					var arr = [];
					arr.push(Pt());                               // VALUE instance element
					var c = arr[0];  c.v = 9;                     // element decl copies
					var c2 = null;   c2 = arr[0]; c2.v = 8;       // element assign copies
					for (auto p : arr) { p.v = 7; }               // copy loop binding copies
					var rows = [[1], [2]];
					var [r0, r1] = rows;                          // destructuring copies containers
					r0.push(99);
					arr[0].v * 10 + rows[0].size();
				)");
				check_eq((int64_t)11, r.as_int(), backend_tag(use_vm) + "value instances and containers keep deep-copy store semantics");
			}
		});

		test("assigned_shared_ptr_keeps_reference_semantics_downstream", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					class Mob { int hp = 10; void hurt(int d) { hp = hp - d; } }
					auto p = new Mob();
					var q = null;
					q = p;
					var q2 = q;               // handle copied on through a second decl
					q2.hurt(1);
					weak_ptr<Mob> w = q;      // and stays weak_ptr-compatible
					auto locked = w.lock();
					locked.hurt(1);
					p.hp;
				)");
				check_eq((int64_t)8, r.as_int(), backend_tag(use_vm) + "assigned handle keeps shared_ptr marker downstream");
			}
		});

		// ============================================================
		// Item B: string subscript. `s[i]` is a read-only CHAR read (C++-familiar);
		// writes are a clear error (strings share storage under copy - subscript
		// writes would need copy-on-write, noted as future work). Bounds/type error
		// text matches the array style, with the numbers actually printed (the
		// placeholder machinery resolves symbol ids, so raw counts printed garbage
		// like "Array index bool out of bounds for array of size class_definition").
		// Char-char ordering comparisons ride in (the demoreel key[0] path needs
		// c0 >= '1' && c0 <= '9').
		// ============================================================

		test("string_subscript_reads_char", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					var s = "hello";
					char c = s[1];
					var parts = [];
					parts.push(type_of(c));
					parts.push(to_string(c == 'e'));
					parts.push(type_of(s.at(1)));       // at() keeps returning a 1-char string
					parts.push(to_string(s[s.size() - 1] == 'o'));
					int count = 0;
					for (int i = 0; i < s.size(); ++i) { if (s[i] == 'l') { count = count + 1; } }
					parts.push(to_string(count));
					parts.push(s);                       // source string untouched
					parts[0] + " " + parts[1] + " " + parts[2] + " " + parts[3] + " " + parts[4] + " " + parts[5];
				)");
				check_eq(std::string("char true string true 2 hello"), r.as<std::string>(), backend_tag(use_vm) + "string subscript reads chars");
			}
		});

		test("string_subscript_errors_byte_identical", [this]() {
			auto run_catch = [](bool use_vm, const char* src) -> std::string {
				auto e = gloom_engine(use_vm);
				try { e->execute(src); } catch (const std::exception& ex) { return ex.what(); }
				return "<no throw>";
			};
			// Each case: interp and vm messages must be byte-IDENTICAL, and carry the
			// expected core text (the engine boundary appends the error-code category)
			const std::pair<const char*, const char*> cases[] = {
				{ R"(var s = "hello"; s[99];)", "String index 99 out of bounds for string of size 5" },
				{ R"(var s = "hello"; s[-1];)", "String index -1 out of bounds for string of size 5" },
				{ R"(var s = "hello"; s["k"];)", "String index must be an integer" },
				{ R"(var s = "hello"; s[0] = 'x';)", "Strings are read-only through subscript: use substr()/+ to build a new string" },
				{ R"(var a = [1]; a[5];)", "Array index 5 out of bounds for array of size 1" },
			};
			for (const auto& [src, core] : cases) {
				const std::string interp_msg = run_catch(false, src);
				const std::string vm_msg = run_catch(true, src);
				check_eq(interp_msg, vm_msg, std::string("backend parity: ") + core);
				check_true(interp_msg.find(core) != std::string::npos, std::string("core text present: ") + interp_msg);
			}
		});

		test("char_ordering_comparisons", [this]() {
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					char a = 'a';
					char m = 'm';
					var ok = (a < m) && (a <= 'a') && (m > a) && (m >= 'm') && !('z' < a);
					ok;
				)");
				check_true(r.as<bool>(), backend_tag(use_vm) + "char-char ordering comparisons");
				// mixed char/int ordering stays a deliberate error (no silent promotion)
				auto e2 = gloom_engine(use_vm);
				bool threw = false;
				try { e2->execute("var x = 'a' < 1;"); } catch (const std::exception& ex) {
					threw = std::string(ex.what()).find("Invalid operands for < operator") != std::string::npos;
				}
				check_true(threw, backend_tag(use_vm) + "char/int ordering stays an error");
			}
		});

		test("demoreel_handle_key_shape_is_live", [this]() {
			// The exact examples/demoreel reel.jai handle_key shape GLOOM flagged as
			// dead (`char c0 = key[0];` would throw): now live end to end.
			for (bool use_vm : {false, true}) {
				auto e = gloom_engine(use_vm);
				auto r = e->execute(R"(
					int nscenes = 9;
					int started = -1;
					function start_scene(int i) { started = i; }
					function handle_key(string key) {
						if (key == "") { return; }
						if (key.size() == 1) {
							char c0 = key[0];
							if (c0 >= '1' && c0 <= '9') {
								int d = key.to_int();
								if (d <= nscenes) { start_scene(d - 1); }
							}
							else if (c0 == '0') { start_scene(nscenes - 1); }
						}
					}
					handle_key("7");
					int seven = started;
					handle_key("0");
					seven * 100 + started;
				)");
				check_eq((int64_t)608, r.as_int(), backend_tag(use_vm) + "reel.jai handle_key digit routing works");
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
