// Regression tests for the Phase 3 hot-reload correctness bugs (June release review).

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// C++ base for the script-extends-C++ reload test. Tracks live instances so the test can prove
// the _cpp_object survives a field-changing reload — neither dropped (which would run the
// destructor) nor silently reconstructed.
struct ReloadCppBase {
    int payload = 0;
    static int live_instances;
    ReloadCppBase() { ++live_instances; }
    ReloadCppBase(int v) : payload(v) { ++live_instances; }
    ~ReloadCppBase() { --live_instances; }
    int payload_value() const { return payload; }
};
int ReloadCppBase::live_instances = 0;

class hot_reload_correctness_tests : public suite {
public:
    hot_reload_correctness_tests() : suite("Hot Reload Correctness") {}

    void forge_tests() override {

        // Static fields are runtime state; a reload must preserve them, not reset to the
        // initializer.
        test("static_state_preserved_across_reload", [&]() {
            auto eng = make_engine();
            eng->execute("class Counter { static int count = 0; }");
            eng->execute("Counter::count = 5;");
            check_eq(eng->execute("Counter::count").as<int>(), 5);
            eng->execute("class Counter { static int count = 0; }"); // reload, same shape
            check_eq(eng->execute("Counter::count").as<int>(), 5);   // bug: resets to 0
        });

        // A static dropped from the new definition must no longer be accessible.
        test("removed_static_inaccessible_after_reload", [&]() {
            auto eng = make_engine();
            eng->execute("class S { static int a = 1; static int b = 2; }");
            eng->execute("class S { static int a = 1; }"); // b removed
            check_throws([&]() { (void)eng->execute("S::b"); }, "removed static must be inaccessible");
        });

        // Reloading a BASE class must not drop the derived instance's own field data.
        test("base_reload_preserves_derived_instance_data", [&]() {
            auto eng = make_engine();
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

        // Changing a field's declared type on reload (int -> string) must adopt the new-typed
        // default, not keep the stale old-typed value. The retype leaves the field-id set
        // identical, so `fields_changed` stayed false and migration was skipped — the type
        // change is detected against the live instance value instead.
        test("field_type_change_resets_value", [&]() {
            auto eng = make_engine();
            eng->execute(R"( class Box { int val = 0; } auto b = Box(); b.val = 42; )");
            eng->execute("class Box { string val = \"\"; }"); // int -> string
            check_eq(std::string(""), eng->execute("b.val").as<std::string>());
        });

        // The flip side: only a *type* change resets. Changing an initializer to a new value of
        // the SAME type is not a type change — existing instances keep their runtime value (only
        // new instances get the new default), matching established reload semantics.
        test("same_type_initializer_change_keeps_value", [&]() {
            auto eng = make_engine();
            eng->execute(R"( class Box { int val = 0; } auto b = Box(); b.val = 42; )");
            eng->execute("class Box { int val = 7; }"); // initializer 0 -> 7, still int
            check_eq(eng->execute("b.val").as<int>(), 42); // runtime value preserved, not reset
        });

        // An inherited field whose declared type changes when the BASE reloads must reset on the
        // derived instance too — the retype is propagated through update_instances_from_base.
        test("inherited_field_type_change_resets_on_derived", [&]() {
            auto eng = make_engine();
            eng->execute(R"(
                class Base { int bv = 0; }
                class Derived : Base { int dv = 0; }
                auto d = Derived();
                d.bv = 42;
                d.dv = 99;
            )");
            eng->execute("class Base { string bv = \"\"; }"); // inherited bv int -> string
            check_eq(std::string(""), eng->execute("d.bv").as<std::string>()); // retyped, reset
            check_eq(eng->execute("d.dv").as<int>(), 99); // derived-own data preserved
        });

        // An error in one instance's hot_reload_migrate hook must not abort the reload for
        // the others.
        test("migrate_hook_error_is_isolated", [&]() {
            auto eng = make_engine();
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
            auto eng = make_engine();
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

        // A static method's body must update on reload (statics are rebuilt through
        // add_static_script_method, the same door fresh classes use).
        test("static_method_body_updates_on_reload", [&]() {
            auto eng = make_engine();
            eng->execute("class Util { static int ver() { return 1; } }");
            check_eq(eng->execute("Util::ver()").as<int>(), 1);
            eng->execute("class Util { static int ver() { return 2; } }"); // reload, new body
            check_eq(eng->execute("Util::ver()").as<int>(), 2);
        });

        // A bound method cached before a reload must not keep running the old code.
        test("bound_method_not_stale_after_reload", [&]() {
            auto eng = make_engine();
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
            auto eng = make_engine();
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

        // A script class extending a C++ (dynamic_binder) base holds the live C++ object in a
        // runtime _cpp_object field that never appears in the declared field defaults. A
        // field-changing reload must carry it across, so the inherited C++ method stays callable
        // and the C++ object is neither destructed nor rebuilt.
        test("cpp_object_survives_field_changing_reload", [&]() {
            auto eng = make_engine();
            const int base_live = ReloadCppBase::live_instances;

            dynamic_binder<ReloadCppBase>(*eng, "ReloadCppBase")
                .constructor<int>()
                .method("payload_value", &ReloadCppBase::payload_value)
                .build();

            eng->execute(R"(
                class Wrapper : ReloadCppBase {
                    int extra = 0;
                    Wrapper(int v) : super(v) {}
                }
                auto w = Wrapper(7);
                w.extra = 3;
            )");
            check_eq(eng->execute("w.payload_value()").as<int>(), 7);   // inherited C++ method works
            check_eq(ReloadCppBase::live_instances - base_live, 1);     // exactly one C++ base alive

            eng->execute(R"(
                class Wrapper : ReloadCppBase {
                    int extra = 0;
                    int added = 9;
                    Wrapper(int v) : super(v) {}
                }
            )"); // field-changing reload (gains `added`)

            check_eq(eng->execute("w.payload_value()").as<int>(), 7);   // _cpp_object preserved
            check_eq(eng->execute("w.added").as<int>(), 9);             // new field present
            check_eq(eng->execute("w.extra").as<int>(), 3);             // existing data kept
            check_eq(ReloadCppBase::live_instances - base_live, 1);     // not destructed, not rebuilt
        });

        // A reentrant execute (host callback firing mid-script) must define at TOP level:
        // redefining a function that is currently on the call stack replaces the global
        // binding (old frames finish with their old bodies) and the binding persists.
        test("function_redefined_mid_call_persists", [&]() {
            auto eng = make_engine();
            jai::engine* raw = eng.get();
            eng->add_function("swap_fn", [raw]() {
                raw->execute("function f(n) { return 1000; }");
            });
            eng->add_function("swap_other", [raw]() {
                raw->execute("function g(n) { return 2000; }");
            });
            // A different, not-on-stack function defined mid-call persists
            eng->execute("function fo(n) { if (n == 2) { swap_other(); } if (n <= 0) { return 0; } return fo(n - 1) + 1; }");
            check_eq((int64_t)4, eng->execute("fo(4)").as_int());
            check_eq((int64_t)2000, eng->execute("g(1)").as_int());
            // Redefining the RUNNING function replaces the global binding: old frames
            // finish with their old bodies, recursion resolves the new one, and the
            // new definition persists
            eng->execute("function f(n) { if (n == 2) { swap_fn(); } if (n <= 0) { return 0; } return f(n - 1) + 1; }");
            check_eq((int64_t)1003, eng->execute("f(4)").as_int());
            check_eq((int64_t)1000, eng->execute("f(1)").as_int());
            check_eq((int64_t)1000, eng->execute("f(4)").as_int());
        });

        test("self_redefinition_mid_call_persists_after_return", [&]() {
            auto eng = make_engine();
            jai::engine* raw = eng.get();
            eng->add_function("swap_min", [raw]() {
                raw->execute("function m(n) { return 77; }");
            });
            eng->execute("function m(n) { if (n == 1) { swap_min(); return m(0); } return 5; }");
            check_eq((int64_t)77, eng->execute("m(1)").as_int());
            // The interpreter used to lose the global binding entirely here
            check_eq((int64_t)77, eng->execute("m(0)").as_int());
        });

        // Class reloaded while one of its methods is mid-recursion: the continuing old
        // frame consistently fails to resolve the sibling method (pinned quirk on both
        // backends), but afterwards the instance, the class and new instances must all
        // stay alive and resolve the NEW body.
        test("class_reload_mid_recursion_keeps_globals_coherent", [&]() {
            auto eng = make_engine();
            jai::engine* raw = eng.get();
            eng->add_function("swap_rec", [raw]() {
                raw->execute("class R { int rec(int k) { if (k <= 0) { return 0; } return rec(k - 1) + 100; } }");
            });
            eng->execute(R"(
                class R { int rec(int k) { if (k == 2) { swap_rec(); } if (k <= 0) { return 0; } return rec(k - 1) + 1; } }
                auto r = R();
            )");
            try { eng->execute("r.rec(4)"); } catch (const std::exception&) {}
            check_eq((int64_t)400, eng->execute("r.rec(4)").as_int());
            check_eq((int64_t)100, eng->execute("auto r2 = R(); r2.rec(1);").as_int());
        });
    }
};

FOUNDRY_REGISTER(jai::foundry::tests::hot_reload_correctness_tests)

} // namespace jai::foundry::tests
