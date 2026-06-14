// Regression tests for the Phase 3 hot-reload correctness bugs (June release review).
// Written tests-first: each asserts the CORRECT post-reload behavior, so it fails against
// the current machinery and passes once the bug is fixed.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class hot_reload_correctness_tests : public suite {
public:
    hot_reload_correctness_tests() : suite("Hot Reload Correctness") {}

    void forge_tests() override {

        // Static fields are runtime state; a reload must preserve them, not reset to the
        // initializer.
        test("static_state_preserved_across_reload", [&]() {
            auto eng = engine::make();
            eng->execute("class Counter { static int count = 0; }");
            eng->execute("Counter::count = 5;");
            check_eq(eng->execute("Counter::count").as<int>(), 5);
            eng->execute("class Counter { static int count = 0; }"); // reload, same shape
            check_eq(eng->execute("Counter::count").as<int>(), 5);   // bug: resets to 0
        });

        // A static dropped from the new definition must no longer be accessible.
        test("removed_static_inaccessible_after_reload", [&]() {
            auto eng = engine::make();
            eng->execute("class S { static int a = 1; static int b = 2; }");
            eng->execute("class S { static int a = 1; }"); // b removed
            check_throws([&]() { (void)eng->execute("S::b"); }, "removed static must be inaccessible");
        });

        // Reloading a BASE class must not drop the derived instance's own field data.
        test("base_reload_preserves_derived_instance_data", [&]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Base { int bv = 1; }
                class Derived : Base { int dv = 2; }
                auto d = Derived();
                d.bv = 88;
                d.dv = 99;
            )");
            eng->execute("class Base { int bv = 1; int nb = 7; }"); // base gains a field
            check_eq(eng->execute("d.dv").as<int>(), 99); // bug: derived data lost
            check_eq(eng->execute("d.bv").as<int>(), 88); // inherited data preserved
            check_eq(eng->execute("d.nb").as<int>(), 7);  // new inherited field added
        });

        // PENDING (deferred): changing a field's declared type on reload should adopt the
        // new-typed default, not keep the stale old-typed value. The real fix is in the
        // change-detection: a type-only change keeps the same field-id set, so `fields_changed`
        // stays false and migration never runs. The fix must fold field value-kinds into the
        // fingerprint / fields_changed test, and reset only the type-changed field — without
        // disturbing the base-reload (update_instances_from_base) path. Left as the next target.
        // test("field_type_change_resets_value", [&]() {
        //     auto eng = engine::make();
        //     eng->execute(R"( class Box { int val = 0; } auto b = Box(); b.val = 42; )");
        //     eng->execute("class Box { string val = \"\"; }"); // int -> string
        //     check_eq(eng->execute("b.val").as<std::string>(), std::string(""));
        // });

        // An error in one instance's hot_reload_migrate hook must not abort the reload for
        // the others.
        test("migrate_hook_error_is_isolated", [&]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Item {
                    int v = 0;
                    void hot_reload_migrate() { if (v == 1) { throw "boom"; } }
                }
                auto a = Item(); a.v = 1;
                auto b = Item(); b.v = 2;
            )");
            eng->execute("class Item { int v = 0; int w = 5; void hot_reload_migrate() { if (v == 1) { throw \"boom\"; } } }");
            check_eq(eng->execute("b.w").as<int>(), 5); // b migrated despite a's hook throwing
            check_eq(eng->execute("b.v").as<int>(), 2);
        });

        // A hot_reload_migrate hook that constructs a new same-class instance must not hang
        // the engine (instances_ mutated while iterated). The budget caps the runaway.
        test("migrate_hook_constructing_instance_terminates", [&]() {
            auto eng = engine::make();
            eng->execution_budget(1.0);
            eng->execute(R"(
                class Node {
                    int v = 0;
                    void hot_reload_migrate() { auto n = Node(); }
                }
                auto root = Node();
            )");
            eng->execute("class Node { int v = 0; int extra = 1; void hot_reload_migrate() { auto n = Node(); } }");
            check_eq(eng->execute("root.extra").as<int>(), 1);
        });

        // A bound method cached before a reload must not keep running the old code.
        test("bound_method_not_stale_after_reload", [&]() {
            auto eng = engine::make();
            eng->execute(R"(
                class Greeter { int value() { return 1; } }
                auto g = Greeter();
            )");
            check_eq(eng->execute("g.value()").as<int>(), 1); // populate any bound-method cache
            eng->execute("class Greeter { int value() { return 2; } }"); // reload, new body
            check_eq(eng->execute("g.value()").as<int>(), 2); // bug: stale cache returns 1
        });

        // Constructor overloads (arity-dispatched) must survive a reload. (Script *methods*
        // don't overload by arity — that's default-parameter territory — so the review's
        // "overload collapse" only applies to constructors here.)
        test("constructor_overload_survives_reload", [&]() {
            auto eng = engine::make();
            const char* def = R"(
                class Pt {
                    int x = 0;
                    int y = 0;
                    Pt(int a) { x = a; }
                    Pt(int a, int b) { x = a; y = b; }
                }
            )";
            eng->execute(def);
            check_eq(eng->execute("Pt(5).x").as<int>(), 5);
            check_eq(eng->execute("Pt(3, 4).y").as<int>(), 4);
            eng->execute(def); // reload, identical
            check_eq(eng->execute("Pt(7).x").as<int>(), 7);     // bug would collapse to 2-arg ctor
            check_eq(eng->execute("Pt(1, 2).y").as<int>(), 2);
        });
    }
};

FOUNDRY_REGISTER(jai::foundry::tests::hot_reload_correctness_tests)

} // namespace jai::foundry::tests
