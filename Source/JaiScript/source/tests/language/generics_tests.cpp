// Generics (docs/site/guide/21-generics.html): a class, function or method with type parameters is instantiated at load for
// each list of type arguments the program uses, and each instance is checked and run as if it had been written by hand
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/testing/temporary_directory.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/debug/controller.hpp>
#include <jaiscript/detail/engine_access.hpp>
#include <jaiscript/detail/type_name_key.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/serialization/convenience.hpp>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>
#include <fstream>
#include <string>

namespace jai::foundry::tests {

class generics_tests : public suite {
public:
	generics_tests() : suite("Generics") {}

	static std::string failure(engine& eng, const std::string& code) {
		try {
			eng.execute(code);
		} catch (const std::exception& e) {
			return e.what();
		}
		return "(no error)";
	}

	static bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
	// the warnings of the engine's last check, as their texts
	static std::vector<std::string> warnings(engine& eng) {
		std::vector<std::string> texts;
		for (const auto& d : eng.last_check_diagnostics().diagnostics) {
			if (d.severity == check_diagnostic::level::warning) { texts.push_back(d.message); }
		}
		return texts;
	}

	// a directory of this suite's own, emptied: the units a test writes
	static std::filesystem::path unit_directory(const std::string& test) {
		const std::filesystem::path directory = test_temp_directory() / ("generics_" + test);
		std::error_code ignored;
		std::filesystem::remove_all(directory, ignored);
		std::filesystem::create_directories(directory);
		return directory;
	}
	static std::string write_unit(const std::filesystem::path& directory, const std::string& name, const std::string& text) {
		const std::filesystem::path path = directory / name;
		std::ofstream(path, std::ios::binary) << text;
		return path.string();
	}
	static std::shared_ptr<engine> stdlib_engine() {
		auto e = make_engine();
		stdlib::register_all(*e);
		return e;
	}

	// the surface the design's samples use: the generics of docs/site/guide/21-generics.html
	static const char* declarations() {
		return R"(
			class Stack<T> {
				array<T> items;
				void push(T item) { items.push(item); }
				T pop() { T last = items[items.size() - 1]; items.pop(); return last; }
				int size() { return items.size(); }
			}
			class Pair<K, V> {
				K first;
				V second;
				Pair(K k, V v) { first = k; second = v; }
			}
			class Entity { int id = 0; }
			class Goblin : Entity { int gold = 3; }
			class Orc : Entity { int rage = 9; }
			class Pool<T : Entity> {
				array<shared_ptr<T>> live;
				static int made;
			}
			T largest<T>(array<T> xs) { T best = xs[0]; for (auto x : xs) { if (x > best) { best = x; } } return best; }
			function smallest<T>(array<T> xs) -> T { T best = xs[0]; for (auto x : xs) { if (x < best) { best = x; } } return best; }
			coroutine T each<T>(array<T> xs) { for (auto x : xs) { yield x; } }
			namespace wire { T decode<T>(string text) { return from_json<T>(text); } }
			class CartForm { string sku; int count = 0; }
			class Request {
				string body;
				T json<T>() { return from_json<T>(body); }
				static T make<T>() { return T(); }
			}
		)";
	}

	void forge_tests() override {
		test("a_generic_declaration_parses_and_runs_nothing", [this]() {
			auto e = make_engine();
			stdlib::register_all(*e);
			check_eq((int64_t)7, e->execute(std::string(declarations()) + " 7;").as_int());
		});

		test("a_generic_never_instantiated_still_fails_the_load_on_a_syntax_error", [this]() {
			auto e = make_engine();
			check_true(contains(failure(*e, "class Broken<T> { int x = ; } 1;"), "expected expression"));
			check_true(contains(failure(*e, "T broken<T>(T x) { return x +; } 1;"), "expected expression"));
			check_true(contains(failure(*e, "class Host { T broken<T>() { return ; ; ] } } 1;"), "error"));
			check_eq((int64_t)2, e->execute("1 + 1").as_int());
		});

		test("the_type_parameter_list_reports_what_this_version_leaves_out", [this]() {
			auto e = make_engine();
			check_true(contains(failure(*e, "class A<T, T> {}"), "duplicate type parameter 'T' in 'A'"));
			check_true(contains(failure(*e, "class A<> {}"), "generic 'A' lists no type parameter: name at least one (A<T>)"));
			check_true(contains(failure(*e, "class A<int N> {}"), "'A' declares a non-type parameter ('int N'): a generic's parameters are types (A<T>)"));
			check_true(contains(failure(*e, "class A<Size N> {}"), "'A' declares a non-type parameter ('Size N'): a generic's parameters are types (A<T>)"));
			check_true(contains(failure(*e, "class A<T = int> {}"), "default type arguments are not supported: declare 'T' alone and name the argument at each use"));
			check_true(contains(failure(*e, "class A<T...> {}"), "variadic type parameters are not supported: list each parameter of 'A'"));
			check_true(contains(failure(*e, "class A<int> {}"), "'A' names a type where a type parameter belongs: specialization is not supported (declare A<T> once, for every T)"));
			check_true(contains(failure(*e, "class A<typename T> {}"), "a type parameter is its name alone: A<T>, not <typename T>"));
			check_true(contains(failure(*e, "class A<class T> {}"), "a type parameter is its name alone: A<T>, not <class T>"));
			check_true(contains(failure(*e, "class A<A> {}"), "'A' is the generic's own name: its type parameter needs another"));
			check_true(contains(failure(*e, "class A<T : > {}"), "expected a class after ':': 'T : Base' bounds T by a class"));
			check_true(contains(failure(*e, "void f<T>(T x) {} void g() { T h<T>(T x) { return x; } }"),
				"generic 'h' is declared inside a function body: a generic is declared at program scope, in a namespace or in a class"));
			check_true(contains(failure(*e, "auto f = []<T>(T x) { return x; };"),
				"a lambda takes no type parameters: give it auto parameters ([](auto x) { ... }), which take any type"));
			check_true(contains(failure(*e, "T f<T>(T x) { return x; } function g = f<int>;"),
				"'f<int>' names a generic's instance, which is not a value: call it, or pass a lambda that does ((x) => f<int>(x))"));
			check_true(contains(failure(*e, "class B { override T m<T>() { return T(); } }"),
				"generic method 'm' cannot be marked override: a generic method is not virtual"));
		});

		test("a_generic_class_is_not_overloaded_by_its_parameter_count", [this]() {
			auto e = make_engine();
			const std::string text = "generic class 'Dup' is declared with 1 type parameter on line 1 and with 2 type parameters here: a generic class cannot be overloaded by its parameter count";
			const std::string unused = failure(*e, "class Dup<T> { T a; }\nclass Dup<K, V> { K a; V b; }\n1;");
			check_true(contains(unused, text), unused);
			check_true(contains(unused, ":2:"), "reported at the second declaration");
			check_true(contains(failure(*e, "class Dup<T> { T a; }\nclass Dup<K, V> { K a; V b; }\nDup<int> x; Dup<int, int> y;"), text));
			check_true(contains(failure(*e, "namespace n { class Dup<A, B> { A a; }\nclass Dup<T> { T a; } }"),
				"generic class 'n::Dup' is declared with 2 type parameters on line 1 and with 1 type parameter here: a generic class cannot be overloaded by its parameter count"));
			// the same count is the declared-again error it was, and functions still overload by their parameters
			check_true(contains(failure(*e, "class Dup<T> { T a; }\nclass Dup<U> { U a; }"), "generic 'Dup<U>' is declared again: line 1 declares it with the same parameters"));
			check_eq(std::string("1|2"), e->execute("string count<T>(T a) { return \"1\"; } string count<A, B>(A a, B b) { return \"2\"; } count(1) + \"|\" + count(1, 2);").as_string());
		});

		test("template_typename_is_a_foreign_spelling", [this]() {
			auto e = make_engine();
			const std::string text = "JaiScript has no 'template<...>' prefix: a generic lists its type parameters after its name, "
				"class Stack<T> { ... } or T largest<T>(array<T> xs) { ... }";
			check_true(contains(failure(*e, "template<typename T> class A {}"), text));
			check_true(contains(failure(*e, "template<typename T> T id(T x) { return x; }"), text));
			check_true(contains(failure(*e, "class A { template<typename T> T id(T x) { return x; } }"), text));
		});

		test("a_type_parameters_name_is_reserved_in_its_generic", [this]() {
			auto e = make_engine();
			const std::string text = "'T' is a type parameter of 'Stack': a declaration cannot reuse its name";
			check_true(contains(failure(*e, "class Stack<T> { int T; }"), text));
			check_true(contains(failure(*e, "class Stack<T> { void m() { int T = 1; } }"), text));
			check_true(contains(failure(*e, "class Stack<T> { void m(int T) {} }"), text));
			check_true(contains(failure(*e, "class Stack<T> { void T() {} }"), text));
			check_true(contains(failure(*e, "class Stack<T> { void m() { for (auto T : [1, 2]) {} } }"), text));
			check_true(contains(failure(*e, "void f<T>(array<int> T) {}"), "'T' is a type parameter of 'f': a declaration cannot reuse its name"));
			// a member read, a comparison against a construction and an instance test are no declaration
			check_eq((int64_t)1, e->execute("class Holder<T> { var held; bool m(T x) { held.T = 1; return x > T(0) || dynamic_cast<shared_ptr<T>>(x) != null; } } 1;").as_int());
		});

		// ---- classes and explicit calls

		test("a_generic_class_is_a_class_per_argument_list", [this]() {
			auto e = stdlib_engine();
			e->execute(declarations());
			check_eq((int64_t)10, e->execute("Stack<int> s; s.push(3); s.push(9); s.pop() + s.size();").as_int());
			check_eq(std::string("hp10"), e->execute("auto p = Pair<string, int>(\"hp\", 10); p.first + to_string(p.second);").as_string());
			check_eq(std::string("Stack<int>|Pair<string, int>|Stack<array<string>>"),
				e->execute("Stack<int> s; Pair<string, int> p(\"a\", 1); Stack<array<string>> n; type_of(s) + \"|\" + type_of(p) + \"|\" + type_of(n);").as_string());
			check_eq(std::string("b"), e->execute("Stack<string> names; names.push(\"a\"); names.push(\"b\"); names.pop();").as_string());
		});

		test("an_instance_constructs_by_every_spelling_a_class_does", [this]() {
			auto e = stdlib_engine();
			e->execute("class W<T> { T v; W(T x) { v = x; } T get() { return v; } }");
			check_eq((int64_t)1, e->execute("auto w = W<int>(1); w.get();").as_int());
			check_eq(std::string("q"), e->execute("auto w = W<string>{\"q\"}; w.get();").as_string());
			check_eq((int64_t)5, e->execute("W<int> w(5); w.get();").as_int());
			check_eq((int64_t)6, e->execute("W<int> w{6}; w.get();").as_int());
			check_eq((int64_t)7, e->execute("auto w = new W<int>(7); w.get();").as_int());
			check_eq((int64_t)8, e->execute("auto w = make_shared<W<int>>(8); w.get();").as_int());
			check_eq(std::string("W<int>"), e->execute("shared_ptr<W<int>> w = new W<int>(7); type_of(w);").as_string());
		});

		test("inside_an_instance_the_parameter_reads_as_its_argument", [this]() {
			auto e = stdlib_engine();
			e->execute(R"(
				class P { int n = 7; }
				class Maker<T> {
					static T make() { return T(); }
					T fresh() { T x; return x; }
					shared_ptr<T> boxed() { return new T(); }
					T from(var v) { return T(v); }
					T as(var v) { return cast<T>(v); }
					array<T> some() { array<T> xs; xs.push(T()); return xs; }
				}
				class Test<T> { bool is(var v) { return dynamic_cast<T>(v) != null; } T& same(T& v) { return dynamic_cast<T&>(v); } }
			)");
			check_eq((int64_t)0, e->execute("Maker<int>::make();").as_int());
			check_eq(std::string("!"), e->execute("Maker<string>::make() + \"!\";").as_string());
			check_eq((int64_t)7, e->execute("Maker<P>::make().n;").as_int());
			check_eq((int64_t)14, e->execute("Maker<P> m; m.fresh().n + m.boxed().n;").as_int());
			check_eq(std::string("P"), e->execute("Maker<P> m; type_of(m.boxed());").as_string());
			// T(v) is a class's construction and a primitive's conversion; T x; is each one's default
			check_eq((int64_t)2, e->execute("Maker<int> m; m.from(2.9) + m.fresh();").as_int());
			check_true(e->execute("Test<P> t; t.is(P()) && t.is(new P()) && !t.is(4);").as_bool());
			// what the argument makes of the text is the argument's own rule: a dynamic cast names a class
			const std::string builtin = failure(*e, "Test<int> t;");
			check_true(contains(builtin, "dynamic_cast takes a class: dynamic_cast<T>(e) is e as a shared_ptr<T>, null when it is no T, and dynamic_cast<T&>(e) is the reference") && contains(builtin, "in Test<int>, instantiated at <script>:1:5"));
			check_false(contains(builtin, "undeclared"));
			check_eq((int64_t)5, e->execute("Maker<int> m; var v = 4; m.as(v) + 1;").as_int());
			check_eq(std::string("1:float"), e->execute("Maker<float> m; auto xs = m.some(); to_string(xs.size()) + \":\" + type_of(xs[0]);").as_string());
		});

		test("a_primitive_constructs_by_its_own_name", [this]() {
			auto e = stdlib_engine();
			check_eq((int64_t)2, e->execute("int(2.9);").as_int());
			check_eq((int64_t)0, e->execute("int() + int{};").as_int());
			check_eq(std::string(""), e->execute("string();").as_string());
			check_false(e->execute("bool();").as_bool());
			check_eq(std::string("float"), e->execute("type_of(float(3));").as_string());
			check_true(contains(failure(*e, "int(1, 2);"), "a 'int' is initialized by one value, not 2"));
		});

		test("statics_are_per_instance", [this]() {
			auto e = stdlib_engine();
			e->execute(declarations());
			check_eq(std::string("2,1"), e->execute(R"(
				class Counter<T : Entity> { static int made = 0; void add() { made += 1; } }
				Counter<Goblin> a; Counter<Orc> b; a.add(); a.add(); b.add();
				to_string(Counter<Goblin>::made) + "," + to_string(Counter<Orc>::made);
			)").as_string());
			check_eq((int64_t)4, e->execute("Pool<Goblin>::made = 4; int made = Pool<Goblin>::made; made;").as_int());
			check_eq((int64_t)0, e->execute("Pool<Orc>::made;").as_int());
		});

		test("value_semantics_follow_the_argument", [this]() {
			auto e = stdlib_engine();
			e->execute("class Item { int n = 0; } class Cell<T> { T held; }");
			// a class argument copies with its cell, a shared_ptr argument shares
			check_eq(std::string("1,2"), e->execute("Cell<Item> a; a.held.n = 1; Cell<Item> b = a; b.held.n = 2; to_string(a.held.n) + \",\" + to_string(b.held.n);").as_string());
			check_eq(std::string("2,2"), e->execute(
				"Cell<shared_ptr<Item>> a; a.held = new Item(); a.held.n = 1; Cell<shared_ptr<Item>> b = a; b.held.n = 2; to_string(a.held.n) + \",\" + to_string(b.held.n);").as_string());
		});

		test("every_kind_of_type_is_an_argument", [this]() {
			auto e = stdlib_engine();
			e->execute("class Cell<T> { T v; }");
			check_eq(std::string("Cell<Color>:1"), e->execute("enum Color { red, green } Cell<Color> c; c.v = Color::green; type_of(c) + \":\" + to_string(c.v);").as_string());
			check_near(3.0, e->execute("Cell<math::vec2> c; c.v = math::vec2(1, 2); c.v.x + c.v.y;").as_float(), 1e-9);
			check_eq((int64_t)4, e->execute("Cell<map<string, int>> m; m.v[\"a\"] = 4; m.v[\"a\"];").as_int());
			check_eq((int64_t)8, e->execute("Cell<function> f; f.v = (x) => x * 2; f.v(4);").as_int());
			check_eq(std::string("Cell<var>"), e->execute("Cell<var> a; a.v = \"s\"; type_of(a);").as_string());
			check_eq(std::string("Cell<int>"), e->execute("Cell<Cell<int>> o; o.v.v = 3; type_of(o.v);").as_string());
			check_eq(std::string("Cell<weak_ptr<Cell<int>>>"), e->execute("Cell<weak_ptr<Cell<int>>> w; type_of(w);").as_string());
			check_true(contains(failure(*e, "Cell<auto> c;"), "a type argument names a type: 'auto' is not one an instance can be made with"));
			check_true(contains(failure(*e, "Cell<void> c;"), "a type argument names a type: 'void' is not one an instance can be made with"));
		});

		test("a_generic_names_itself_and_other_instances", [this]() {
			auto e = stdlib_engine();
			check_eq(std::string("6Box<int>"), e->execute(R"(
				class Box<T> { T item; Box(T x) { item = x; } Box<T> same() { return Box(item); } Box other() { return Box<T>(item); } }
				auto b = Box<int>(3);
				to_string(b.same().item + b.other().item) + type_of(b.same());
			)").as_string());
			check_eq(std::string("4Node<int>"), e->execute(R"(
				class Node<T> { T value; shared_ptr<Node<T>> next; }
				auto n = new Node<int>(); n.value = 4; n.next = new Node<int>();
				to_string(n.value) + type_of(n.next);
			)").as_string());
			check_eq(std::string("8true9"), e->execute(R"(
				class Base<T> { T held; Base(T x) { held = x; } }
				class Derived<T> : Base<T> { Derived(T x) : Base<T>(x) {} T twice() { return held + held; } }
				class Ints : Base<int> { Ints() : Base<int>(9) {} }
				Derived<int> d(4); Ints i;
				to_string(d.twice()) + to_string(reflect::is_instance_of(d, "Base<int>")) + to_string(i.held);
			)").as_string());
		});

		test("an_instances_name_is_its_canonical_spelling_across_namespaces", [this]() {
			auto e = stdlib_engine();
			check_eq(std::string("shop::Stack<shop::Product>|1|shop::Stack<shop::Product>|true"), e->execute(R"(
				namespace shop {
					class Product { string title = "tee"; }
					class Stack<T> { array<T> items; void push(T x) { items.push(x); } int size() { return items.size(); } }
					Stack<Product> inside;
				}
				shop::Stack<shop::Product> outside;
				outside.push(shop::Product());
				shop::inside = outside;
				type_of(outside) + "|" + to_string(shop::inside.size()) + "|" + type_of(shop::inside) + "|" + to_string(reflect::type_name(outside) == type_of(shop::inside));
			)").as_string());
		});

		test("a_generic_function_is_a_function_per_argument_list", [this]() {
			auto e = stdlib_engine();
			e->execute(declarations());
			check_eq(std::string("8,2"), e->execute("array<int> scores = [4, 8, 2]; int top = largest<int>(scores); to_string(top) + \",\" + to_string(smallest<int>(scores));").as_string());
			check_eq(std::string("pear"), e->execute("largest<string>([\"apple\", \"pear\", \"fig\"]);").as_string());
			check_eq((int64_t)6, e->execute("int sum = 0; for (auto v : each<int>([1, 2, 3])) { sum += v; } sum;").as_int());
			check_eq(std::string("abab8"), e->execute(R"(
				namespace text { T twice<T>(T x) { return x + x; } int go() { return twice<int>(4); } }
				text::twice<string>("ab") + to_string(text::go());
			)").as_string());
			check_eq(std::string("2,1"), e->execute("void swap<T>(T& a, T& b) { T t = a; a = b; b = t; } int x = 1; int y = 2; swap<int>(x, y); to_string(x) + \",\" + to_string(y);").as_string());
			check_eq((int64_t)24, e->execute("int fact<T>(T n) { return n <= 1 ? 1 : n * fact<T>(n - 1); } fact<int>(4);").as_int());
		});

		test("a_type_parameter_forwards_to_a_hosts_type_argument", [this]() {
			auto e = stdlib_engine();
			e->execute(declarations());
			check_eq(std::string("tee:2"), e->execute(R"(CartForm f = wire::decode<CartForm>("{\"sku\": \"tee\", \"count\": 2}"); f.sku + ":" + to_string(f.count);)").as_string());
		});

		test("an_instance_is_declared_the_moment_all_it_is_built_on_is", [this]() {
			// no place is computed from uses, so no shape of use can be missed: each program names the instance in an expression
			// above its first type (a construction, a call, a cast, a dynamic cast, a type argument, a lambda's or a function's body,
			// another instance's text)
			const std::string box = "class Box<T> { T v; Box() { } Box(T x) { v = x; } }\n";
			const std::pair<const char*, const char*> programs[] = {
				{"auto a = Box<int>(1);\nBox<int> b;\nto_string(a.v) + to_string(b.v);", "10"},
				{"string got = to_string(Box<int>(7).v);\narray<Box<int>> xs;\ngot + to_string(xs.size());", "70"},
				{"auto a = Box<int>(2);\nauto q = cast<Box<int>>(a);\nto_string(q.v);", "2"},
				{"auto a = Box<int>(2);\nto_string(dynamic_cast<shared_ptr<Box<int>>>(a) != null);", "true"},
				{"auto a = Box<int>(4);\nauto back = from_json<Box<int>>(to_json(a));\nto_string(back.v);", "4"},
				{"auto mk = () => Box<string>(\"s\");\nstring got = mk().v;\nmap<string, Box<string>> m;\ngot + to_string(m.size());", "s0"},
				{"int f() { auto a = Box<int>(5); return a.v; }\nstring got = to_string(f());\nBox<int> later;\ngot + to_string(later.v);", "50"},
				{"int f() { auto a = Box<int>(5); return a.v; }\nto_string(f());", "5"},
				{"class Maker<T> { int made(T x) { auto b = Box<T>(x); return b.v; } }\nMaker<int> m;\nstring got = to_string(m.made(3));\nBox<int> later;\ngot;", "3"},
			};
			for (const auto& [program, expected] : programs) {
				auto e = stdlib_engine();
				std::string got;
				try { got = e->execute(box + program).as_string(); } catch (const std::exception& error) { got = error.what(); }
				check_eq(std::string(expected), got, program);
			}
			auto e = stdlib_engine();
			// a class a type argument names, declared between the generic and the use: the instance exists once it is declared
			check_eq(std::string("falsetrue3"), e->execute(R"(
				class Shelf<T> { T v; }
				bool before = reflect::classes().contains("Shelf<Item>");
				class Item { int n = 3; }
				bool after = reflect::classes().contains("Shelf<Item>");
				Shelf<Item> b;
				to_string(before) + to_string(after) + to_string(b.v.n);
			)").as_string());
			// a function's instance a later round of the check finds an earlier call of; a method's instance likewise
			check_eq(std::string("46"), e->execute(R"(
				T first<T>(array<T> xs) { return xs[0]; }
				T wrap<T>(T x) { return x; }
				auto x = wrap(first([4, 5]));
				auto y = wrap(6);
				to_string(x) + to_string(y);
			)").as_string());
			check_eq(std::string("57"), e->execute(R"(
				class Oracle { int base = 4; T answer<T>(T x) { return x + base; } }
				Oracle o;
				auto x = wrap(o.answer(1));
				auto y = o.answer<int>(3);
				to_string(x) + to_string(y);
			)").as_string());
		});

		test("an_instances_declaration_runs_where_its_generic_and_classes_are_declared", [this]() {
			auto e = stdlib_engine();
			// its static initializers run at that moment and see what is declared above it, as a class written there would
			check_eq(std::string("70"), e->execute(R"(
				int seed() { return 5; }
				int base = 2;
				class Counter<T> { static T zero; static int made = seed() + base; T v; }
				to_string(Counter<int>::made) + to_string(Counter<int>::zero);
			)").as_string());
			check_true(failure(*e, "class Early<T> { static int made = later_value; T v; }\nint later_value = 9;\nEarly<int>::made;") != "(no error)",
				"an initializer reads nothing declared below the moment the instance is declared");
			// a class deriving from an instance; an instance deriving from a class declared further down
			check_eq(std::string("1"), e->execute("class Base<T> { T v; int tag() { return 1; } }\nclass Derived : Base<int> { }\nDerived d;\nto_string(d.tag());").as_string());
			check_eq(std::string("4"), e->execute("class Wrap<T> : Later { T v; }\nclass Later { int n = 4; }\nWrap<int> w;\nto_string(w.n);").as_string());
		});

		test("a_use_above_what_an_instance_is_built_on_is_the_order_error_of_a_plain_name", [this]() {
			auto e = stdlib_engine();
			const std::string order = ": top-level code runs in order, so declare it above the use or make the use after it";
			check_true(contains(failure(*e, "auto a = Box<int>(1);\nclass Box<T> { T v; Box(T x) { v = x; } }"), "'Box' is used before its declaration (line 2)" + order));
			check_true(contains(failure(*e, "class Crate<T> { T v; }\nauto a = Crate<Thing>();\nclass Thing { int n = 0; }"), "'Thing' is used before its declaration (line 3)" + order));
			check_true(contains(failure(*e, "int n = twice<int>(1);\nT twice<T>(T x) { return x + x; }"),
				"'twice' is called before its declaration (line 2): top-level code runs in order, so declare it above the call or make the call after it"));
			// a type above the generic is a run-time error, as a plain class's is, and says what the instance waits for
			check_true(contains(failure(*e, "Holder<int> h;\nclass Holder<T> { T held; }"), "'Holder<int>' is not declared yet: it waits for 'Holder'"));
			check_true(contains(failure(*e, "Plain p;\nclass Plain { int n = 0; }"), "No constructor found for class 'Plain'"));
		});

		test("a_functions_instance_is_declared_the_moment_its_generic_is", [this]() {
			// a call that runs above the generic's declaration fails where a plain function's does, and says what the instance waits for
			check_true(contains(failure(*stdlib_engine(), "int f() { return twice(2); }\nint r = f();\nint twice(int x) { return x + x; }"), "Undefined variable 'twice'"));
			const std::string waits = "'twice<int>' is not declared yet: it waits for 'twice'";
			check_true(contains(failure(*stdlib_engine(), "int f() { return twice<int>(2); }\nint r = f();\nT twice<T>(T x) { return x + x; }"), waits));
			check_true(contains(failure(*stdlib_engine(), "int f() { return twice(2); }\nint r = f();\nT twice<T>(T x) { return x + x; }"), waits));
			check_true(contains(failure(*stdlib_engine(), "namespace m { int f() { return twice(2); } }\nint r = m::f();\nnamespace m { T twice<T>(T x) { return x + x; } }"),
				"'m::twice<int>' is not declared yet: it waits for 'm::twice'"));
			// from its generic's declaration on it is there: a function above calls it once the declaration has run
			auto e = stdlib_engine();
			check_eq((int64_t)4, e->execute("int f() { return twice<int>(2); }\nT twice<T>(T x) { return x + x; }\nf();").as_int());
			check_eq(std::string("abab"), e->execute("namespace m { string f() { return twice(\"ab\"); } }\nnamespace m { T twice<T>(T x) { return x + x; } }\nm::f();").as_string());
			// a generic an earlier unit declared: its instance is declared as the unit that names it starts
			check_eq((int64_t)6, e->execute("int g() { return twice(3); }\ng();").as_int());
			// a generic the import in the middle of the unit brings: the instance is declared as the import runs
			const std::filesystem::path directory = unit_directory("function_link");
			write_unit(directory, "lib.jai", "T thrice<T>(T x) { return x + x + x; }\n");
			const std::string before = write_unit(directory, "before.jai", "int f() { return thrice(2); }\nint r = f();\nimport \"lib.jai\";\n");
			const std::string below = write_unit(directory, "below.jai", "int f() { return thrice(2); }\nimport \"lib.jai\";\nf();\n");
			std::string error = "(no error)";
			try { stdlib_engine()->execute_file(before); } catch (const std::exception& ex) { error = ex.what(); }
			check_true(contains(error, "'thrice<int>' is not declared yet: it waits for 'thrice'"), error);
			check_eq((int64_t)6, stdlib_engine()->execute_file(below).as_int());
			// the top-level call above the declaration is still the load error
			check_true(contains(failure(*stdlib_engine(), "int r = twice<int>(2);\nT twice<T>(T x) { return x + x; }"), "'twice' is called before its declaration (line 2)"));
		});

		test("an_instance_that_still_waits_says_what_for_when_data_or_reflection_reaches_it", [this]() {
			auto e = stdlib_engine();
			check_eq(std::string("reflect::construct: 'Rack<Part>' is not declared yet: it waits for 'Part'|'Rack<Part>' is not declared yet: it waits for 'Part'|1"), e->execute(R"(
				class Rack<T> { T v; }
				string got = "";
				try { reflect::construct("Rack<Part>"); } catch (e) { got += e; }
				got += "|";
				try { from_json("{\"$type\":\"Rack<Part>\",\"$val\":{}}"); } catch (e) { got += e; }
				class Part { int n = 1; }
				Rack<Part> rack;
				got + "|" + to_string(reflect::construct("Rack<Part>").v.n);
			)").as_string());
		});

		test("a_use_that_reaches_a_waiting_instance_says_what_it_waits_for", [this]() {
			auto e = stdlib_engine();
			const std::string waits = "'Box<Later>' is not declared yet: it waits for 'Later'";
			const std::string got = e->execute(R"(
				class Box<T> { T v; }
				void typed() { Box<Later> b; }
				void made() { auto b = Box<Later>(); }
				void shared() { auto b = new Box<Later>(); }
				void elements() { array<Box<Later>> many; many.resize(1); }
				string got = "";
				try { typed(); } catch (e) { got += e; }
				got += "|";
				try { made(); } catch (e) { got += e; }
				got += "|";
				try { shared(); } catch (e) { got += e; }
				got += "|";
				try { elements(); } catch (e) { got += e; }
				class Later { int n = 1; }
				typed(); made(); shared(); elements();
				got;
			)").as_string();
			check_eq(waits + "|" + waits + "|" + waits + "|" + waits, got);
			// a plain class not declared yet keeps the plain texts
			check_eq(std::string("No constructor found for class 'Plain'"), e->execute("string got = \"\";\nvoid typed() { Plain p; }\ntry { typed(); } catch (e) { got += e; }\nclass Plain { int n = 0; }\ngot;\n").as_string());
		});

		test("a_generic_an_import_in_the_middle_of_the_unit_brings", [this]() {
			const auto directory = unit_directory("import_in_the_middle");
			write_unit(directory, "lib.jai", "int lib_base = 3;\nclass Stack<T> {\n\tarray<T> items;\n\tstatic int made = lib_base;\n\tStack() { made += 1; }\n}\n");
			const std::string root = write_unit(directory, "root.jai", "int before = 1;\nimport \"lib.jai\";\nStack<int> s;\nto_string(before) + to_string(Stack<int>::made);\n");
			auto e = stdlib_engine();
			check_eq(std::string("14"), e->execute_file(root).as_string(), "the instance waited for the import, and its static saw the module's global");
			check_true(detail::engine_access::get_class_registry(*e).waiting_instances_.empty());
		});

		test("an_edit_to_an_argument_classs_file_reaches_the_instance", [this]() {
			const auto directory = unit_directory("argument_reload");
			const std::string item = write_unit(directory, "item.jai", "class Item {\n\tint n = 1;\n}\n");
			write_unit(directory, "user.jai", "import \"item.jai\";\nclass Box<T> {\n\tT v;\n}\nBox<Item> box;\nint user_value() { Box<Item> fresh; return fresh.v.n; }\n");
			auto e = stdlib_engine();
			e->add_include_path(directory.string());
			e->execute("import \"user.jai\";");
			check_eq((int64_t)1, e->execute("user_value();").as_int());
			auto stamp = std::filesystem::last_write_time(item);
			write_unit(directory, "item.jai", "class Item {\n\tint n = 55;\n}\n");
			std::filesystem::last_write_time(item, stamp + std::chrono::seconds(2));
			check_true(e->execute("import(\"user.jai\");").as_bool());
			check_eq((int64_t)55, e->execute("user_value();").as_int());
			check_eq(std::string("Box<Item>"), e->execute("Box<Item> again; type_of(again);").as_string());
		});

		test("errors_in_an_instance_name_the_generic_and_where_it_was_asked_for", [this]() {
			auto e = stdlib_engine();
			const std::string text = failure(*e, "class Box<T> {\n\tT item;\n\tvoid show() { print(item.nope); }\n}\nBox<int> b;\n");
			check_true(contains(text, "<script>:3:27: error: int has no member 'nope'"));
			check_true(contains(text, "    in Box<int>, instantiated at <script>:5:4"));
			// one note per level when instances nest
			const std::string nested = failure(*e, "class Inner<T> { T v; void f() { v.nope(); } }\nclass Outer<T> { Inner<T> in; }\nOuter<int> o;\n");
			check_true(contains(nested, "in Inner<int>, instantiated at <script>:2:23"));
			check_true(contains(nested, "in Outer<int>, instantiated at <script>:3:6"));
			// each instance reports what its own arguments break, and a sound instance reports nothing
			const std::string two = failure(*e, "class Named<T> { T v; string name() { return v.name; } }\nclass Dog { string name = \"rex\"; }\nNamed<Dog> d; Named<int> i; Named<float> f;\n");
			check_true(contains(two, "in Named<int>, instantiated at") && contains(two, "in Named<float>, instantiated at") && !contains(two, "in Named<Dog>"));
		});

		test("an_instance_that_cannot_be_made_says_why_at_the_use", [this]() {
			auto e = stdlib_engine();
			check_true(contains(failure(*e, "Foo<int> x;"), "<script>:1:4: error: unknown type 'Foo<int>': no generic class 'Foo' is declared"));
			check_true(contains(failure(*e, "class Foo {} Foo<int> x;"), "'Foo' takes no type arguments: it is not a generic class"));
			check_true(contains(failure(*e, "class Two<K, V> { K k; V v; } Two<int> p;"), "'Two' takes 2 type arguments, not 1"));
			check_true(contains(failure(*e, "T id<T>(T x) { return x; } id<int, string>(1);"), "'id' takes 1 type argument, not 2"));
			check_true(contains(failure(*e, "T id<T>(T x) { return x; } id<int>(1, 2);"), "'id<T>' takes 1 argument, not 2"));
			check_true(contains(failure(*e, "class Two<K, V> { K k; V v; Two(K a, V b) { k = a; v = b; } } auto p = Two(\"hp\", 10);"),
				"'Two' is a generic class: its type arguments are written, never deduced from a constructor's (Two<K, V>(...))"));
			check_true(contains(failure(*e, "int f(int x) { return x; } f<int>(1);"), "'f' takes no type arguments: a type argument goes to a host function"));
			// a parse error only an argument brings: reported in the generic, with the instance's note
			const std::string parse = failure(*e, "class Init<T> { T made() { return T(1, 2); } }\nclass Vec { Vec(int a, int b) {} }\nInit<Vec> ok; Init<int> bad;\n");
			check_true(contains(parse, "a 'int' is initialized by one value, not 2") && contains(parse, "in Init<int>, instantiated at <script>:3:19") && !contains(parse, "Init<Vec>,"));
		});

		test("instantiation_nests_sixty_four_deep_and_no_deeper", [this]() {
			auto e = stdlib_engine();
			const std::string text = failure(*e, "class A<T> { A<array<T>> next; } A<int> a;");
			check_true(contains(text, "generic instantiation nests more than 64 deep: 'A<int>' needs 'A<array<int>>' needs 'A<array<array<int>>>', and so on without end"));
			check_true(contains(text, "more instantiations around these"));
			check_eq((int64_t)2, e->execute("1 + 1").as_int());
			check_true(contains(failure(*e, "void down<T>(T x) { array<T> xs; down<array<T>>(xs); } down<int>(1);"), "generic instantiation nests more than 64 deep"));
		});

		test("a_member_that_names_an_instance_built_from_its_own_is_refused_at_the_first_step", [this]() {
			auto e = stdlib_engine();
			const std::string text = failure(*e, "class List<T> {\n\tarray<T> items;\n\tList<List<T>> chunks() { List<List<T>> out; return out; }\n}\nList<int> l;\n");
			check_true(contains(text, ":3:6: error: 'List<int>' would need 'List<List<int>>' and so on without end: the member 'chunks' names an instance of 'List' built from the "
				"instance itself; a class's members are made with it and a generic function only where it is called, so write it as one outside the class: "
				"chunks<T>(List<T> self)"), text);
			check_false(contains(text, "nests more than 64 deep"));
			check_true(contains(text, "    in List<int>, instantiated at <script>:5:5"), text);
			// a field, and an instance reached through another generic's text
			const std::string field = failure(*e, "class Tree<T> {\n\tshared_ptr<Tree<array<Tree<T>>>> up;\n}\nTree<int> t;\n");
			check_true(contains(field, "'Tree<int>' would need 'Tree<array<Tree<int>>>' and so on without end: the member 'up' names an instance of 'Tree' built from the instance "
				"itself; a class's members are made with it and a generic function only where it is called, so let a generic function outside the class make that instance"), field);
			const std::string through = failure(*e, "class A<T> { B<T> b; }\nclass B<T> {\n\tint n = 0;\n\tA<A<T>> twice() { A<A<T>> x; return x; }\n}\nA<int> a;\n");
			check_true(contains(through, ":4:3: error: 'A<int>' would need 'A<A<int>>' and so on without end: the member 'twice' names an instance of 'A' built from the instance itself"), through);
			check_true(contains(through, "so let a generic function outside the class make that instance"), through);
			// the way out the text names: a generic function is made only where it is called
			check_eq((int64_t)0, e->execute("class List<T> { array<T> items; }\nList<List<T>> chunks<T>(List<T> self) { List<List<T>> out; return out; }\nList<int> l;\nchunks(l).items.size();\n").as_int());
			// an instance that names itself, or a smaller one, is no such chain
			check_eq((int64_t)1, e->execute("class Node<T> { shared_ptr<Node<T>> next; array<Node<T>> kids; T v; }\nNode<Node<int>> n; n.v.v = 1; n.v.v;").as_int());
		});

		test("a_long_instance_name_keeps_its_two_ends_in_a_note", [this]() {
			auto e = stdlib_engine();
			const std::string text = failure(*e, "class A<T> { A<array<T>> next; } A<int> a;");
			check_true(contains(text, "generic instantiation nests more than 64 deep: 'A<int>' needs 'A<array<int>>' needs 'A<array<array<int>>>', and so on without end"));
			check_true(contains(text, "    in A<array<array<array<array<array<arra ... >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>, instantiated at <script>:1:15"), text);
			size_t longest = 0;
			for (size_t at = 0; at < text.size();) {
				const size_t end = std::min(text.find('\n', at), text.size());
				longest = std::max(longest, end - at);
				at = end + 1;
			}
			check_true(longest < 200, "no line of the listing spells a 64-deep name: " + std::to_string(longest));
		});

		test("two_units_that_use_an_instance_use_one_class", [this]() {
			const auto directory = unit_directory("two_units");
			write_unit(directory, "stack.jai", "class Stack<T> {\n\tarray<T> items;\n\tstatic int made = 0;\n\tStack() { made += 1; }\n\tvoid push(T x) { items.push(x); }\n}\n");
			write_unit(directory, "first.jai", "import \"stack.jai\";\nStack<int> first_stack;\nfirst_stack.push(1);\nStack<int> take_first() { return first_stack; }\n");
			write_unit(directory, "second.jai", "import \"stack.jai\";\nStack<int> second_stack;\nint made_in_second() { return Stack<int>::made; }\n");
			const std::string root = write_unit(directory, "root.jai",
				"import \"first.jai\";\nimport \"second.jai\";\nsecond_stack = take_first();\nsecond_stack.push(2);\n"
				"to_string(second_stack.items.size()) + \"|\" + to_string(Stack<int>::made) + \"|\" + to_string(made_in_second()) + \"|\" + type_of(second_stack);\n");
			auto e = stdlib_engine();
			// each unit declares the instance it names; the second unit's declaration finds the class there and leaves its statics alone
			check_eq(std::string("2|2|2|Stack<int>"), e->execute_file(root).as_string());
		});

		test("an_error_in_another_files_generic_names_that_file", [this]() {
			const auto directory = unit_directory("other_file");
			const std::string lib = write_unit(directory, "lib.jai", "class Box<T> {\n\tT item;\n\tint size() { return item.count; }\n}\n");
			const std::string root = write_unit(directory, "root.jai", "import \"lib.jai\";\nBox<int> b;\n");
			auto e = stdlib_engine();
			std::string text;
			try { e->execute_file(root); } catch (const std::exception& error) { text = error.what(); }
			check_true(contains(text, lib + ":3:27: error: int has no member 'count'"));
			check_true(contains(text, "    in Box<int>, instantiated at " + root + ":2:4"));
		});

		test("a_later_execute_instantiates_what_an_earlier_one_declared", [this]() {
			auto e = stdlib_engine();
			e->execute("class Cell<T> { T v; } T same<T>(T x) { return x; }");
			check_eq((int64_t)3, e->execute("Cell<int> c; c.v = same<int>(3); c.v;").as_int());
			// the generic declared again: instances made after it are made from the new text
			e->execute("T same<T>(T x) { return x + x; }");
			check_eq((int64_t)6, e->execute("Cell<int> c; c.v = same<int>(3); c.v;").as_int());
		});

		// ---- declaring it again (docs/site/guide/21-generics.html "Declaring it again")

		test("a_generic_declared_again_makes_its_live_instances_again", [this]() {
			auto e = stdlib_engine();
			e->execute("class Box<T> { T v; static int made = 0; Box() { made += 1; } int get() { return 1; } }");
			e->execute("Box<int> a; a.v = 5; int use() { Box<int> x; return x.get(); }");
			check_eq((int64_t)1, e->execute("use();").as_int());
			// the unit that declares Box again names no instance: Box<int> is made again all the same, as a class declared again is
			e->execute("class Box<T> { T v; int extra = 7; static int made = 0; Box() { made += 1; } int get() { return 3 + extra; } }");
			check_true(warnings(*e).empty());
			check_eq((int64_t)10, e->execute("a.get();").as_int(), "the object that lived takes the new body and the new field");
			check_eq((int64_t)10, e->execute("use();").as_int(), "a function compiled before the change calls the new body");
			check_eq(std::string("5|7|3"), e->execute("to_string(a.v) + \"|\" + to_string(a.extra) + \"|\" + to_string(Box<int>::made);").as_string(),
				"fields and statics keep their values, as a class declared again keeps them");
			// the plain class it mirrors
			e->execute("class Plain { int v; static int made = 0; Plain() { made += 1; } int get() { return 1; } }");
			e->execute("Plain p; p.v = 5; int use_plain() { Plain x; return x.get(); }");
			e->execute("use_plain();");
			e->execute("class Plain { int v; int extra = 7; static int made = 0; Plain() { made += 1; } int get() { return 3 + extra; } }");
			check_eq(std::string("10|10|5|7|3"), e->execute("to_string(p.get()) + \"|\" + to_string(use_plain()) + \"|\" + to_string(p.v) + \"|\" + to_string(p.extra) + \"|\" + to_string(Plain::made);").as_string());
		});

		test("the_instances_are_made_again_where_the_generic_is_declared", [this]() {
			auto e = stdlib_engine();
			e->execute("class Box<T> { int get() { return 1; } } T twice<T>(T x) { return x; }");
			e->execute("Box<int> a; int call() { return twice<int>(4); }");
			// top-level code runs in order: above the new declaration the old instance answers, below it the new one
			check_eq(std::string("1,4|2,8"), e->execute(
				"string before = to_string(a.get()) + \",\" + to_string(call());\n"
				"class Box<T> { int get() { return 2; } }\n"
				"T twice<T>(T x) { return x + x; }\n"
				"before + \"|\" + to_string(a.get()) + \",\" + to_string(call());\n").as_string());
			check_eq(std::string("1|2"), e->execute(
				"class Plain { int get() { return 1; } }\nPlain p;\nstring was = to_string(p.get());\nclass Plain { int get() { return 2; } }\nwas + \"|\" + to_string(p.get());\n").as_string(),
				"a class declared twice in a unit: each declaration runs where it stands");
		});

		test("a_generic_function_and_method_declared_again_reach_their_callers", [this]() {
			auto e = stdlib_engine();
			e->execute("T same<T>(T x) { return x; } namespace wire { T twice<T>(T x) { return x; } }");
			e->execute("int use() { return same<int>(3) + wire::twice<int>(10); }");
			check_eq((int64_t)13, e->execute("use();").as_int());
			e->execute("T same<T>(T x) { return x + x; }");
			check_eq((int64_t)16, e->execute("use();").as_int());
			e->execute("namespace wire { T twice<T>(T x) { return x + x; } }");
			check_eq((int64_t)26, e->execute("use();").as_int());
			// another parameter list is another overload beside the first, as for a plain function: the instance stays
			e->execute("T same<T>(T x, T y) { return x * y; }");
			check_eq((int64_t)26, e->execute("use();").as_int());
			check_eq((int64_t)12, e->execute("same<int>(3, 4);").as_int());
			// a generic method of a plain class, and of a generic class
			e->execute("class Tool { int level = 1; T scaled<T>(T x) { return x * level; } }");
			e->execute("Tool tool; int scale() { return tool.scaled<int>(3); }");
			check_eq((int64_t)3, e->execute("scale();").as_int());
			e->execute("class Tool { int level = 2; T scaled<T>(T x) { return x * level + 100; } }");
			check_eq((int64_t)103, e->execute("scale();").as_int(), "the object keeps its field, the method's instance is the new text");
			e->execute("class Kit<T> { T base; U plus<U>(U x) { return x; } }");
			e->execute("Kit<int> kit; int kit_use() { return kit.plus<int>(1); }");
			check_eq((int64_t)1, e->execute("kit_use();").as_int());
			e->execute("class Kit<T> { T base; U plus<U>(U x) { return x + 41; } }");
			check_true(warnings(*e).empty());
			check_eq((int64_t)42, e->execute("kit_use();").as_int());
		});

		test("an_instance_the_new_declaration_cannot_make_keeps_its_earlier_one_with_a_warning", [this]() {
			auto e = stdlib_engine();
			e->execute("class Entity { int id = 0; } class Rock { int weight = 9; }");
			e->execute("class Pool<T> { T item; int n() { return 1; } } class Box<T> { T v; int n() { return 1; } }");
			e->execute("Pool<Rock> rocks; Pool<Entity> all; Box<int> count; Box<string> words;");
			// a bound the argument no longer meets
			e->execute("class Pool<T : Entity> { T item; int n() { return 2; } }");
			check_eq(std::vector<std::string>{"'Pool<Rock>' keeps its earlier declaration: this declaration of 'Pool' cannot make it: Pool<Rock>: 'Rock' does not derive from 'Entity', which T requires"},
				warnings(*e));
			check_eq(std::string("1|2"), e->execute("to_string(rocks.n()) + \"|\" + to_string(all.n());").as_string());
			check_true(contains(failure(*e, "Pool<Rock> more;"), "Pool<Rock>: 'Rock' does not derive from 'Entity', which T requires"), "named in new code: the ordinary error");
			check_eq((int64_t)1, e->execute("rocks.n();").as_int());
			// a body that no longer checks for those arguments
			e->execute("class Box<T> {\n\tT v;\n\tint n() { return v.size() + 5; }\n}\n");
			check_eq(std::vector<std::string>{"'Box<int>' keeps its earlier declaration: this declaration of 'Box' cannot make it: int has no member 'size' (<script>:3:21)"},
				warnings(*e));
			check_eq(std::string("1|5"), e->execute("to_string(count.n()) + \"|\" + to_string(words.n());").as_string());
			check_true(contains(failure(*e, "Box<int> more;"), "int has no member 'size'"));
			// the next declaration that can make it does
			e->execute("class Box<T> { T v; int n() { return 3; } }");
			check_true(warnings(*e).empty());
			check_eq(std::string("3|3"), e->execute("to_string(count.n()) + \"|\" + to_string(words.n());").as_string());
			// a function's instance
			e->execute("T dbl<T>(T x) { return x; } int use_dbl() { return dbl<int>(4); }");
			e->execute("T dbl<T>(T x) { return x.twice(); }");
			check_eq(std::vector<std::string>{"'dbl<int>' keeps its earlier declaration: this declaration of 'dbl' cannot make it: int has no member 'twice' (<script>:1:26)"}, warnings(*e));
			check_eq((int64_t)4, e->execute("use_dbl();").as_int());
			// the unit that declares the generic again and names the instance itself: its error, not a warning
			check_true(contains(failure(*e, "class Box<T> { T v; int n() { return v.size(); } }\nBox<int> named;\n"), "int has no member 'size'"));
			check_eq((int64_t)3, e->execute("count.n();").as_int());
		});

		test("a_generic_class_declared_again_with_another_parameter_list_replaces_the_first", [this]() {
			auto e = stdlib_engine();
			e->execute("class Box<T> { T v; int n() { return 1; } }");
			e->execute("Box<int> a; a.v = 4;");
			e->execute("class Box<K, V> { K k; V v; int n() { return 2; } }");
			check_eq(std::vector<std::string>{"'Box<int>' keeps its earlier declaration: this declaration of 'Box' cannot make it: 'Box' takes 2 type arguments, not 1"}, warnings(*e));
			check_eq((int64_t)2, e->execute("Box<int, string> b; b.n();").as_int());
			check_true(contains(failure(*e, "Box<int> c;"), "'Box' takes 2 type arguments, not 1"));
			check_eq(std::string("1|4"), e->execute("to_string(a.n()) + \"|\" + to_string(a.v);").as_string(), "the instance that lived keeps working");
			// and back
			e->execute("class Box<T> { T v; int n() { return 3; } }");
			check_eq(std::vector<std::string>{"'Box<int, string>' keeps its earlier declaration: this declaration of 'Box' cannot make it: 'Box' takes 1 type argument, not 2"}, warnings(*e));
			check_eq((int64_t)3, e->execute("a.n();").as_int());
			check_eq((int64_t)2, e->execute("b.n();").as_int());
		});

		test("two_files_that_declare_one_generic_class_the_later_replaces_as_for_a_class", [this]() {
			const auto directory = unit_directory("two_files");
			// only a.jai names Box<string>: b.jai's declaration makes it again all the same, as it runs
			write_unit(directory, "a.jai", "class Box<T> { int get() { return 1; } }\nBox<int> from_a;\nBox<string> only_a;\nint a_get() { return from_a.get() * 10 + only_a.get(); }\n");
			write_unit(directory, "b.jai", "class Box<T> { int get() { return 2; } }\nint b_get() { Box<int> b; return b.get(); }\n");
			const std::string root = write_unit(directory, "root.jai", "import \"a.jai\";\nimport \"b.jai\";\nBox<int> mine;\nto_string(mine.get()) + to_string(a_get()) + to_string(b_get());\n");
			write_unit(directory, "pa.jai", "class Plain { int get() { return 1; } }\nPlain from_pa;\nint pa_get() { return from_pa.get(); }\n");
			write_unit(directory, "pb.jai", "class Plain { int get() { return 2; } }\nint pb_get() { Plain b; return b.get(); }\n");
			const std::string plain = write_unit(directory, "plain.jai", "import \"pa.jai\";\nimport \"pb.jai\";\nPlain mine;\nto_string(mine.get()) + to_string(pa_get()) + to_string(pb_get());\n");
			check_eq(std::string("222"), stdlib_engine()->execute_file(plain).as_string());
			check_eq(std::string("2222"), stdlib_engine()->execute_file(root).as_string());
			// between the two imports the first file's declaration answers
			const std::string between = write_unit(directory, "between.jai",
				"import \"a.jai\";\nstring first = to_string(a_get());\nimport \"b.jai\";\nfirst + \"|\" + to_string(a_get());\n");
			check_eq(std::string("11|22"), stdlib_engine()->execute_file(between).as_string());
		});

		test("a_class_and_a_generic_class_do_not_share_a_name", [this]() {
			auto e = stdlib_engine();
			const std::string generic_second = failure(*e, "class Box { int plain = 1; }\nclass Box<T> { T v; }\n1;");
			check_true(contains(generic_second, ":2:7: error: class 'Box' is declared without type parameters on line 1 and with 1 type parameter here: a class and a generic class cannot share a name"), generic_second);
			const std::string class_second = failure(*e, "namespace n { class Box<K, V> { K k; }\nclass Box { int plain = 1; } }\n1;");
			check_true(contains(class_second, ":2:7: error: class 'n::Box' is declared with 2 type parameters on line 1 and without type parameters here: a class and a generic class cannot share a name"), class_second);
			// from a later unit the name is taken over: the generic by the class
			e->execute("class Box<T> { T v; int n() { return 1; } }");
			e->execute("Box<int> q;");
			e->execute("class Box { int plain = 1; }");
			check_eq(std::vector<std::string>{"'Box<int>' keeps its earlier declaration: this declaration of 'Box' cannot make it: 'Box' takes no type arguments: it is not a generic class"}, warnings(*e));
			check_eq((int64_t)1, e->execute("Box p; p.plain;").as_int());
			check_true(contains(failure(*e, "Box<int> again;"), "'Box' takes no type arguments: it is not a generic class"));
			check_eq((int64_t)1, e->execute("q.n();").as_int(), "the instance that lived stays");
			// and the class by the generic: the class stays for what uses it already, the bare name is the generic's
			e->execute("int old_plain() { Box b; return b.plain; }");
			e->execute("class Box<T> { T v; int n() { return 2; } }");
			check_eq((int64_t)2, e->execute("Box<int> r; r.n();").as_int());
			const std::string bare = "'Box' is a generic class: name its type arguments (Box<T>)";
			check_true(contains(failure(*e, "Box again;"), bare));
			check_true(contains(failure(*e, "array<Box> boxes;"), bare));
			check_true(contains(failure(*e, "void take(Box b) { }"), bare));
			check_true(contains(failure(*e, "auto made = new Box();"), bare));
			check_true(contains(failure(*e, "auto held = dynamic_cast<shared_ptr<Box>>(p);"), bare));
			check_true(contains(failure(*e, "auto& same = dynamic_cast<Box&>(p);"), bare));
			check_true(contains(failure(*e, "auto same = cast<Box>(p);"), bare));
			check_true(contains(failure(*e, "class Sub : Box { }"), bare));
			check_true(contains(failure(*e, "auto made = Box();"), "'Box' is a generic class: its type arguments are written, never deduced from a constructor's (Box<T>(...))"));
			check_eq(std::string("2|1|1"), e->execute("to_string(q.n()) + \"|\" + to_string(p.plain) + \"|\" + to_string(old_plain());").as_string());
			// a bare generic name with no class behind it says the same
			check_true(contains(failure(*e, "class Lone<T> { T v; }\nLone l;"), "'Lone' is a generic class: name its type arguments (Lone<T>)"));
		});

		test("an_edit_to_a_generic_makes_its_instances_again_once_and_keeps_their_statics", [this]() {
			const auto directory = unit_directory("reload_statics");
			const std::string body = " {\n\tstatic int made = 0;\n\tBox() { made += 1; }\n\tint size() { return SIZE; }\n}\n";
			const auto lib_text = [&](const std::string& size) {
				std::string text = "class Box<T>" + body + "class Plain" + body;
				text.replace(text.rfind("Box()"), 3, "Plain");
				for (size_t at = text.find("SIZE"); at != std::string::npos; at = text.find("SIZE")) { text.replace(at, 4, size); }
				return text;
			};
			const std::string lib = write_unit(directory, "lib.jai", lib_text("1"));
			write_unit(directory, "user.jai", "import \"lib.jai\";\nBox<int> box;\nPlain plain;\n"
				"int user_value() { return box.size() * 100 + Box<int>::made; }\nint plain_value() { return plain.size() * 100 + Plain::made; }\n");
			write_unit(directory, "other.jai", "import \"lib.jai\";\nint other_value() { Box<string> s; return s.size() * 100 + Box<string>::made; }\n");
			auto e = stdlib_engine();
			e->add_include_path(directory.string());
			e->execute("import \"user.jai\"; import \"other.jai\";");
			check_eq(std::string("101|101|101"), e->execute("to_string(user_value()) + \"|\" + to_string(plain_value()) + \"|\" + to_string(other_value());").as_string());
			auto stamp = std::filesystem::last_write_time(lib);
			write_unit(directory, "lib.jai", lib_text("5"));
			std::filesystem::last_write_time(lib, stamp += std::chrono::seconds(3));
			// only user.jai is imported again: the edit reaches the instance other.jai made too, and an instance's static is what a
			// class's is after the same reload (the unit that runs again declares the instance it finds made, and resets nothing)
			check_true(e->execute("import(\"user.jai\");").as_bool());
			check_true(warnings(*e).empty());
			const std::string plain_now = e->execute("to_string(plain_value());").as_string();
			check_eq(plain_now, e->execute("to_string(user_value());").as_string(), "Box<int> beside the class written by hand");
			check_eq((int64_t)5, e->execute("plain_value() / 100;").as_int());
			check_eq((int64_t)502, e->execute("other_value();").as_int(), "Box<string>, which no unit that ran again names, was made again with its static kept");
		});

		// ---- deduction

		const std::string deducible = R"(
			T largest<T>(array<T> xs) { T best = xs[0]; for (auto x : xs) { if (x > best) { best = x; } } return best; }
			T id<T>(T x) { return x; }
			K first<K, V>(map<K, V> m) { for (auto [k, v] : m) { return k; } return K(); }
			class Box<T> { T item; Box(T x) { item = x; } }
			class Crate : Box<string> { Crate() : Box<string>("wood") {} }
			T unbox<T>(Box<T> b) { return b.item; }
			class Goblin { int gold = 3; }
			T deref<T>(shared_ptr<T> p) { return p; }
			int gold_of<T>(weak_ptr<T> w) { return w.lock().gold; }
			void swap<T>(T& a, T& b) { T t = a; a = b; b = t; }
			T either<T>(T a, T b) { return a; }
			T make<T>() { return T(); }
			U conv<U, T>(T x) { return cast<U>(x); }
			T opt<T>(T a, T b = T()) { return a; }
		)";

		test("a_call_deduces_its_type_arguments_from_its_value_arguments", [this, deducible]() {
			auto e = stdlib_engine();
			e->execute(deducible);
			check_eq((int64_t)8, e->execute("array<int> scores = [4, 8, 2]; int top = largest(scores); top;").as_int());
			check_eq(std::string("1.5b"), e->execute("to_string(largest([1.5, 0.5])) + largest([\"a\", \"b\"]);").as_string());
			check_eq(std::string("4s2.5trueGoblinGoblin"), e->execute(
				"to_string(id(4)) + id(\"s\") + to_string(id(2.5)) + to_string(id(true)) + type_of(id(Goblin())) + type_of(id(new Goblin()));").as_string());
			check_eq(std::string("k"), e->execute("map<string, int> m = {\"k\": 1}; first(m);").as_string());
			check_eq(std::string("j"), e->execute("first({\"j\": 1});").as_string());
			check_eq((int64_t)7, e->execute("auto b = Box<int>(7); unbox(b);").as_int());
			check_eq(std::string("wood"), e->execute("Crate c; unbox(c);").as_string());   // a class that derives from an instance
			check_eq((int64_t)3, e->execute("shared_ptr<Goblin> g = new Goblin(); deref(g).gold;").as_int());
			check_eq((int64_t)3, e->execute("shared_ptr<Goblin> g = new Goblin(); weak_ptr<Goblin> w = g; gold_of(w);").as_int());
			check_eq(std::string("2,1"), e->execute("int x = 1; int y = 2; swap(x, y); to_string(x) + \",\" + to_string(y);").as_string());
			check_eq(std::string("3,4"), e->execute("to_string(opt(3)) + \",\" + to_string(opt(4, 5));").as_string());
			// the arguments written fill the leading parameters, the rest are deduced
			check_eq(std::string("41!"), e->execute("conv<string>(41) + \"!\";").as_string());
			check_near(1.0, e->execute("either<float>(1, 2.5);").as_float(), 1e-9);
			check_eq(std::string("abab8"), e->execute("namespace m { T twice<T>(T x) { return x + x; } int go() { return twice(4); } } m::twice(\"ab\") + to_string(m::go());").as_string());
		});

		test("a_deduction_that_fails_says_what_to_write", [this, deducible]() {
			auto e = stdlib_engine();
			e->execute(deducible);
			check_true(contains(failure(*e, "make();"), "cannot deduce T for 'make': no parameter names it; write make<T>()"));
			check_true(contains(failure(*e, "var v = 4; largest(v);"), "cannot deduce T for 'largest': argument 1's type is not known at load; write largest<T>(...)"));
			check_true(contains(failure(*e, "largest([1, \"a\"]);"), "cannot deduce T for 'largest': argument 1's type is not known at load; write largest<T>(...)"));
			check_true(contains(failure(*e, "either(1, \"a\");"), "cannot deduce T for 'either': it is int from argument 1 and string from argument 2"));
			check_true(contains(failure(*e, "either(1, 2.5);"), "cannot deduce T for 'either': it is int from argument 1 and float from argument 2"));   // no promotion
			check_true(contains(failure(*e, "id(null);"), "cannot deduce T for 'id': argument 1 is null, which says nothing of a type; write id<T>(...)"));
			check_true(contains(failure(*e, "auto f = (x) => x; id(f);"), "cannot deduce T for 'id': argument 1 is a function, which says nothing of a type; write id<T>(...)"));
			check_true(contains(failure(*e, "largest(5);"), "cannot deduce T for 'largest': argument 1 is int, which 'array<T>' does not match; write largest<T>(...)"));
		});

		test("the_hint_spells_the_generics_own_parameters_with_the_deduced_ones_filled_in", [this, deducible]() {
			auto e = stdlib_engine();
			e->execute(deducible);
			e->execute("V key_of<K, V>(K key) { return V(); } V third<A, V, C>(A a, C c) { return V(); } V of_map<K, V>(map<K, V> m) { return V(); }");
			check_true(contains(failure(*e, "conv(1);"), "cannot deduce U for 'conv': no parameter names it; write conv<U, int>(...)"));
			check_true(contains(failure(*e, "key_of(\"hp\");"), "cannot deduce V for 'key_of': no parameter names it; write key_of<string, V>(...)"));
			check_true(contains(failure(*e, "third(1, [2.5]);"), "cannot deduce V for 'third': no parameter names it; write third<int, V, array<float>>(...)"));
			check_true(contains(failure(*e, "var v = 1; of_map(v);"), "cannot deduce K for 'of_map': argument 1's type is not known at load; write of_map<K, V>(...)"));
			check_true(contains(failure(*e, "namespace m { T make<T>() { return T(); } } m::make();"), "cannot deduce T for 'make': no parameter names it; write make<T>()"));
		});

		test("a_construction_expression_has_a_type_deduction_reads", [this, deducible]() {
			auto e = stdlib_engine();
			e->execute(deducible);
			// of<T>(x) names the T the call deduced: the argument list of the instance Of<T>
			e->execute("class Of<T> {} string of<T>(T x) { string name = type_of(Of<T>()); return name.substr(3, name.size() - 4); }");
			// a primitive's construction, a class's and an instance's, by every spelling
			check_eq(std::string("int float string bool char int"), e->execute(
				"of(int(2.5)) + \" \" + of(float(2)) + \" \" + of(string(2)) + \" \" + of(bool(1)) + \" \" + of(char(65)) + \" \" + of(int());").as_string());
			check_eq(std::string("Goblin Goblin Box<int> Box<string>"), e->execute(
				"of(Goblin()) + \" \" + of(Goblin{}) + \" \" + of(Box<int>(3)) + \" \" + of(Box<string>(\"s\"));").as_string());
			check_eq((int64_t)7, e->execute("unbox(Box<int>(7));").as_int());
			// new, make_shared and cast name what they make
			check_eq(std::string("shared_ptr<Goblin> shared_ptr<Goblin> shared_ptr<Box<int>>"), e->execute(
				"of(new Goblin()) + \" \" + of(make_shared<Goblin>()) + \" \" + of(new Box<int>(3));").as_string());
			check_eq((int64_t)3, e->execute("deref(new Goblin()).gold + deref(make_shared<Goblin>()).gold - unbox(deref(new Box<int>(3)));").as_int());
			check_eq(std::string("int Goblin float"), e->execute("of(cast<int>(2.5)) + \" \" + of(cast<Goblin>(Goblin())) + \" \" + of(cast<float>(2));").as_string());
			// a math type's constructor, a typed container's, an enum's value
			check_eq(std::string("math::vec2 math::vec3 math::color math::ivec2"), e->execute(
				"of(math::vec2(1.0, 2.0)) + \" \" + of(math::vec3(1.0, 2.0, 3.0)) + \" \" + of(math::color(1.0, 0.5, 0.0, 1.0)) + \" \" + of(math::ivec2(1, 2));").as_string());
			check_eq(std::string("array<int> map<string,int> Tint"), e->execute(
				"enum Tint { red, green } of(array<int>(3)) + \" \" + of(map<string, int>()) + \" \" + of(Tint::green);").as_string());
		});

		test("an_ordinary_function_that_fits_wins_over_a_generic", [this]() {
			auto e = stdlib_engine();
			check_eq(std::string("101s"), e->execute("T id<T>(T x) { return x; } int id(int x) { return x + 100; } to_string(id(1)) + id(\"s\");").as_string());
			check_eq(std::string("1,2"), e->execute("T one<T>(T a) { return a; } T one<T, U>(T a, U b) { return a; } to_string(one(1)) + \",\" + to_string(one(2, \"x\"));").as_string());
			const std::string both = failure(*e, "void show<T>(array<T> xs) {}\nvoid show<T>(T x) {}\nshow(5);\nshow([1]);\n");
			check_true(contains(both, "<script>:4:9: error: call to 'show' is ambiguous: the generics 'show<T>' (<script>:1:6) and 'show<T>' (<script>:2:6) both take it"));
			check_false(contains(both, "<script>:3:"));   // an int is no array<T>: one generic takes it
			check_true(contains(failure(*e, "void two<T>(T a) {}\nvoid two<U>(U b) {}\n"), "generic 'two<U>' is declared again: line 1 declares it with the same parameters"));
			check_true(contains(failure(*e, "class Dup<T> {}\nclass Dup<U> {}\n"), "generic 'Dup<U>' is declared again: line 1 declares it with the same parameters"));
		});

		// ---- bounds

		test("a_bound_is_checked_where_the_instance_is_asked_for", [this]() {
			auto e = stdlib_engine();
			e->execute(declarations());
			e->execute(R"(
				class Comparable<T> { bool before(T other) { return true; } }
				class Num : Comparable<Num> { int n = 0; }
				int ident<T : Entity>(T e) { return e.id; }
				bool sorted<T : Comparable<T>>(T a, T b) { return a.before(b); }
			)");
			check_eq((int64_t)0, e->execute("Pool<Goblin> goblins; Pool<Entity> all; goblins.live.size() + all.live.size();").as_int());
			check_eq((int64_t)0, e->execute("ident(Goblin()) + ident<Entity>(Entity());").as_int());
			check_true(e->execute("sorted(Num(), Num());").as_bool());   // the bound names the parameter: Comparable<Num>
			check_true(contains(failure(*e, "Pool<int> p;"), "<script>:1:5: error: Pool<int>: 'int' does not derive from 'Entity', which T requires"));
			check_true(contains(failure(*e, "Pool<shared_ptr<Goblin>> p;"), "Pool<shared_ptr<Goblin>>: 'shared_ptr<Goblin>' does not derive from 'Entity', which T requires"));
			check_true(contains(failure(*e, "ident(5);"), "<script>:1:8: error: ident<int>: 'int' does not derive from 'Entity', which T requires"));
			check_true(contains(failure(*e, "sorted(Goblin(), Goblin());"), "sorted<Goblin>: 'Goblin' does not derive from 'Comparable<Goblin>', which T requires"));
			// the error is the use's: the body the argument would break is not reported beside it
			const std::string only = failure(*e, "class Named<T : Entity> { T v; string name() { return v.nope; } }\nNamed<int> n;\n");
			check_true(contains(only, "Named<int>: 'int' does not derive from 'Entity', which T requires") && !contains(only, "nope"));
			check_true(contains(failure(*e, "class Bad<T : int> {}"), "expected a class after ':': 'T : Base' bounds T by a class"));
		});

		// ---- generic methods

		test("a_generic_method_is_a_method_per_argument_list", [this]() {
			auto e = stdlib_engine();
			e->execute(declarations());
			e->execute(R"(
				class Vault {
					string body = "{\"sku\": \"tee\", \"count\": 2}";
					T json<T>() { return from_json<T>(body); }
					static T make<T>() { return T(); }
					T twice<T>(T x) { return x + x; }
					T again<T>() { return json<T>(); }                 // a bare call reaches the class's own generic
					T reveal<T>() { return cast<T>(secret); }          // a method of the class: its private members are its own
				private:
					int secret = 5;
					T hidden<T>(T x) { return x; }
				}
			)");
			check_eq(std::string("tee:2"), e->execute("Vault v; CartForm f = v.json<CartForm>(); f.sku + \":\" + to_string(f.count);").as_string());
			check_eq(std::string("tee"), e->execute("Request req; req.body = \"{\\\"sku\\\": \\\"tee\\\"}\"; CartForm f = req.json<CartForm>(); f.sku;").as_string());
			check_eq((int64_t)0, e->execute("Vault::make<CartForm>().count + Vault::make<int>();").as_int());
			check_eq(std::string("8abab"), e->execute("Vault v; to_string(v.twice(4)) + v.twice<string>(\"ab\");").as_string());   // deduced, then written
			check_eq(std::string("tee"), e->execute("Vault v; v.again<CartForm>().sku;").as_string());
			check_eq((int64_t)5, e->execute("Vault v; v.reveal<int>();").as_int());
			check_eq((int64_t)2, e->execute("shared_ptr<Vault> v = new Vault(); v.json<CartForm>().count;").as_int());
			check_true(contains(failure(*e, "Vault v; v.hidden<int>(1);"), "private"));
			check_true(contains(failure(*e, "Vault v; v.json();"), "cannot deduce T for 'json': no parameter names it; write json<T>()"));
		});

		test("the_receivers_class_picks_a_generic_methods_instance", [this]() {
			auto e = stdlib_engine();
			e->execute(R"(
				class Base { string tag<T>() { return "base"; } string kept<T>() { return "kept"; } }
				class Derived : Base { string tag<T>() { return "derived"; } }
				class Box<T> { T item; U apply<U>(function f) { return f(item); } static string kind<U>() { return "box"; } }
			)");
			// a derived class's generic of the name hides the base's; one it does not declare is the base's
			check_eq(std::string("base|derived|kept"), e->execute("Base b; Derived d; b.tag<int>() + \"|\" + d.tag<int>() + \"|\" + d.kept<int>();").as_string());
			// not virtual: the class the load knows picks the instance
			check_eq(std::string("base"), e->execute("Derived d; Base& b = d; b.tag<int>();").as_string());
			check_eq(std::string("4!box"), e->execute("Box<int> b; b.item = 4; b.apply<string>((x) => to_string(x) + \"!\") + Box<int>::kind<float>();").as_string());
			check_true(contains(failure(*e, "var b = Base(); b.tag<int>();"), "cannot instantiate 'tag<int>': the receiver's class is not known at load"));
			check_true(contains(failure(*e, "class A { T m<T>() { return T(); } T m<U>() { return U(); } }"), "generic 'A::m<U>' is declared again: line 1 declares it with the same parameters"));
		});

		test("a_generic_method_called_through_a_receiver_of_unknown_class_says_what_to_write", [this]() {
			auto e = stdlib_engine();
			e->execute("class Base { T twice<T>(T x) { return x + x; } int plain() { return 1; } } class Derived : Base {}");
			const std::string text = "Base::twice is a generic method: its instance is picked where the call is written, from the receiver's class, which is not known there; "
				"give the receiver a typed name (Base x = ...) or cast it (cast<Base&>(x).twice(...))";
			check_eq(text, failure(*e, "var b = Base(); b.twice(4);").substr(0, text.size()));
			// the instance exists by then (another call made it): the receiver's class is still what picks it
			check_eq(text, failure(*e, "Base typed; typed.twice(4); var b = Base(); b.twice(4);").substr(0, text.size()));
			check_true(contains(failure(*e, "var d = Derived(); d.twice(4);"),
				"Base::twice is a generic method: its instance is picked where the call is written, from the receiver's class, which is not known there; "
				"give the receiver a typed name (Derived x = ...) or cast it (cast<Derived&>(x).twice(...))"));
			// what the text says to write
			check_eq((int64_t)8, e->execute("var b = Base(); Base typed = b; typed.twice(4);").as_int());
			check_eq((int64_t)8, e->execute("var b = Base(); cast<Base&>(b).twice(4);").as_int());
			// a member the class does not have is what it was
			check_true(contains(failure(*e, "var b = Base(); b.nope(4);"), "Base has no member 'nope'"));
			check_eq((int64_t)1, e->execute("var b = Base(); b.plain();").as_int());
		});

		test("units_that_call_a_generic_methods_instance_share_it", [this]() {
			const auto directory = unit_directory("method_units");
			write_unit(directory, "counter.jai", "class Counter {\n\tint n = 0;\n\tT bump<T>(T by) { n += 1; return by; }\n}\nCounter shared_counter;\n");
			write_unit(directory, "first.jai", "import \"counter.jai\";\nint first_bump() { return shared_counter.bump<int>(1); }\n");
			write_unit(directory, "second.jai", "import \"counter.jai\";\nint second_bump() { return shared_counter.bump<int>(2); }\n");
			const std::string root = write_unit(directory, "root.jai",
				"import \"first.jai\";\nimport \"second.jai\";\nto_string(first_bump() + second_bump()) + \":\" + to_string(shared_counter.n);\n");
			auto e = stdlib_engine();
			check_eq(std::string("3:2"), e->execute_file(root).as_string());
		});

		test("a_generic_method_is_called_from_a_function_above_its_class", [this]() {
			auto e = stdlib_engine();
			// the instance is declared ahead of the function that first calls it; the class takes it when its own declaration runs
			check_eq((int64_t)7, e->execute(R"(
				int ask(Oracle o) { return o.answer<int>(3); }
				class Oracle { int base = 4; T answer<T>(T x) { return x + base; } }
				ask(Oracle());
			)").as_int());
			check_true(detail::engine_access::get_class_registry(*e).waiting_instances_.empty(), "the class took what waited for it");
		});

		test("a_generic_method_is_called_from_a_function_above_its_classs_import", [this]() {
			const auto directory = unit_directory("method_above_import");
			const std::string oracle = write_unit(directory, "oracle.jai", "class Oracle {\n\tint base = 4;\n\tT answer<T>(T x) { return x + base; }\n\tstatic T zero<T>() { return T(); }\n}\n");
			write_unit(directory, "user.jai", "int ask(Oracle o) { return o.answer<int>(3) + Oracle::zero<int>(); }\nimport \"oracle.jai\";\n");
			auto e = stdlib_engine();
			e->add_include_path(directory.string());
			e->execute("import \"user.jai\";");
			check_eq((int64_t)7, e->execute("ask(Oracle());").as_int());
			check_true(detail::engine_access::get_class_registry(*e).waiting_instances_.empty());
			// the class's file again: the class holds the instances its caller declared
			auto stamp = std::filesystem::last_write_time(oracle);
			write_unit(directory, "oracle.jai", "class Oracle {\n\tint base = 10;\n\tT answer<T>(T x) { return x + base; }\n\tstatic T zero<T>() { return T(); }\n}\n");
			std::filesystem::last_write_time(oracle, stamp + std::chrono::seconds(2));
			check_true(e->execute("import(\"user.jai\");").as_bool());
			check_eq((int64_t)13, e->execute("ask(Oracle());").as_int());
			check_true(detail::engine_access::get_class_registry(*e).waiting_instances_.empty());
		});

		test("a_call_ahead_of_the_class_fails_as_any_use_of_a_class_not_yet_declared", [this]() {
			auto e = stdlib_engine();
			// the instances wait for the class; a call that runs first finds no class, as a call of a plain method does
			check_eq(std::string("Class 'Oracle' not found|No constructor found for class 'Oracle'|0|7"), e->execute(R"(
				int zero() { return Oracle::zero<int>(); }
				int ask() { Oracle o; return o.answer<int>(3); }
				string early = "";
				try { zero(); } catch (e) { early += e; }
				early += "|";
				try { ask(); } catch (e) { early += e; }
				class Oracle { int base = 4; T answer<T>(T x) { return x + base; } static T zero<T>() { return T(); } }
				early + "|" + to_string(zero()) + "|" + to_string(ask());
			)").as_string());
		});

		test("an_instance_whose_class_is_never_declared_waits_as_one_entry", [this]() {
			auto e = stdlib_engine();
			auto& awaited = detail::engine_access::get_class_registry(*e).waiting_instances_;
			const std::string stopped = R"(
				int ask(Oracle o) { return o.answer<int>(3); }
				throw "stopped";
				class Oracle { int base = 4; T answer<T>(T x) { return x + base; } }
			)";
			for (int run = 0; run < 3; ++run) {   // each run declares the instance again: one entry, whatever the count of runs
				check_true(contains(failure(*e, stopped), "stopped"));
				check_eq((size_t)1, awaited.size());
			}
			check_true(contains(failure(*e, "Oracle o;"), "unknown type 'Oracle'"), "the instance that waits makes no class");
			// the class, declared at last, takes it: the function the stopped unit left calls the method
			e->execute("class Oracle { int base = 4; T answer<T>(T x) { return x + base; } }");
			check_true(awaited.empty());
			check_eq((int64_t)7, e->execute("ask(Oracle());").as_int());
		});

		test("a_class_declared_again_keeps_the_instances_that_waited_for_it", [this]() {
			auto e = stdlib_engine();
			e->execute(R"(
				int ask(Oracle o) { return o.answer<int>(3); }
				class Oracle { int base = 4; T answer<T>(T x) { return x + base; } }
			)");
			e->execute("class Oracle { int base = 5; T answer<T>(T x) { return x + base; } }");
			check_eq((int64_t)8, e->execute("ask(Oracle());").as_int());
		});

		test("a_class_declared_again_keeps_its_generic_methods_instances", [this]() {
			auto e = stdlib_engine();
			e->execute("class Tool { int level = 1; T scaled<T>(T x) { return x * level; } }");
			e->execute("Tool tool; int use() { return tool.scaled<int>(3); }");
			check_eq((int64_t)3, e->execute("use();").as_int());
			e->execute("class Tool { int level = 2; T scaled<T>(T x) { return x * level; } }");   // the class again: the instance its caller declared stays
			check_eq((int64_t)6, e->execute("Tool fresh; fresh.scaled<int>(3);").as_int());
		});

		// ---- the rest of the engine sees ordinary declarations

		test("an_edit_to_a_generic_reruns_the_modules_that_instantiate_it", [this]() {
			const auto directory = unit_directory("reload");
			const std::string lib = write_unit(directory, "lib.jai", "T tagged<T>(T x) { return x; }\nclass Box<T> {\n\tT item;\n\tint size() { return 1; }\n}\n");
			write_unit(directory, "user.jai", "import \"lib.jai\";\nuser_ran();\nBox<int> box;\nint user_value() { return tagged<int>(1) + box.size(); }\n");
			auto e = stdlib_engine();
			e->add_include_path(directory.string());
			int runs = 0;
			e->add_function("user_ran", [&runs]() { ++runs; });
			e->execute("import \"user.jai\";");
			check_eq((int64_t)2, e->execute("user_value();").as_int());
			check_eq(1, runs);
			check_false(e->execute("import(\"user.jai\");").as_bool(), "nothing moved: nothing re-runs");
			// the generic's file is part of the module that instantiates it: its edit re-runs that module
			auto stamp = std::filesystem::last_write_time(lib);
			write_unit(directory, "lib.jai", "T tagged<T>(T x) { return x + 10; }\nclass Box<T> {\n\tT item;\n\tint size() { return 5; }\n}\n");
			std::filesystem::last_write_time(lib, stamp += std::chrono::seconds(3));
			check_true(e->execute("import(\"user.jai\");").as_bool());
			check_eq(2, runs);
			check_eq((int64_t)16, e->execute("user_value();").as_int());
			// a broken edit is reported and the old code keeps running
			write_unit(directory, "lib.jai", "T tagged<T>(T x) { return x.nope; }\nclass Box<T> {\n\tT item;\n\tint size() { return 5; }\n}\n");
			std::filesystem::last_write_time(lib, stamp += std::chrono::seconds(3));
			const std::string broken = failure(*e, "import(\"user.jai\");");
			check_true(contains(broken, "int has no member 'nope'") && contains(broken, "in tagged<int>, instantiated at"));
			check_eq(2, runs);
			check_eq((int64_t)16, e->execute("user_value();").as_int());
		});

		test("a_unit_that_instantiates_another_files_generic_keeps_no_jaibite_cache", [this]() {
			const auto directory = unit_directory("cache");
			const std::string lib = write_unit(directory, "lib.jai", "class Box<T> {\n\tT item;\n}\nBox<int> own;\n");
			const std::string user = write_unit(directory, "user.jai", "import \"lib.jai\";\nBox<string> b;\nb.item = \"x\";\nb.item;\n");
			auto e = stdlib_engine();
			check_eq(std::string("x"), e->execute_file(user).as_string());
			// lib.jai instantiates its own generic: cached with its instance. user.jai rests on lib.jai's text: no cache holds it
			check_true(std::filesystem::exists(jaibite_cache_beside(lib)));
			check_false(std::filesystem::exists(jaibite_cache_beside(user)));
			auto again = stdlib_engine();
			check_eq(std::string("x"), again->execute_file(user).as_string());   // the cached lib.jai still declares its generic
		});

		test("a_jaibite_keeps_a_generic_and_its_instances", [this]() {
			auto e = stdlib_engine();
			auto bite = e->jaibite("class Cell<T> { T v; T twice() { return v + v; } }\nT same<T>(T x) { return x; }\nCell<int> c;\nc.v = same(4);\nc.twice();\n");
			check_eq((int64_t)8, bite.execute().as_int());
			const std::vector<uint8_t> bytes = bite.save_bytes();
			auto loader = stdlib_engine();
			auto loaded = loader->jaibite_load_bytes(bytes);
			check_eq((int64_t)8, loaded.execute().as_int());
			// the generic a bite declares instantiates for the units that run after it
			check_eq(std::string("abab|Cell<string>"), loader->execute("Cell<string> s; s.v = same(\"ab\"); s.twice() + \"|\" + type_of(s);").as_string());
			// a bite that only uses a generic carries its instances
			auto user = loader->jaibite("Cell<float> f; f.v = 1.5; f.twice();");
			auto second = stdlib_engine();
			second->execute("class Cell<T> { T v; T twice() { return v + v; } }");
			auto reloaded = second->jaibite_load_bytes(user.save_bytes());
			check_near(3.0, reloaded.execute().as_float(), 1e-9);
		});

		test("a_cached_unit_keeps_its_instances_where_the_first_use_put_them", [this]() {
			const auto directory = unit_directory("cached_placement");
			const std::string root = write_unit(directory, "root.jai",
				"class Box<T> { T v; Box() { } Box(T x) { v = x; } }\nauto a = Box<int>(1);\nBox<int> b;\nto_string(a.v) + to_string(b.v);\n");
			check_eq(std::string("10"), stdlib_engine()->execute_file(root).as_string());
			check_true(std::filesystem::exists(jaibite_cache_beside(root)));
			check_eq(std::string("10"), stdlib_engine()->execute_file(root).as_string(), "the cache's declarations stand in the order the check left them");
		});

		test("reflection_and_serialization_name_an_instance_as_a_class", [this]() {
			auto e = stdlib_engine();
			e->execute("class Cell<T> { T v; }\nclass User { string name; }\nclass Page<T> { array<T> rows; int total = 0; }");
			check_eq(std::string("Cell<int>|true"), e->execute("Cell<int> c; reflect::type_name(c) + \"|\" + to_string(reflect::classes().contains(\"Cell<int>\"));").as_string());
			check_eq(std::string("{\"$type\":\"Cell<int>\",\"$val\":{\"v\":4}}"), e->execute("Cell<int> c; c.v = 4; to_json(c);").as_string());
			check_eq(std::string("4Cell<int>"), e->execute("Cell<int> c; c.v = 4; var back = from_json(to_json(c)); to_string(back.v) + type_of(back);").as_string());
			check_eq(std::string("int"), e->execute("Cell<int> c; reflect::fields(c)[0][\"type\"];").as_string());
			// a type argument to a host names an instance like any class
			check_eq(std::string("ann1Page<User>"), e->execute(
				R"(Page<User> p = from_json<Page<User>>("{\"rows\": [{\"name\": \"ann\"}], \"total\": 1}"); p.rows[0].name + to_string(p.total) + type_of(p);)").as_string());
		});

		test("a_type_naming_an_instance_the_program_never_made_raises", [this]() {
			auto e = stdlib_engine();
			e->execute("class Cell<T> { T v; }\nCell<int> made;");
			const std::string text = "'Cell<float>' names an instance of the generic 'Cell' that the program never made: name the type where it is loaded, as from_json<Cell<float>>(text) does";
			check_true(contains(failure(*e, R"(from_json("{\"$type\":\"Cell<float>\",\"$val\":{\"v\":1.5}}");)"), text));
			check_eq(text, e->execute(R"(string got = ""; try { from_json("[{\"$type\":\"Cell<float>\",\"$val\":{\"v\":1.5}}]"); } catch (e) { got = e; } got;)").as_string());
			// named where it is loaded, the instance is made; a name no generic declares is data, as any unknown class's is
			check_eq(std::string("1.5Cell<float>"), e->execute(R"(auto c = from_json<Cell<float>>("{\"$type\":\"Cell<float>\",\"$val\":{\"v\":1.5}}"); to_string(c.v) + type_of(c);)").as_string());
			check_eq(std::string("map"), e->execute(R"(type_of(from_json("{\"$type\":\"Shelf<float>\",\"$val\":{\"v\":1.5}}"));)").as_string());
			check_eq(std::string("map"), e->execute(R"(type_of(from_json("{\"$type\":\"Nowhere\",\"$val\":{\"v\":1.5}}"));)").as_string());
		});

		test("printed_and_saved_type_names_keep_their_spellings", [this]() {
			auto e = stdlib_engine();
			e->execute("class Duo<A, B> { A a; B b; } class Box<T> { T v; } namespace shop { class Item { int n = 2; } class Shelf<T> { T item; } }"
			           " Duo<int, string> d; Box<map<string, int>> bm; Box<Duo<int, array<float>>> nested; shop::Shelf<shop::Item> shelf; Box<var> any; Box<Box<var>> inner; Box<array<var>> list;");
			// an instance joins its arguments with ", ", a builtin container with ","
			check_eq(std::string("Duo<int, string>|Box<map<string,int>>|Box<Duo<int, array<float>>>|shop::Shelf<shop::Item>"),
				e->execute("type_of(d) + \"|\" + type_of(bm) + \"|\" + reflect::type_name(nested) + \"|\" + type_of(shelf);").as_string());
			check_eq(std::string("{\"$type\":\"Duo<int, string>\",\"$val\":{\"a\":0,\"b\":\"\"}}"), e->execute("to_json(d);").as_string());
			check_eq(std::string("{\"$type\":\"Box<map<string,int>>\",\"$val\":{\"v\":{}}}"), e->execute("to_json(bm);").as_string());
			check_true(e->execute("reflect::classes().contains(\"Duo<int, string>\") && !reflect::classes().contains(\"Duo<int,string>\");").as_bool());
			check_true(contains(e->execute("to_binary(d);").as_string(), "Duo<int, string>"));
			check_true(contains(e->execute("to_binary(bm);").as_string(), "Box<map<string,int>>"));
			// var is spelled var in an instance's name, at any depth; a builtin container's own name is what it was
			check_eq(std::string("Box<var>|Box<Box<var>>|Box<array<var>>"), e->execute("type_of(any) + \"|\" + type_of(inner) + \"|\" + type_of(list);").as_string());
			check_eq(std::string("{\"$type\":\"Box<var>\",\"$val\":{\"v\":null}}"), e->execute("to_json(any);").as_string());
			check_eq(std::string("Box<var>|7"), e->execute("any.v = 7; auto back = from_json(to_json(any)); type_of(back) + \"|\" + to_string(back.v);").as_string());
			check_eq(std::string("array<any>"), detail::engine_access::get_type_info_array(*e, detail::engine_access::get_type_info_any(*e))->canonical_name());
			check_eq(std::string("array<var>"), detail::engine_access::get_type_info_array(*e, detail::engine_access::get_type_info_any(*e))->spelling());
			check_eq(std::string("array|map"), e->execute("array<var> xs; map<string, var> m; type_of(xs) + \"|\" + type_of(m);").as_string());
		});

		test("a_type_name_that_arrives_as_text_is_read_without_its_spacing", [this]() {
			auto e = stdlib_engine();
			e->execute("class Duo<A, B> { A a; B b; } class Box<T> { T v; } namespace shop { class Item { int n = 2; } class Shelf<T> { T item; } } class Plain { int n = 1; }"
			           " Duo<int, string> d; d.a = 4; Box<map<string, int>> bm; shop::Shelf<shop::Item> shelf;");
			const auto reaches = [&](const std::string& spelled, const std::string& name) {
				const std::string quoted = "\"" + spelled + "\"";
				check_eq(name, e->execute("type_of(reflect::construct(" + quoted + "));").as_string(), "reflect::construct " + spelled);
				check_eq(name, e->execute("type_of(reflect::construct_shared(" + quoted + "));").as_string(), "reflect::construct_shared " + spelled);
				check_true(e->execute("reflect::fields(" + quoted + ").size() > 0 && reflect::is_instance_of(reflect::construct(" + quoted + "), " + quoted + ");").as_bool(), "reflect:: " + spelled);
				check_eq(name, e->execute("type_of(from_json(\"{\\\"$type\\\":\\\"" + spelled + "\\\",\\\"$val\\\":{}}\"));").as_string(), "from_json " + spelled);
				check_eq(name, detail::registered_type_spelling(e.get(), spelled), "the registered spelling of " + spelled);
			};
			for (const char* spelled : {"Duo<int, string>", "Duo<int,string>", "Duo< int , string >", "  Duo <int ,  string> "}) { reaches(spelled, "Duo<int, string>"); }
			for (const char* spelled : {"Box<map<string,int>>", "Box<map<string, int>>", "Box< map < string , int > >"}) { reaches(spelled, "Box<map<string,int>>"); }
			for (const char* spelled : {"shop::Shelf<shop::Item>", "shop :: Shelf < shop :: Item >"}) { reaches(spelled, "shop::Shelf<shop::Item>"); }
			reaches(" Plain ", "Plain");
			check_eq((int64_t)9, e->execute("from_json(\"{\\\"$type\\\":\\\"Duo< int,string >\\\",\\\"$val\\\":{\\\"a\\\":9,\\\"b\\\":\\\"x\\\"}}\").a;").as_int());
			// case stays significant
			check_true(contains(failure(*e, "reflect::construct(\"duo<int, string>\");"), "reflect::construct: unknown class 'duo<int, string>'"));
			// the binary and the base64 archives: the name in the data, spaced otherwise
			std::string blob = e->execute("to_binary(d);").as_string();
			const size_t at = blob.find("Duo<int, string>");
			check_true(at != std::string::npos);
			blob.replace(at, 16, "Duo< int,string>");
			e->add_function("spaced_blob", [blob]() { return blob; });
			e->add_function("spaced_base64", [blob]() { return jai::detail::base64_encode(blob); });
			check_eq(std::string("Duo<int, string>4"), e->execute("auto back = from_binary(spaced_blob()); type_of(back) + to_string(back.a);").as_string());
			check_eq(std::string("Duo<int, string>4"), e->execute("auto again = from_base64(spaced_base64()); type_of(again) + to_string(again.a);").as_string());
			// an instance that waits and one the program never made are told from their keys too
			check_eq(std::string("reflect::construct: 'Rack<Part>' is not declared yet: it waits for 'Part'|'Rack<Part>' is not declared yet: it waits for 'Part'"), e->execute(R"(
				class Rack<T> { T v; }
				string got = "";
				try { reflect::construct("Rack< Part >"); } catch (e) { got += e; }
				got += "|";
				try { from_json("{\"$type\":\" Rack <Part>\",\"$val\":{}}"); } catch (e) { got += e; }
				class Part { int n = 1; }
				Rack<Part> rack;
				got;
			)").as_string());
			check_true(contains(failure(*e, "from_json(\"{\\\"$type\\\":\\\"Duo< float,float >\\\",\\\"$val\\\":{}}\");"),
				"'Duo< float,float >' names an instance of the generic 'Duo' that the program never made: name the type where it is loaded, as from_json<Duo< float,float >>(text) does"));
		});

		// An instance is ordinary code: flow narrowing, initializer lists, declared compound operators, copy rebinding, the
		// dynamic casts and condition declarations hold inside one, and beside one, as written by hand
		test("an_instance_narrows_through_a_cast_of_its_type_parameter", [this]() {
			auto e = stdlib_engine();
			check_eq(std::string("3|0|8|int|other"), e->execute(R"(
				class Entity { int id = 0; }
				class Goblin : Entity { int gold = 3; }
				int gold_of<T : Entity>(Entity e) { if (dynamic_cast<shared_ptr<T>>(e)) { return e.gold; } return 0; }
				class Purse<T : Entity> {
					int take(Entity e) { if (reflect::is_instance_of(e, "Goblin")) { return e.gold + 5; } return -1; }
				}
				string kind<T>(var v) { if (type_of(v) == "int") { int n = v; return "int"; } return "other"; }
				Goblin g; Entity plain; Purse<Goblin> purse;
				`${gold_of<Goblin>(g)}|${gold_of<Goblin>(plain)}|${purse.take(g)}|${kind<bool>(4)}|${kind<bool>("s")}`;
			)").as_string());
			// without the test the member is the base's, in the instance as in a function written by hand
			const std::string unnarrowed = failure(*e, "class Entity { int id = 0; } class Goblin : Entity { int gold = 3; }"
			                                           " int gold_of<T : Entity>(Entity e) { return e.gold; } Goblin g; gold_of<Goblin>(g);");
			check_true(contains(unnarrowed, "no member 'gold' in class 'Entity'") && contains(unnarrowed, "in gold_of<Goblin>, instantiated at"), unnarrowed);
		});

		test("the_dynamic_casts_take_a_type_parameter_and_an_instance", [this]() {
			auto e = stdlib_engine();
			check_eq(std::string("true|false|4|9|true|false|true|false"), e->execute(R"(
				class Shape { int sides = 0; }
				class Square : Shape { Square() { sides = 4; } }
				class Probe<T> {
					bool holds(shared_ptr<Shape> s) { return dynamic_cast<T>(s) != null; }
					int sides_of(Shape& s) { return dynamic_cast<T&>(s).sides; }
				}
				bool is<T>(shared_ptr<Shape> s) { return dynamic_cast<T>(s) != null; }
				class Box<T> { T v; }
				class Wide<T> : Box<T> { }
				shared_ptr<Shape> sq = new Square(); shared_ptr<Shape> plain = new Shape();
				Probe<Square> probe; Square real;
				shared_ptr<Box<int>> held = new Wide<int>(); held.v = 9;
				`${probe.holds(sq)}|${probe.holds(plain)}|${probe.sides_of(real)}|${dynamic_cast<shared_ptr<Wide<int>>>(held).v}|${is<Square>(sq)}|${is<Square>(plain)}|${is<shared_ptr<Square>>(sq)}|${is<shared_ptr<Square>>(plain)}`;
			)").as_string());
			// a handle as the type argument is that handle (above); inside a spelled shared_ptr<T> it is a handle to a handle, and that instance alone fails
			const std::string handle = failure(*e, "class Shape { } bool is<T>(shared_ptr<Shape> s) { return dynamic_cast<shared_ptr<T>>(s) != null; } shared_ptr<Shape> s = new Shape(); is<Shape>(s); is<shared_ptr<Shape>>(s);");
			check_true(contains(handle, "dynamic_cast<shared_ptr<shared_ptr<Shape>>> names a handle to a handle: dynamic_cast takes a class or one shared_ptr to it, dynamic_cast<Shape>(e)") &&
			           contains(handle, "in is<shared_ptr<Shape>>, instantiated at") && !contains(handle, "in is<Shape>"), handle);
			const std::string plain = failure(*e, "class Shape { } bool is<T>(shared_ptr<Shape> s) { return dynamic_cast<T>(s) != null; } shared_ptr<Shape> s = new Shape(); is<Shape>(s); is<int>(s);");
			check_true(contains(plain, "dynamic_cast takes a class: dynamic_cast<T>(e) is e as a shared_ptr<T>, null when it is no T, and dynamic_cast<T&>(e) is the reference") &&
			           contains(plain, "in is<int>, instantiated at") && !contains(plain, "in is<Shape>"), plain);
			// a bound's class in a condition declaration, an instance's name and a namespace's as the cast's class
			check_eq(std::string("3|0|5|true|true|true"), e->execute(R"(
				class Animal { int legs = 4; }
				class Dog : Animal { int tricks = 3; }
				int tricks_of<T : Animal>(shared_ptr<Animal> a) { if (auto found = dynamic_cast<T>(a)) { return found.tricks; } return 0; }
				class Stack<T> { array<T> items; }
				namespace shop { class Item { } class Stack<T> { int n = 5; } }
				shared_ptr<Animal> dog = new Dog(); shared_ptr<Animal> other = new Animal();
				var stack = Stack<int>(); shop::Stack<shop::Item> crate;
				`${tricks_of<Dog>(dog)}|${tricks_of<Dog>(other)}|${dynamic_cast<shop::Stack<shop::Item>>(crate).n}|${dynamic_cast<Stack<int>>(stack) != null}|` +
				`${dynamic_cast<shared_ptr<Stack<int>>>(stack) != null}|${dynamic_cast<Stack<int>&>(stack).items.empty()}`;
			)").as_string());
			const std::string builtin = failure(*e, "class Shape { } class Probe<T> { int f(Shape& s) { dynamic_cast<T&>(s); return 1; } } Probe<int> p;");
			check_true(contains(builtin, "dynamic_cast takes a class: dynamic_cast<T>(e) is e as a shared_ptr<T>, null when it is no T, and dynamic_cast<T&>(e) is the reference") &&
			           contains(builtin, "in Probe<int>, instantiated at"), builtin);
		});

		test("a_condition_declares_an_instance_and_a_comparison_stays_one", [this]() {
			auto e = stdlib_engine();
			check_eq(std::string("7|7|8|less|3"), e->execute(R"(
				class Box<T> { T v; Box() { } Box(T x) { v = x; } }
				shared_ptr<Box<int>> make(int n) { return new Box<int>(n); }
				string got = "";
				if (shared_ptr<Box<int>> b = make(7)) { got += to_string(b.v); }
				if (auto b = dynamic_cast<shared_ptr<Box<int>>>(make(7))) { got += "|" + to_string(b.v); }
				if (Box<int> c = Box<int>(8); c.v > 3) { got += "|" + to_string(c.v); }
				int a = 1; int b = 4;
				if (a < b) { got += "|less"; }
				int turns = 0;
				while (a < b) { a += 1; turns += 1; }
				got + "|" + to_string(turns);
			)").as_string());
		});

		test("an_instance_takes_an_initializer_list_and_a_declared_compound_operator", [this]() {
			auto e = stdlib_engine();
			check_eq(std::string("10|2|ab|1|11|1"), e->execute(R"(
				class Base<T> { T seed; Base(T s) : seed(s) { } }
				class Acc<T> : Base<T> {
					T total; int adds;
					Acc(T start) : Base<T>(start), total(start), adds(0) { }
					Acc& operator+=(T x) { total += x; adds += 1; return this; }
					Acc& operator++() { adds += 10; return this; }
				}
				Acc<int> a(5); a += 2; a += 3;
				Acc<string> s("a"); s += "b";
				Acc<int> n(0); ++n; n += 1;
				`${a.total}|${a.adds}|${s.total}|${s.adds}|${n.adds}|${n.total}`;
			)").as_string());
		});

		test("a_copy_of_an_instance_rebinds_the_lambdas_bound_to_it", [this]() {
			auto e = stdlib_engine();
			check_eq(std::string("1|6|x|xyz"), e->execute(R"(
				class Counter<T> {
					T n; var bump;
					void arm() { bump = [this](T by) { n += by; return n; }; }
				}
				Counter<int> c; c.arm(); c.bump(1);
				Counter<int> d = c; d.bump(5);
				Counter<string> s; s.arm(); s.bump("x");
				array<Counter<string>> all; all.push(s); all[0].bump("yz");
				`${c.n}|${d.n}|${s.n}|${all[0].n}`;
			)").as_string());
		});

		test("a_signature_is_a_type_inside_a_generic_and_no_type_argument_yet", [this]() {
			auto e = stdlib_engine();
			// function<R(A...)> in a generic's parameters and fields is ordinary code once the instance is made
			check_eq(std::string("4|8|3|7"), e->execute(R"(
				T apply<T>(function<T(T)> f, T x) { return f(x); }
				class Slot<T> { function<T(T)> f; T run(T x) { return f(x); } }
				int total = 0;
				void each<T>(array<T> xs, function<void(T)> f) { for (auto x : xs) { f(x); } }
				class Box<T> { T v; }
				Slot<int> s; s.f = (int a) => a * 2;
				each([1, 2], (int x) => { total += x; });
				Box<function> any; any.v = () => 7;
				`${apply<int>((int a) => a + 1, 3)}|${s.run(4)}|${total}|${any.v()}`;
			)").as_string());
			// as a type argument a signature and a coroutine type are refused by name, in a type and in a call
			const std::string box = "class Box<T> { T v; } T pick<T>(T a) { return a; } ";
			check_true(contains(failure(*e, box + "Box<function<int(int)>> b;"), "'function<int(int)>' is not a type an instance is made with: 'Box<function<int(int)>>'"));
			check_true(contains(failure(*e, box + "array<Box<function<void()>>> bs;"), "'function<void()>' is not a type an instance is made with: 'Box<function<void()>>'"));
			check_true(contains(failure(*e, box + "Box<coroutine<int>> c;"), "'coroutine<int>' is not a type an instance is made with: 'Box<coroutine<int>>'"));
			check_true(contains(failure(*e, box + "function<int(int)> inc = (int a) => a + 1; auto g = pick<function<int(int)>>(inc);"),
				"'function<int(int)>' is not a type an instance is made with: 'pick<function<int(int)>>'"));
			// a signature deduces nothing: the call writes the type argument
			check_true(contains(failure(*e, "T run<T>(function<T()> f) { return f(); } function<int()> one = () => 1; run(one);"),
				"cannot deduce T for 'run': argument 1 is a function, which says nothing of a type; write run<T>(...)"));
			check_eq((int64_t)1, e->execute("T run<T>(function<T()> f) { return f(); } function<int()> one = () => 1; run<int>(one);").as_int());
			// a typed callable's own '<' is no type-argument list of a call, and a comparison beside one stays a comparison
			check_eq(std::string("3|true|false"), e->execute("function<int(int)> f = (int a) => a + 1; int g = 1; int h = 2; `${f(2)}|${g < h}|${f(1) < h}`;").as_string());
		});

		// A generic class is a class of its namespace to another file's block of that namespace, as a plain class is: its bare
		// name resolves there whichever file is imported first (engine::namespace_class_known answers for both)
		test("a_namespaces_generic_class_is_named_bare_from_another_files_block", [this]() {
			const std::filesystem::path directory = unit_directory("namespace_files");
			write_unit(directory, "declaring.jai", R"(
				namespace shop {
					class Item { int price = 3; }
					class Plain { int n = 1; }
					class Shelf<T> { T held; int n = 1; }
					namespace inner { class Deep<T> { T d; } class Flat { int f = 2; } }
				}
			)");
			const std::string naming = R"(
				namespace shop {
					int generic() { Shelf<Item> s; return s.held.price + s.n; }
					int plain() { Plain p; return p.n; }
					int ints() { Shelf<int> s; s.held = 5; return s.held; }
					int held() { Shelf<Plain> s; return s.held.n + s.n; }
					namespace inner { int deep() { Deep<Item> d; Flat f; Shelf<int> s; return d.d.price + f.f + s.n; } }
				}
				string report() { shop::Shelf<shop::Item> last; return `${shop::generic()} ${shop::plain()} ${shop::ints()} ${shop::held()} ${shop::inner::deep()} ${type_of(last)}`; }
			)";
			write_unit(directory, "naming.jai", naming);
			write_unit(directory, "naming_imports.jai", "import \"declaring.jai\";\n" + naming);
			write_unit(directory, "naming_using.jai", "using shop::Shelf, shop::Item, shop::Plain;\nint outside() { Shelf<Item> s; Plain p; return s.held.price + p.n; }");
			const std::string expected = "4 1 5 2 6 shop::Shelf<shop::Item>";
			const std::pair<std::string, std::string> programs[] = {
				{"include \"declaring.jai\"; include \"naming.jai\"; report();", expected},   // one unit: what every program below must match
				{"import \"declaring.jai\"; import \"naming.jai\"; report();", expected},
				{"import \"naming.jai\"; import \"declaring.jai\"; report();", expected},
				{"import \"naming_imports.jai\"; report();", expected},
				{"import \"declaring.jai\"; import \"naming_using.jai\"; to_string(outside());", "4"},
				{"import \"naming_using.jai\"; import \"declaring.jai\"; to_string(outside());", "4"},
			};
			for (const auto& [program, wanted] : programs) {
				auto e = stdlib_engine();
				e->jaibite_cache(false);
				e->add_include_path(directory.string());
				std::string got;
				try { got = e->execute(program).to_string(); } catch (const std::exception& error) { got = error.what(); }
				check_eq(wanted, got.substr(0, got.find('\n')), program);
			}
		});

		test("a_parallel_body_uses_an_instance", [this]() {
			auto e = stdlib_engine();
			check_eq(std::string("[2, 4, 6, 8]"), e->execute(R"(
				class Acc<T> { T total; void add(T x) { total += x; } }
				T doubled<T>(T x) { Acc<T> a; a.add(x); a.add(x); return a.total; }
				array<int> xs = [1, 2, 3, 4];
				parallel_for (auto& x : xs) { x = doubled<int>(x); }
				to_string(xs);
			)").as_string());
		});

		test("a_breakpoint_in_a_generic_stops_in_its_instance", [this]() {
			for (const backend_type type : {backend_type::interpreter, backend_type::vm}) {
				auto e = make_default_engine();
				e->set_backend(type);
				auto& dbg = e->debugger();
				std::mutex m;
				std::condition_variable cv;
				bool stopped = false;
				debug::stop_info last;
				dbg.set_on_stopped([&](const debug::stop_info& si) {
					std::lock_guard<std::mutex> lock(m);
					last = si;
					stopped = true;
					cv.notify_all();
				});
				dbg.set_breakpoints("<script>", {3});
				dbg.set_enabled(true);
				const std::string source =
					"class Box<T> {\n"
					"\tT item;\n"
					"\tT twice() { return item + item; }\n"   // line 3: the generic's own line
					"}\n"
					"Box<int> b;\n"
					"b.item = 4;\n"
					"int result = b.twice();\n";
				std::thread script([&] { e->execute(source); });
				{
					std::unique_lock<std::mutex> lock(m);
					check_true(cv.wait_for(lock, std::chrono::minutes(10), [&] { return stopped; }));
				}
				check_eq(std::string("breakpoint"), last.reason);
				check_eq(3, last.line);
				dbg.resume();
				script.join();
				check_eq((int64_t)8, e->get_variable("result").as_int());
			}
		});

		test("less_than_keeps_comparing_where_no_type_argument_list_closes", [this]() {
			auto e = make_engine();
			check_true(e->execute("int a = 1; int b = 2; a < b;").as_bool());
			check_true(e->execute("int a = 1; int b = 2; int c = 0; a < b && b > (c);").as_bool());
			check_true(e->execute("int a = 1; int b = 2; int c = 3; int d = 4; bool both(bool x, bool y) { return x && y; } both(a < b, c < d);").as_bool());
			check_true(e->execute("int a = 1; int b = 2; int c = 3; int d = 1; bool both(bool x, bool y) { return x && y; } both(a < b, c > d);").as_bool());
			check_eq((int64_t)3, e->execute("int n = 3; int i = 0; for (i = 0; i < n; i += 1) {} i;").as_int());
		});

		test("the_typed_result_options_are_spelled_returns_of_a_shape", [this]() {
			auto e = make_engine();
			stdlib::register_all(*e);
			engine* raw = e.get();
			e->add_function("echo_as", [raw](const script_type& t, script_int n) {
				(void)t;
				return script_value(n, raw);
			}, returns<T>);
			check_eq(static_cast<uint8_t>(1), e->type_argument_result("echo_as"));
			check_eq(static_cast<uint8_t>(1), returns<T>.shape);
			check_eq(static_cast<uint8_t>(2), returns<array_of<T>>.shape);
			check_eq(static_cast<uint8_t>(3), returns<shared_ptr_of<T>>.shape);
			check_eq((int64_t)5, e->execute("int n = echo_as<int>(5); n;").as_int());
		});
	}
};

}

using generics_tests = jai::foundry::tests::generics_tests;
FOUNDRY_REGISTER(generics_tests)
