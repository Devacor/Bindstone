// reflect:: — the script-side reflection capability (docs/reflection.md front door,
// docs/reflection_design.md companion). These pins hold the three contracts: reserved
// namespace (engine truth is not impersonatable; math:: stays open by contract),
// live-query recapture (hot reload -> re-ask sees the new shape), and allowlist scoping
// (an engine reflects only what IT registered).

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/properties/macros.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class reflect_host_thing : public property_owner<reflect_host_thing> {
public:
    JAI_PROPERTY((int), armor, 4);
    reflect_host_thing() = default;
};

class reflection_tests : public suite {
    static std::shared_ptr<engine> make_reflective_engine() {
        auto eng = engine::make();
        stdlib::register_all(*eng);
        return eng;
    }

public:
    reflection_tests() : suite("Reflection") {}

    void forge_tests() override {
        test("namespace_call_resolves", [this]() {
            // The registration probe: reflect:: members are namespace VARIABLES holding
            // function values, called through ordinary namespace-member syntax.
            auto eng = make_reflective_engine();
            check_eq(eng->execute("reflect::type_name(42)").as<std::string>(), "int");
        });

        test("reserved_namespace_rejects_script_declarations", [this]() {
            auto eng = make_reflective_engine();
            check_throws([&]() { eng->execute("namespace reflect { function evil() { return 1; } }"); },
                "namespace reflect is engine-owned");
            check_throws([&]() { eng->execute("namespace reflect::sub { function evil() { return 1; } }"); },
                "flat sub-names are reserved with the prefix");
        });

        test("math_namespace_stays_open_by_contract", [this]() {
            // The deliberate contrast: script-contributed math is legitimate math.
            auto eng = make_reflective_engine();
            eng->execute("namespace math { function double_it(auto x) -> auto { return x * 2; } }");
            check_eq(eng->execute("math::double_it(21)").as_int(), (int64_t)21 * 2);
        });

        test("type_name_is_semantic", [this]() {
            auto eng = make_reflective_engine();
            check_eq(eng->execute("reflect::type_name(1.5)").as<std::string>(), "float");
            check_eq(eng->execute("reflect::type_name(\"hi\")").as<std::string>(), "string");
            check_eq(eng->execute("reflect::type_name(null)").as<std::string>(), "null");
            eng->execute("class Creature { int hp = 10; }");
            check_eq(eng->execute("reflect::type_name(Creature())").as<std::string>(), "Creature");
            // References answer what they hold, never the wrapper
            check_eq(eng->execute(
                "var c = Creature(); var& r = c; reflect::type_name(r)").as<std::string>(), "Creature");
        });

        test("has_field_and_has_method_walk_the_chain", [this]() {
            auto eng = make_reflective_engine();
            eng->execute(R"(
                class Base { int hp = 5; void heal() { hp = hp + 1; } }
                class Derived : Base { int mana = 2; }
            )");
            check_eq(eng->execute("reflect::has_field(Derived(), \"hp\")").as<bool>(), true);
            check_eq(eng->execute("reflect::has_field(Derived(), \"mana\")").as<bool>(), true);
            check_eq(eng->execute("reflect::has_field(Derived(), \"nope\")").as<bool>(), false);
            check_eq(eng->execute("reflect::has_method(\"Derived\", \"heal\")").as<bool>(), true);
            check_eq(eng->execute("reflect::has_method(\"Derived\", \"nope\")").as<bool>(), false);
        });

        test("classes_bases_and_unknown_class_error", [this]() {
            auto eng = make_reflective_engine();
            eng->execute("class Alpha {} class Beta : Alpha {}");
            check_eq(eng->execute(
                "var found = 0; for (auto c : reflect::classes()) { if (c == \"Alpha\" || c == \"Beta\") { found = found + 1; } } found"
            ).as_int(), (int64_t)2);
            check_eq(eng->execute("reflect::bases(Beta())[0]").as<std::string>(), "Alpha");
            check_eq(eng->execute("reflect::bases(\"Alpha\").size()").as_int(), (int64_t)0);
            check_throws([&]() { eng->execute("reflect::bases(\"Gremlin\");"); },
                "unknown class name raises the family error");
        });

        test("is_cpp_bound_distinguishes_host_from_script", [this]() {
            auto eng = make_reflective_engine();
            dynamic_binder<reflect_host_thing>(*eng, "HostThing")
                .constructor<>()
                .auto_bind()
                .build();
            eng->execute("class ScriptThing { int x = 1; }");
            check_eq(eng->execute("reflect::is_cpp_bound(\"HostThing\")").as<bool>(), true);
            check_eq(eng->execute("reflect::is_cpp_bound(\"ScriptThing\")").as<bool>(), false);
            check_eq(eng->execute("reflect::is_cpp_bound(ScriptThing())").as<bool>(), false);
            check_eq(eng->execute("reflect::is_cpp_bound(42)").as<bool>(), false);
        });

        test("generation_bumps_on_hot_reload_and_recapture_sees_new_shape", [this]() {
            // The live-query contract: reload -> the SAME queries answer the new shape,
            // and generation() is the cheap should-I-re-ask poll.
            auto eng = make_reflective_engine();
            eng->execute("class Evolver { int a = 1; }");
            auto gen0 = eng->execute("reflect::generation(\"Evolver\")").as_int();
            check_eq(eng->execute("reflect::has_field(\"Evolver\", \"b\")").as<bool>(), false);
            eng->execute("class Evolver { int a = 1; int b = 2; }");   // hot reload: new field
            auto gen1 = eng->execute("reflect::generation(\"Evolver\")").as_int();
            check(gen1 > gen0, "generation moved on redefinition");
            check_eq(eng->execute("reflect::has_field(\"Evolver\", \"b\")").as<bool>(), true);
        });

        test("fields_declaration_order_base_first", [this]() {
            // The determinism contract: base's fields first, then own, each in SOURCE
            // order — never hash order. Access labels and static flags ride the entry.
            auto eng = make_reflective_engine();
            eng->execute(R"(
                class FBase { int hp = 5; private: string secret = "s"; }
                class FDer : FBase { float speed = 1.5; static int count = 0; }
            )");
            eng->execute("var fs = reflect::fields(FDer());");
            check_eq(eng->execute("fs.size()").as_int(), (int64_t)4);
            check_eq(eng->execute("fs[0][\"name\"]").as<std::string>(), "hp");
            check_eq(eng->execute("fs[0][\"from\"]").as<std::string>(), "FBase");
            check_eq(eng->execute("fs[0][\"type\"]").as<std::string>(), "int");
            check_eq(eng->execute("fs[1][\"name\"]").as<std::string>(), "secret");
            check_eq(eng->execute("fs[1][\"access\"]").as<std::string>(), "private");
            check_eq(eng->execute("fs[2][\"name\"]").as<std::string>(), "speed");
            check_eq(eng->execute("fs[2][\"from\"]").as<std::string>(), "FDer");
            check_eq(eng->execute("fs[3][\"name\"]").as<std::string>(), "count");
            check_eq(eng->execute("fs[3][\"static\"]").as<bool>(), true);
            // Order is a cross-engine contract, not one engine's accident
            auto eng2 = make_reflective_engine();
            eng2->execute(R"(
                class FBase { int hp = 5; private: string secret = "s"; }
                class FDer : FBase { float speed = 1.5; static int count = 0; }
            )");
            check_eq(eng2->execute("reflect::fields(FDer())[0][\"name\"]").as<std::string>(), "hp");
            check_eq(eng2->execute("reflect::fields(FDer())[2][\"name\"]").as<std::string>(), "speed");
        });

        test("fields_reload_recaptures_new_shape_and_order", [this]() {
            auto eng = make_reflective_engine();
            eng->execute("class Shape { int a = 1; int b = 2; }");
            check_eq(eng->execute("reflect::fields(Shape()).size()").as_int(), (int64_t)2);
            eng->execute("class Shape { int b = 2; int c = 3; int a = 1; }");   // reload: reorder + add
            eng->execute("var fs2 = reflect::fields(\"Shape\");");
            check_eq(eng->execute("fs2.size()").as_int(), (int64_t)3);
            check_eq(eng->execute("fs2[0][\"name\"]").as<std::string>(), "b");
            check_eq(eng->execute("fs2[1][\"name\"]").as<std::string>(), "c");
            check_eq(eng->execute("fs2[2][\"name\"]").as<std::string>(), "a");
        });

        test("fields_of_host_class_reports_schema_kinds", [this]() {
            auto eng = make_reflective_engine();
            dynamic_binder<reflect_host_thing>(*eng, "HostThing2")
                .constructor<>()
                .auto_bind()
                .build();
            eng->execute("var hf = reflect::fields(\"HostThing2\");");
            check_eq(eng->execute("hf.size()").as_int(), (int64_t)1);
            check_eq(eng->execute("hf[0][\"name\"]").as<std::string>(), "armor");
            check_eq(eng->execute("hf[0][\"type\"]").as<std::string>(), "int");
            check_eq(eng->execute("hf[0][\"kind\"]").as<std::string>(), "field");
            // Non-class values: empty array, never an error (property grids call this on anything)
            check_eq(eng->execute("reflect::fields(42).size()").as_int(), (int64_t)0);
        });

        test("instances_walks_the_live_registry", [this]() {
            auto eng = make_reflective_engine();
            eng->execute("class Tracked { int n = 0; }");
            eng->execute("var a = new Tracked(); var b = new Tracked(); a.n = 7;");
            check_eq(eng->execute("reflect::instances(\"Tracked\").size()").as_int(), (int64_t)2);
            check_eq(eng->execute(
                "var total = 0; for (auto t : reflect::instances(\"Tracked\")) { total = total + t.n; } total"
            ).as_int(), (int64_t)7);
        });

        test("sandbox_engine_reflects_nothing_it_did_not_register", [this]() {
            // The allowlist contract: reflection is a capability over THIS engine's
            // registrations. A second engine in the same process knows nothing about
            // the first engine's classes.
            auto eng_a = make_reflective_engine();
            eng_a->execute("class OnlyInA { int x = 1; }");
            auto eng_b = make_reflective_engine();
            check_throws([&]() { eng_b->execute("reflect::bases(\"OnlyInA\");"); },
                "engine B cannot name engine A's class");
        });
    }
};

} // namespace jai::foundry::tests

using reflection_tests = jai::foundry::tests::reflection_tests;
FOUNDRY_REGISTER(reflection_tests)
