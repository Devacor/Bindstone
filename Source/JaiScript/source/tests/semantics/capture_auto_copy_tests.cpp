// Lambda value captures and auto declarations follow C++: a value object (script class or bound C++ class) copies through
// its copy constructor, a shared_ptr handle (new T, or a host shared_ptr) copies the handle and shares the object.
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

#include <memory>
#include <string>

namespace capture_auto_copy {

struct CopyCountedVec {
	static inline int copies = 0;
	int64_t x = 1;
	CopyCountedVec() = default;
	explicit CopyCountedVec(int64_t a_x) : x(a_x) {}
	CopyCountedVec(const CopyCountedVec& a_other) : x(a_other.x) { ++copies; }
	CopyCountedVec& operator=(const CopyCountedVec& a_other) = default;
	int64_t get() const { return x; }
	void set(int64_t a_x) { x = a_x; }
};

struct VecHolder {
	CopyCountedVec inner;
	CopyCountedVec& child() { return inner; }
};

}

namespace jai::foundry::tests {

class capture_auto_copy_tests : public suite {
public:
	capture_auto_copy_tests() : suite("Capture And Auto Copies") {}

	void pre_test() override { capture_auto_copy::CopyCountedVec::copies = 0; }

	std::shared_ptr<engine> make_bound_engine() {
		using capture_auto_copy::CopyCountedVec;
		using capture_auto_copy::VecHolder;
		auto result = make_engine();
		jai::stdlib::register_all(result);
		dynamic_binder<CopyCountedVec>(*result, "Vec")
			.constructor<>()
			.constructor<int64_t>()
			.property("x", &CopyCountedVec::x)
			.method("get", &CopyCountedVec::get)
			.method("set", &CopyCountedVec::set)
			.build();
		dynamic_binder<VecHolder>(*result, "VecHolder")
			.constructor<>()
			.property("inner", &VecHolder::inner)
			.method("child", &VecHolder::child)
			.build();
		result->add_function("make_vec", [](int64_t a_x) { return CopyCountedVec{a_x}; });
		result->add_function("make_shared_vec", [](int64_t a_x) { return std::make_shared<CopyCountedVec>(a_x); });
		return result;
	}

	int64_t run_int(engine& a_engine, const std::string& a_source) {
		auto value = a_engine.execute(a_source);
		check_true(!value.is_null(), "the script returned a value");
		return value.as_int();
	}

	void forge_tests() override {
		test("script_class_value_capture_copies", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)1, run_int(*eng, R"(
				class P { int x = 1; }
				int run() {
					P p = P();
					auto f = [p]() { return p.x; };
					p.x = 5;
					return f();
				}
				run()
			)"), "[p] holds a copy taken at creation");
			check_eq((int64_t)1, run_int(*eng, R"(
				int run_default() {
					P p = P();
					auto f = [=]() { return p.x; };
					p.x = 5;
					return f();
				}
				run_default()
			)"), "[=] holds a copy taken at creation");
			check_eq((int64_t)1, run_int(*eng, R"(
				int run_write() {
					P p = P();
					auto f = [p]() { p.x = 7; return p.x; };
					f();
					return p.x;
				}
				run_write()
			)"), "a write to the captured copy does not reach the original");
		});

		test("script_class_reference_and_handle_captures_share", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)5, run_int(*eng, R"(
				class P { int x = 1; }
				int run() {
					P p = P();
					auto f = [&p]() { return p.x; };
					auto g = [&]() { return p.x; };
					p.x = 5;
					return f() * 0 + g();
				}
				run()
			)"), "[&p] and [&] share the local");
			check_eq((int64_t)5, run_int(*eng, R"(
				int run_handle() {
					auto h = new P();
					auto f = [h]() { return h.x; };
					auto g = [=]() { return h.x; };
					h.x = 5;
					return f() * 0 + g();
				}
				run_handle()
			)"), "a captured handle shares its object");
		});

		test("script_class_auto_copies_a_value_and_shares_a_handle", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)1, run_int(*eng, R"(
				class P { int x = 1; }
				int run() {
					P p = P();
					auto y = p;
					y.x = 9;
					return p.x;
				}
				run()
			)"), "auto from a value copies");
			check_eq((int64_t)9, run_int(*eng, R"(
				int run_handle() {
					auto h = new P();
					auto y = h;
					y.x = 9;
					return h.x;
				}
				run_handle()
			)"), "auto from a handle shares");
		});

		test("bound_value_capture_copies_through_the_copy_constructor", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)1, run_int(*eng, R"(
				int run() {
					Vec v = Vec(1);
					auto f = [v]() { return v.x; };
					v.x = 5;
					return f();
				}
				run()
			)"), "[v] holds a copy of the C++ object");
			check_eq(1, capture_auto_copy::CopyCountedVec::copies, "the capture ran the copy constructor once");
			check_eq((int64_t)1, run_int(*eng, R"(
				int run_default() {
					Vec v = Vec(1);
					auto f = [=]() { return v.get(); };
					v.set(5);
					return f();
				}
				run_default()
			)"), "[=] holds a copy of the C++ object");
			check_eq((int64_t)1, run_int(*eng, R"(
				int run_write() {
					Vec v = Vec(1);
					auto f = [v]() { v.x = 7; return v.x; };
					f();
					return v.x;
				}
				run_write()
			)"), "a write to the captured copy does not reach the original");
			check_eq((int64_t)7, run_int(*eng, R"(
				int run_kept() {
					Vec v = Vec(1);
					auto f = [v]() { v.x = v.x + 3; return v.x; };
					f();
					return f();
				}
				run_kept()
			)"), "the lambda's copy is its own and keeps its writes between calls");
		});

		test("bound_program_scope_named_capture_copies", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)1, run_int(*eng, R"(
				Vec gv = Vec(1);
				auto f = [gv]() { return gv.x; };
				gv.x = 5;
				f()
			)"), "[g] copies a global at creation");
		});

		test("bound_reference_capture_shares", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)5, run_int(*eng, R"(
				int run() {
					Vec v = Vec(1);
					auto f = [&v]() { return v.x; };
					auto g = [&]() { return v.x; };
					v.x = 5;
					return f() * 0 + g();
				}
				run()
			)"), "[&v] and [&] share the C++ object");
			check_eq(0, capture_auto_copy::CopyCountedVec::copies, "a reference capture copies nothing");
		});

		test("bound_handle_capture_shares", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)5, run_int(*eng, R"(
				int run() {
					auto h = new Vec(1);
					auto f = [h]() { return h.x; };
					auto g = [=]() { return h.x; };
					h.x = 5;
					return f() * 0 + g();
				}
				run()
			)"), "a captured new'd handle shares its object");
			check_eq((int64_t)5, run_int(*eng, R"(
				int run_host() {
					auto h = make_shared_vec(1);
					auto f = [h]() { return h.x; };
					h.x = 5;
					return f();
				}
				run_host()
			)"), "a captured host shared_ptr shares its object");
			check_eq((int64_t)5, run_int(*eng, R"(
				int run_typed() {
					shared_ptr<Vec> h = Vec(1);
					auto f = [h]() { return h.x; };
					h.x = 5;
					return f();
				}
				run_typed()
			)"), "a captured shared_ptr<Vec> shares its object");
			check_eq(0, capture_auto_copy::CopyCountedVec::copies, "sharing a handle copies no object");
		});

		test("bound_auto_copies_a_value_and_shares_a_handle", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)1, run_int(*eng, R"(
				int run() {
					Vec v = Vec(1);
					auto y = v;
					y.x = 9;
					return v.x;
				}
				run()
			)"), "auto from a Vec value copies");
			check_eq((int64_t)1, run_int(*eng, R"(
				int run_host_value() {
					auto a = make_vec(1);
					auto b = a;
					b.x = 9;
					return a.x;
				}
				run_host_value()
			)"), "auto from a host value result copies");
			check_eq((int64_t)9, run_int(*eng, R"(
				int run_new() {
					auto h = new Vec(1);
					auto y = h;
					y.x = 9;
					return h.x;
				}
				run_new()
			)"), "auto from new shares");
			check_eq((int64_t)9, run_int(*eng, R"(
				int run_host_handle() {
					auto h = make_shared_vec(1);
					auto y = h;
					y.x = 9;
					return h.x;
				}
				run_host_handle()
			)"), "auto from a host shared_ptr shares");
		});

		test("bound_auto_from_a_member_copies", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)1, run_int(*eng, R"(
				int run() {
					VecHolder holder = VecHolder();
					auto a = holder.inner;
					a.x = 9;
					return holder.inner.x;
				}
				run()
			)"), "auto from a C++ member copies it");
		});

		test("bound_auto_from_a_reference_return_copies", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)1, run_int(*eng, R"(
				int run() {
					VecHolder holder = VecHolder();
					auto a = holder.child();
					a.x = 9;
					return holder.child().x;
				}
				run()
			)"), "auto from a call returning Vec& copies, as C++ auto drops the reference");
			check_eq((int64_t)9, run_int(*eng, R"(
				int run_ref() {
					VecHolder holder = VecHolder();
					auto& a = holder.child();
					a.x = 9;
					return holder.child().x;
				}
				run_ref()
			)"), "auto& from a call returning Vec& names the member");
		});

		test("value_declarations_from_a_handle_copy_the_object", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)1, run_int(*eng, R"(
				int run_host() {
					auto h = make_shared_vec(1);
					Vec a = h;
					a.x = 9;
					return h.x;
				}
				run_host()
			)"), "Vec a = host handle copies the object, as Vec a = *h does");
			check_eq((int64_t)1, run_int(*eng, R"(
				int run_new() {
					auto h = new Vec(1);
					Vec a = h;
					a.x = 9;
					return h.x;
				}
				run_new()
			)"), "Vec a = new'd handle copies the object");
			check_eq((int64_t)1, run_int(*eng, R"(
				class P { int x = 1; }
				int run_script() {
					auto h = new P();
					P a = h;
					a.x = 9;
					return h.x;
				}
				run_script()
			)"), "P a = handle copies the script object");
			check_eq((int64_t)1, run_int(*eng, R"(
				int run_typed_ref() {
					VecHolder holder = VecHolder();
					Vec a = holder.child();
					a.x = 9;
					return holder.child().x;
				}
				run_typed_ref()
			)"), "Vec a = a call returning Vec& copies");
		});

		test("host_shared_ptr_results_are_handles", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)9, run_int(*eng, R"(
				int run_var() {
					var h = make_shared_vec(1);
					var y = h;
					y.x = 9;
					return h.x;
				}
				run_var()
			)"), "var copies of a host shared_ptr share the object");
			check_eq((int64_t)3, run_int(*eng, R"(
				int run_weak() {
					auto h = make_shared_vec(3);
					weak_ptr<Vec> w = h;
					return w.lock().x;
				}
				run_weak()
			)"), "a host shared_ptr seeds a weak_ptr");
			check_eq((int64_t)1, run_int(*eng, R"(
				int run_value() {
					auto v = make_vec(1);
					var y = v;
					y.x = 9;
					return v.x;
				}
				run_value()
			)"), "a host result returned by value is a value");
			check_eq(2, capture_auto_copy::CopyCountedVec::copies, "the by-value result's boxing and the var copy ran the copy constructor; the handles copied none");
		});

		test("literal_elements_copy_like_captures", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)1, run_int(*eng, R"(
				int run() {
					Vec v = Vec(1);
					auto arr = [v];
					auto f = [v]() { return v.x; };
					v.x = 5;
					return arr[0].x * 10 + f() - 10;
				}
				run()
			)"), "a literal element and a value capture each hold their own copy");
			check_eq(2, capture_auto_copy::CopyCountedVec::copies, "one copy for the element, one for the capture");
			check_eq((int64_t)5, run_int(*eng, R"(
				int run_handle() {
					auto h = new Vec(1);
					auto arr = [h];
					h.x = 5;
					return arr[0].x;
				}
				run_handle()
			)"), "a literal element of a handle shares the object");
		});

		test("typed_container_elements_copy_the_object_a_handle_names", [this]() {
			auto eng = make_bound_engine();
			const char* paths[] = {
				"array<Vec> a = [h]; h.x = 5; return a[0].x;",
				"array<Vec> a; a.push(h); h.x = 5; return a[0].x;",
				"array<Vec> a = [Vec(1)]; a.insert(0, h); h.x = 5; return a[0].x;",
				"array<Vec> a = [Vec(1)]; a[0] = h; h.x = 5; return a[0].x;",
				"array<Vec> a; a.append([h]); h.x = 5; return a[0].x;",
				"array<Vec> a; a.concat([h]); h.x = 5; return a[0].x;",
				"var src = [h]; array<Vec> a = src; h.x = 5; return a[0].x;",
				"map<string, Vec> m = {\"k\": h}; h.x = 5; return m[\"k\"].x;",
				"map<string, Vec> m; m[\"k\"] = h; h.x = 5; return m[\"k\"].x;",
				"array<array<Vec>> a = [[h]]; h.x = 5; return a[0][0].x;",
			};
			int index = 0;
			for (const char* body : paths) {
				capture_auto_copy::CopyCountedVec::copies = 0;
				const std::string name = "typed_path_" + std::to_string(index++);
				check_eq((int64_t)1, run_int(*eng, "int " + name + "() { auto h = make_shared_vec(1); " + body + " } " + name + "()"),
					std::string("a host handle stored into a Vec element is a copy: ") + body);
				check_eq(1, capture_auto_copy::CopyCountedVec::copies, std::string("one copy-constructor call: ") + body);
			}
		});

		test("untyped_and_handle_containers_keep_the_handle", [this]() {
			auto eng = make_bound_engine();
			const char* paths[] = {
				"var a = [h]; h.x = 5; return a[0].x;",
				"array<var> a = [h]; h.x = 5; return a[0].x;",
				"array<shared_ptr<Vec>> a = [h]; h.x = 5; return a[0].x;",
				"array<shared_ptr<Vec>> a; a.push(h); h.x = 5; return a[0].x;",
				"map<string, shared_ptr<Vec>> m = {\"k\": h}; h.x = 5; return m[\"k\"].x;",
				"var m = {\"k\": h}; h.x = 5; return m[\"k\"].x;",
			};
			int index = 0;
			for (const char* body : paths) {
				capture_auto_copy::CopyCountedVec::copies = 0;
				const std::string name = "shared_path_" + std::to_string(index++);
				check_eq((int64_t)5, run_int(*eng, "int " + name + "() { auto h = make_shared_vec(1); " + body + " } " + name + "()"),
					std::string("an untyped or handle element shares the object: ") + body);
				check_eq(0, capture_auto_copy::CopyCountedVec::copies, std::string("no copy: ") + body);
			}
		});

		test("script_class_typed_elements_are_new_objects", [this]() {
			auto eng = make_bound_engine();
			check_eq(std::string("false false false false true true"), eng->execute(R"(
				class P { int x = 1; }
				auto h = new P();
				array<P> lit = [h];
				array<P> pushed;
				pushed.push(h);
				map<string, P> m = {"k": h};
				array<array<P>> nested = [[h]];
				var untyped = [h];
				array<shared_ptr<P>> handles = [h];
				to_string(same_object(h, lit[0])) + " " + to_string(same_object(h, pushed[0])) + " " + to_string(same_object(h, m["k"])) + " " +
					to_string(same_object(h, nested[0][0])) + " " + to_string(same_object(h, untyped[0])) + " " + to_string(same_object(h, handles[0]))
			)").as<std::string>(), "a class element holds its own object; an untyped or handle element is the handle's object");
			check_eq((int64_t)1, run_int(*eng, R"(
				array<P> values = [new P()];
				auto taken = values[0];
				taken.x = 9;
				values[0].x
			)"), "a value element copies on the way out, as any value does");
		});

		test("nested_lambdas_copy_a_captured_value", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)1, run_int(*eng, R"(
				int run() {
					Vec v = Vec(1);
					auto outer = [v]() {
						auto inner = [v]() { return v.x; };
						return inner;
					};
					auto made = outer();
					v.x = 5;
					return made();
				}
				run()
			)"), "the inner lambda's copy is independent of the outer frame's later writes");
			check_eq((int64_t)1, run_int(*eng, R"(
				int run_inner_write() {
					Vec v = Vec(1);
					auto outer = [v]() {
						auto inner = [v]() { v.x = 8; return v.x; };
						inner();
						return v.x;
					};
					return outer();
				}
				run_inner_write()
			)"), "a write to the inner lambda's copy does not reach the outer lambda's copy");
			check_eq((int64_t)5, run_int(*eng, R"(
				int run_shared() {
					auto h = new Vec(1);
					auto outer = [h]() {
						auto inner = [h]() { return h.x; };
						return inner;
					};
					auto made = outer();
					h.x = 5;
					return made();
				}
				run_shared()
			)"), "a handle stays shared through nested captures");
		});

		test("captures_of_a_field_holding_a_cpp_value_copy", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)1, run_int(*eng, R"(
				class Owner {
					Vec v;
					Owner() { v = Vec(1); }
				}
				int run() {
					Owner o = Owner();
					auto f = [o]() { return o.v.x; };
					o.v.x = 5;
					return f();
				}
				run()
			)"), "a captured script object copies its C++ field");
			check_eq((int64_t)1, run_int(*eng, R"(
				int run_init() {
					Owner o = Owner();
					auto f = [w = o.v]() { return w.x; };
					o.v.x = 5;
					return f();
				}
				run_init()
			)"), "an init-capture of a C++ field copies it");
			check_eq((int64_t)1, run_int(*eng, R"(
				class Keeper {
					Vec v;
					Keeper() { v = Vec(1); }
					auto snapshot() {
						auto held = v;
						return [held]() { return held.x; };
					}
				}
				int run_member() {
					Keeper k = Keeper();
					auto f = k.snapshot();
					k.v.x = 5;
					return f();
				}
				run_member()
			)"), "a capture of a local copied from a C++ field holds its own copy");
		});

		test("script_class_deriving_a_bound_class_captures_by_copy", [this]() {
			auto eng = make_bound_engine();
			check_eq((int64_t)1, run_int(*eng, R"(
				class Derived : Vec {
					int extra = 2;
				}
				int run() {
					Derived d = Derived();
					auto f = [d]() { return d.x; };
					d.x = 5;
					return f();
				}
				run()
			)"), "a value capture of a script class deriving a C++ class copies it");
			check_eq((int64_t)1, run_int(*eng, R"(
				int run_auto() {
					Derived d = Derived();
					auto y = d;
					y.x = 9;
					return d.x;
				}
				run_auto()
			)"), "auto from a script class deriving a C++ class copies it");
		});
	}
};

}

using capture_auto_copy_tests = jai::foundry::tests::capture_auto_copy_tests;
FOUNDRY_REGISTER(capture_auto_copy_tests)
