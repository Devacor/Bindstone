#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/detail/ast_serializer.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <cmath>
#include <limits>

namespace jai::foundry::tests {

namespace prelaunch_detail {

struct first_base { int first{11}; };
struct second_base { int second{22}; };
struct native_child : first_base, second_base {};
struct native_grandchild : native_child {};
struct virtual_child : first_base, virtual second_base {};
struct abstract_base { virtual ~abstract_base() = default; virtual int read() const = 0; };
struct abstract_child : first_base, abstract_base { int read() const override { return 29; } };
struct unrelated { int other{33}; };

inline void bind_native(engine& eng) {
	dynamic_binder<first_base>(eng, "FirstBase").constructor<>().property("first", &first_base::first).build();
	dynamic_binder<second_base>(eng, "SecondBase").constructor<>().property("second", &second_base::second).build();
	dynamic_binder<native_child>(eng, "NativeChild").constructor<>().base_class<first_base>().base_class<second_base>().build();
	dynamic_binder<unrelated>(eng, "Unrelated").constructor<>().build();
}

}

class prelaunch_regression_tests : public suite {
public:
	prelaunch_regression_tests() : suite("Prelaunch Regressions") {}

	void forge_tests() override {
		test("numeric_int64_selection_is_exact", [this]() {
			auto eng = make_engine();
			constexpr script_int low = 9007199254740992LL;
			check_eq(low, eng->execute("math::min(9007199254740993, 9007199254740992);").as_int());
			check_eq(low + 1, eng->execute("math::max(9007199254740992, 9007199254740993);").as_int());
			check_eq(low, eng->execute("math::clamp(9007199254740993, 0, 9007199254740992);").as_int());
			check_eq(-low, eng->execute("math::clamp(-9007199254740993, -9007199254740992, 0);").as_int());
			eng->add_global("lowest", script_value((std::numeric_limits<script_int>::min)(), eng.get()));
			eng->add_global("highest", script_value((std::numeric_limits<script_int>::max)(), eng.get()));
			check_eq((std::numeric_limits<script_int>::max)() - 1, eng->execute("math::min(highest, highest - 1);").as_int());
			check_eq((std::numeric_limits<script_int>::min)() + 1, eng->execute("math::max(lowest, lowest + 1);").as_int());
			script_int bound = low + 1;
			eng->add_global_ref("bound", bound);
			check_eq(low, eng->execute("math::min(bound, 9007199254740992);").as_int());
		});

		test("numeric_mixed_and_float_selection_unchanged", [this]() {
			auto eng = make_engine();
			// Mixed pairs still promote to double, and ties still choose the first operand.
			check_eq(script_int{9007199254740993LL}, eng->execute("math::min(9007199254740993, 9007199254740992.0);").as_int());
			check(eng->execute("math::min(9007199254740992.0, 9007199254740993);").is_float());
			eng->add_global("negative_zero", script_value(-0.0, eng.get()));
			eng->add_global("not_a_number", script_value(std::numeric_limits<double>::quiet_NaN(), eng.get()));
			check(std::signbit(eng->execute("math::min(negative_zero, 0.0);").as_float()));
			check(std::signbit(eng->execute("math::max(negative_zero, 0.0);").as_float()));
			check_eq(script_int{2}, eng->execute("math::min(not_a_number, 2);").as_int());
			check(std::isnan(eng->execute("math::max(2, not_a_number);").as_float()));
			check(std::isnan(eng->execute("math::clamp(not_a_number, 0, 1);").as_float()));
		});

		test("numeric_integer_conversion_valid_boundaries", [this]() {
			auto eng = make_engine();
			const double upper = std::nextafter(0x1p63, 0.0);
			eng->add_global("upper", script_value(upper, eng.get()));
			eng->add_global("lower", script_value(-0x1p63, eng.get()));
			for (const std::string fn : {"math::itrunc", "math::ifloor", "cast<int>"}) {
				check_eq(static_cast<script_int>(upper), eng->execute(fn + "(upper);").as_int());
				check_eq((std::numeric_limits<script_int>::min)(), eng->execute(fn + "(lower);").as_int());
				check_eq(script_int{0}, eng->execute(fn + "(0.0);").as_int());
			}
			check_eq(script_int{-3}, eng->execute("math::itrunc(-3.9);").as_int());
			check_eq(script_int{-4}, eng->execute("math::ifloor(-3.9);").as_int());
			check_eq(script_int{9223372036854775807LL}, eng->execute("math::ifloor(9223372036854775807);").as_int());
			check_eq(script_int{9007199254740993LL}, eng->execute("math::itrunc(9007199254740993);").as_int());
		});

		test("numeric_float_to_int_errors_are_catchable", [this]() {
			const std::string expected = "Cannot convert float to integer: non-finite or out of range";
			for (double input : {0x1p63, std::nextafter(-0x1p63, -INFINITY), 1e100, -1e100,
				std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
				std::numeric_limits<double>::quiet_NaN()}) {
				for (const std::string action : {
					"math::itrunc(source);", "math::ifloor(source);", "cast<int>(source);",
					"int target = source;", "int target = 7; target = source;",
					"int accept(int x) { return x; } accept(source);",
					"int convert(var x) { return x; } convert(source);",
					"class Box { int value = 7; } auto box = Box(); box.value = source;",
					"class Number { var value = 0; float to_int() { return value; } } auto n = Number(); n.value = source; int target = n;"}) {
					auto eng = make_engine();
					eng->add_global("input", script_value(input, eng.get()));
					auto result = eng->execute("var source = input; string message = \"\"; try { " + action +
						" } catch (e) { message = e; } message;");
					check_eq(expected, result.as_string());
					check_eq(script_int{42}, eng->execute("40 + 2;").as_int());
				}
			}
		});

		test("numeric_failed_store_preserves_value", [this]() {
			auto eng = make_engine();
			check_eq(script_int{14}, eng->execute(R"(
				var source = 1e100;
				int target = 7;
				class Box { int value = 7; }
				auto box = Box();
				try { target = source; } catch (e) {}
				try { box.value = source; } catch (e) {}
				target + box.value;
			)").as_int());
		});

		test("numeric_random_seed_conversion_is_checked", [this]() {
			auto eng = make_engine();
			for (double input : {0x1p64, -1.0, 1e100, std::numeric_limits<double>::infinity(),
				std::numeric_limits<double>::quiet_NaN()}) {
				eng->add_global("seed_input", script_value(input, eng.get()));
				check_eq(std::string("Cannot convert float to integer: non-finite or out of range"), eng->execute(R"(
					string message = "";
					try { math::random_seed(seed_input); } catch (e) { message = e; }
					message;
				)").as_string());
			}
			for (double input : {-0.5, 0.0, 42.75, 0x1p63, std::nextafter(0x1p64, 0.0)}) {
				eng->add_global("seed_input", script_value(input, eng.get()));
				eng->add_global("integer_seed", script_value(static_cast<script_int>(static_cast<uint64_t>(input)), eng.get()));
				check(eng->execute(R"(
					math::random_seed(integer_seed); auto expected = math::random();
					math::random_seed(seed_input); math::random() == expected;
				)").as_bool());
			}
		});

		test("saved_destructuring_rejects_missing_slots", [this]() {
			auto eng = make_engine();
			const source_location loc{};
			auto literal = std::make_shared<literal_expr>(loc, script_value(script_int{1}, eng.get()));
			auto array = std::make_shared<array_literal_expr>(loc, std::vector<expression_ptr>{literal});
			auto decl = std::make_shared<destructuring_decl>(loc, array);
			decl->names.emplace_back("item", eng->symbolize("item"));
			auto bytes = detail::serialize_jaibite({decl}, *eng->get_symbolizer(), 0);
			check_throws([&]() { eng->jaibite_load_bytes(bytes); });
			check_eq(script_int{42}, eng->execute("40 + 2;").as_int());
		});

		for (const std::string format : {"json", "binary", "base64"}) {
			test("nested_class_roundtrip_" + format, [this, format]() {
				auto eng = make_engine();
				stdlib::register_all(*eng);
				eng->execute(R"(
					class SavedPoint { int x = 17; }
					class SavedBox { var child = null; }
					auto box = SavedBox();
					box.child = SavedPoint();
					array<var> payload = [box, {"point": SavedPoint()}];
				)");
				eng->execute("var restored = from_" + format + "(to_" + format + "(payload));");
				check_eq(std::string("SavedBox"), eng->execute("type_of(restored[0]);").as_string());
				check_eq(std::string("SavedPoint"), eng->execute("type_of(restored[0].child);").as_string());
				check_eq(script_int{17}, eng->execute("restored[0].child.x;").as_int());
				check_eq(std::string("SavedPoint"), eng->execute("type_of(restored[1][\"point\"]);").as_string());
				check_eq(script_int{17}, eng->execute("restored[1][\"point\"].x;").as_int());
			});
		}

		test("native_second_base_shared_pointer", [this]() {
			auto eng = make_engine();
			prelaunch_detail::bind_native(*eng);
			auto child = std::make_shared<prelaunch_detail::native_child>();
			auto value = eng->make_object(child);
			auto base = value.as<std::shared_ptr<prelaunch_detail::second_base>>();
			check(base.get() == static_cast<prelaunch_detail::second_base*>(child.get()));
			check(!base.owner_before(child) && !child.owner_before(base));
		});

		test("json_maps_retain_string_key_type", [this]() {
			auto eng = make_engine();
			stdlib::register_all(*eng);
			auto result = eng->execute(R"(from_json("{\"nested\": {\"answer\": 42}}");)");
			check(result.get_type_info()->key_type() == type_info_ptr(eng->get_type_info_string()));
			const auto& nested = result.as_map().at(script_value("nested", eng.get()));
			check(nested.get_type_info()->key_type() == type_info_ptr(eng->get_type_info_string()));
			eng->add_global("decoded", result);
			check_throws([&]() { eng->execute("decoded[1] = 5;"); });
			check_throws([&]() { eng->execute("decoded[\"nested\"][1] = 5;"); });
		});

		test("native_checked_errors_do_not_throw", [this]() {
			auto eng = make_engine();
			prelaunch_detail::bind_native(*eng);
			auto wrong = eng->make_object(std::make_shared<prelaunch_detail::unrelated>());
			auto result = detail::checked_native_reference(wrong, typeid(prelaunch_detail::second_base));
			check_false(result.has_value());
			auto holder = wrong.get_object_holder();
			auto shared = detail::checked_native_object(eng.get(), *holder, typeid(prelaunch_detail::second_base));
			check_false(shared.has_value());
			check_eq(std::string("Object type mismatch"), std::string(shared.message()));
		});

		test("retired_native_layer_diagnostic_matches_all_adapters", [this]() {
			using namespace prelaunch_detail;
			auto eng = make_engine();
			bind_native(*eng);
			auto value = eng->make_object(std::make_shared<native_child>());
			value.as<std::shared_ptr<class_instance>>()->retire_script_layer();
			const std::string expected = "Script layer was detached (script_layer_detach, or a move to another engine): this handle no longer reaches its object";
			auto reference = detail::checked_native_reference(value, typeid(second_base));
			check_false(reference.has_value());
			check_eq(expected, std::string(reference.message()));
			auto shared = detail::checked_native_object(eng.get(), *value.get_object_holder(), typeid(second_base));
			check_false(shared.has_value());
			check_eq(expected, std::string(shared.message()));
			auto message = [](auto&& action) -> std::string {
				try { action(); } catch (const runtime_error& error) { return error.what(); }
				return "no error";
			};
			check_eq(expected, message([&]() { value.as<second_base&>(); }));
			check_eq(expected, message([&]() { value.as<const second_base&>(); }));
			check_eq(expected, message([&]() { value.as<std::shared_ptr<second_base>>(); }));
		});

		test("native_borrowed_argument_can_copy_without_ownership", [this]() {
			using namespace prelaunch_detail;
			auto eng = make_engine();
			bind_native(*eng);
			native_child child;
			eng->add_global("borrowed", script_value::make_cpp_reference("NativeChild", eng->symbolize("NativeChild"), &child, {}, eng.get()));
			eng->add_function("copy_second", [](second_base copy) { copy.second += 3; return copy.second; });
			eng->add_function("read_second", [](const second_base& base) { return base.second; });
			check_eq(script_int{25}, eng->execute("copy_second(borrowed);").as_int());
			check_eq(script_int{22}, eng->execute("read_second(borrowed);").as_int());
			check_eq(11, child.first);
		});

		test("native_second_base_typed_array", [this]() {
			auto eng = make_engine();
			prelaunch_detail::bind_native(*eng);
			auto child = std::make_shared<prelaunch_detail::native_child>();
			eng->add_global("native_child", child);
			auto value = eng->execute("array<NativeChild> children = [native_child]; children;");
			auto bases = value.as<std::vector<std::shared_ptr<prelaunch_detail::second_base>>>();
			check_eq(size_t{1}, bases.size());
			check(bases[0].get() == static_cast<prelaunch_detail::second_base*>(child.get()));
		});

		test("native_unrelated_pointer_rejected", [this]() {
			auto eng = make_engine();
			prelaunch_detail::bind_native(*eng);
			auto value = eng->make_object(std::make_shared<prelaunch_detail::unrelated>());
			check_throws([&]() { value.as<std::shared_ptr<prelaunch_detail::second_base>>(); });
		});

		test("native_inherited_property_uses_base_subobject", [this]() {
			auto eng = make_engine();
			prelaunch_detail::bind_native(*eng);
			check_eq(script_int{22}, eng->execute("auto child = NativeChild(); child.second;").as_int());
			check_eq(script_int{11}, eng->execute("child.second = 44; child.first;").as_int());
			check_eq(script_int{44}, eng->execute("child.second;").as_int());
		});

		test("native_base_reference_arguments", [this]() {
			auto eng = make_engine();
			prelaunch_detail::bind_native(*eng);
			auto child = std::make_shared<prelaunch_detail::native_child>();
			auto value = eng->make_object(child);
			check(&value.as<prelaunch_detail::second_base&>() == static_cast<prelaunch_detail::second_base*>(child.get()));
			check(&value.as<const prelaunch_detail::second_base&>() == static_cast<prelaunch_detail::second_base*>(child.get()));
			eng->add_global("child", child);
			eng->add_function("set_second", [](prelaunch_detail::second_base& base) { base.second = 47; });
			eng->add_function("read_second", [](const prelaunch_detail::second_base& base) { return base.second; });
			check_eq(script_int{47}, eng->execute("set_second(child); read_second(child);").as_int());
			check_eq(11, child->first);
		});

		test("native_virtual_and_transitive_base_casts", [this]() {
			using namespace prelaunch_detail;
			auto eng = make_engine();
			bind_native(*eng);
			dynamic_binder<native_grandchild>(*eng, "Grandchild").constructor<>().base_class<native_child>().build();
			dynamic_binder<virtual_child>(*eng, "VirtualChild").constructor<>().base_class<first_base>().base_class<second_base>().build();
			auto grandchild = std::make_shared<native_grandchild>();
			auto virtual_object = std::make_shared<virtual_child>();
			check(eng->make_object(grandchild).as<std::shared_ptr<second_base>>().get() == static_cast<second_base*>(grandchild.get()));
			check(eng->make_object(virtual_object).as<std::shared_ptr<second_base>>().get() == static_cast<second_base*>(virtual_object.get()));
			eng->add_global("grandchild", grandchild);
			eng->add_global("virtual_object", virtual_object);
			auto result = eng->execute("array<SecondBase> mixed = [grandchild, virtual_object]; mixed;")
				.as<std::vector<std::shared_ptr<second_base>>>();
			check_eq(size_t{2}, result.size());
			check_eq(22, result[0]->second);
			check_eq(22, result[1]->second);
		});

		test("native_abstract_base_and_unrelated_rejection", [this]() {
			using namespace prelaunch_detail;
			auto eng = make_engine();
			bind_native(*eng);
			dynamic_binder<abstract_base>(*eng, "AbstractBase").method("read", &abstract_base::read).build();
			dynamic_binder<abstract_child>(*eng, "AbstractChild").constructor<>().base_class<first_base>().base_class<abstract_base>().build();
			auto child = std::make_shared<abstract_child>();
			check(eng->make_object(child).as<std::shared_ptr<abstract_base>>().get() == static_cast<abstract_base*>(child.get()));
			auto wrong = eng->make_object(std::make_shared<unrelated>());
			check_throws([&]() { wrong.as<std::shared_ptr<abstract_base>>(); });
		});

		test("native_nonowning_base_reference", [this]() {
			using namespace prelaunch_detail;
			auto eng = make_engine();
			bind_native(*eng);
			auto child = std::make_shared<native_child>();
			auto value = script_value::make_cpp_reference("NativeChild", eng->symbolize("NativeChild"), child.get(), child, eng.get());
			check(&value.as<second_base&>() == static_cast<second_base*>(child.get()));
			auto shared = value.as<std::shared_ptr<second_base>>();
			check(shared.get() == static_cast<second_base*>(child.get()));
			check(!shared.owner_before(child) && !child.owner_before(shared));
			shared.reset();
			child.reset();
			check_throws([&]() { value.as<std::shared_ptr<second_base>>(); });
		});
	}
};

}

using prelaunch_regression_tests = jai::foundry::tests::prelaunch_regression_tests;
FOUNDRY_REGISTER(prelaunch_regression_tests)
