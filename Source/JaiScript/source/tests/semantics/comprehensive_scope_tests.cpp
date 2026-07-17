#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>
#include <vector>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Global log to track construction/destruction order
static std::vector<std::string> operation_log;

class CppBoundObject {
public:
    static int instance_count;  // Total created (for unique IDs)
    static int alive_count;     // Currently alive
    int id;
    std::string name;

    CppBoundObject(const std::string& n) : id(++instance_count), name(n) {
        alive_count++;
        operation_log.push_back("CppBoundObject(\"" + name + "\") ctor, id=" + std::to_string(id) + ", alive=" + std::to_string(alive_count));
    }

    CppBoundObject(const CppBoundObject& other) : id(other.id), name(other.name) {
        alive_count++;  // Copies also increase alive count
        operation_log.push_back("CppBoundObject COPY from id=" + std::to_string(other.id) + ", alive=" + std::to_string(alive_count));
    }

    ~CppBoundObject() {
        alive_count--;
        operation_log.push_back("~CppBoundObject(\"" + name + "\") dtor, id=" + std::to_string(id) + ", alive=" + std::to_string(alive_count));
    }

    void instance_method() {
        operation_log.push_back("CppBoundObject::instance_method() called on \"" + name + "\"");
    }

    static void static_method() {
        operation_log.push_back("CppBoundObject::static_method() called");
    }

    std::string get_name() const { return name; }
    int get_id() const { return id; }

    static void reset() {
        instance_count = 0;
        alive_count = 0;
        operation_log.clear();
    }
};

int CppBoundObject::instance_count = 0;
int CppBoundObject::alive_count = 0;

void register_cpp_bound_object(engine& eng) {
    dynamic_binder<CppBoundObject>(eng, "CppBoundObject")
        .constructor<std::string>()
        .method("instance_method", &CppBoundObject::instance_method)
        .method("get_name", &CppBoundObject::get_name)
        .method("get_id", &CppBoundObject::get_id)
        .property("name", &CppBoundObject::name)
        .property("id", &CppBoundObject::id)
        .static_method("static_method", &CppBoundObject::static_method)
        .static_property("instance_count", &CppBoundObject::instance_count)
        .static_property("alive_count", &CppBoundObject::alive_count)
        .build();
}

class comprehensive_scope_tests : public suite {
public:
    comprehensive_scope_tests() : suite("Comprehensive Scope Tests") {}

    void forge_tests() override {
        test("block_scope_destruction", [this]() {
            auto eng = make_engine();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                // Test 1: Simple block scope
                {
                    auto obj1 = CppBoundObject("block_scope");
                }
                // obj1 should be destroyed here

                // Test 2: Nested blocks
                {
                    auto outer = CppBoundObject("outer_block");
                    {
                        auto inner = CppBoundObject("inner_block");
                    }
                    // inner should be destroyed here
                }
                // outer should be destroyed here

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "All C++ objects should be destroyed");

            // Verify construction/destruction order
            std::cout << "\n=== Operation Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }

            // Should have: ctor block_scope, dtor block_scope, ctor outer, ctor inner, dtor inner, dtor outer
            check(operation_log.size() >= 6, "Should have at least 6 operations");
        });

        test("if_statement_scope", [this]() {
            auto eng = make_engine();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                auto condition = true;

                if (condition) {
                    auto if_obj = CppBoundObject("if_true_branch");
                }
                // if_obj should be destroyed here

                if (!condition) {
                    auto else_obj = CppBoundObject("if_false_branch");
                } else {
                    auto else_true = CppBoundObject("else_branch");
                }
                // else_true should be destroyed here

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "All if-scope objects destroyed");

            std::cout << "\n=== If Statement Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }
        });

        test("for_loop_scope", [this]() {
            auto eng = make_engine();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                // Test for loop scope with initialization variable
                for (auto i = 0; i < 3; i = i + 1) {
                    auto loop_obj = CppBoundObject("loop_iteration");
                }
                // loop_obj should be destroyed after each iteration
                // i should be destroyed when for loop ends

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "All loop objects destroyed");

            std::cout << "\n=== For Loop Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }

            // Should see 3 constructions and 3 destructions
            int ctor_count = 0;
            int dtor_count = 0;
            for (const auto& op : operation_log) {
                if (op.find("ctor") != std::string::npos) ctor_count++;
                if (op.find("dtor") != std::string::npos) dtor_count++;
            }
            check_eq(ctor_count, 3, "Should construct 3 objects in loop");
            check_eq(dtor_count, 3, "Should destruct 3 objects in loop");
        });

        test("function_scope", [this]() {
            auto eng = make_engine();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                void test_function() {
                    auto func_local = CppBoundObject("function_local");
                }
                // func_local should be destroyed when function returns

                test_function();
                test_function();  // Call twice to verify destruction each time

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "All function locals destroyed");

            std::cout << "\n=== Function Scope Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }

            // Should see 2 constructions and 2 destructions
            int ctor_count = 0;
            int dtor_count = 0;
            for (const auto& op : operation_log) {
                if (op.find("ctor") != std::string::npos) ctor_count++;
                if (op.find("dtor") != std::string::npos) dtor_count++;
            }
            check_eq(ctor_count, 2, "Should construct object twice");
            check_eq(dtor_count, 2, "Should destruct object twice");
        });

        test("lambda_scope", [this]() {
            auto eng = make_engine();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                auto make_lambda = []() {
                    auto lambda_local = CppBoundObject("lambda_local");
                    return lambda_local.get_id();
                };

                auto id1 = make_lambda();
                auto id2 = make_lambda();

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "All lambda locals destroyed");

            std::cout << "\n=== Lambda Scope Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }
        });

        test("script_class_with_destructor", [this]() {
            auto eng = make_engine();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                class ScriptClass {
                    auto cpp_member;

                    ScriptClass(auto name) {
                        cpp_member = CppBoundObject(name);
                    }

                    ~ScriptClass() {
                        // Destructor should be called automatically
                        // cpp_member will be destroyed when ScriptClass is destroyed
                    }

                    void instance_method() {
                        cpp_member.instance_method();
                    }

                    static void static_method() {
                        CppBoundObject::static_method();
                    }
                }

                // Test script class scope
                {
                    auto script_obj = ScriptClass("script_class_member");
                    script_obj.instance_method();
                }
                // script_obj and its cpp_member should both be destroyed here

                ScriptClass::static_method();

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "Script class member destroyed");

            std::cout << "\n=== Script Class Destructor Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }
        });

        test("mixed_scopes_comprehensive", [this]() {
            auto eng = make_engine();
            stdlib::register_all(*eng);  // Register stdlib to get print()
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                // Global scope object (will live until script ends)
                auto global_obj = CppBoundObject("global");

                class ScriptClassWithDestructor {
                    auto member;

                    ScriptClassWithDestructor(auto name) {
                        member = CppBoundObject(name + "_member");
                    }

                    ~ScriptClassWithDestructor() {
                        // member will be destroyed automatically
                    }

                    void call_lambda() {
                        print("call_lambda: before creating lambda");
                        auto lambda = [this]() {
                            print("lambda: inside lambda body");
                            auto lambda_obj = CppBoundObject("lambda_capture");
                            member.instance_method();
                        };
                        print("call_lambda: after creating lambda, before calling");
                        lambda();
                        print("call_lambda: after calling lambda");
                        // lambda_obj destroyed here
                    }
                }

                void test_function() {
                    auto func_obj = CppBoundObject("function");

                    for (auto i = 0; i < 2; i = i + 1) {
                        auto loop_obj = CppBoundObject("loop");

                        if (i == 0) {
                            auto if_obj = CppBoundObject("if_branch");
                        }
                        // if_obj destroyed here if created
                        // loop_obj destroyed here
                    }
                    // i destroyed here
                    // func_obj destroyed here
                }

                {
                    auto block_obj = CppBoundObject("block");
                    auto script_class = ScriptClassWithDestructor("script_class");
                    script_class.call_lambda();
                    test_function();
                    // script_class destroyed here (and its member)
                    // block_obj destroyed here
                }

                // global_obj still alive

                true
            )");

            check_eq(result.as<bool>(), true);

            // global_obj is still alive, so alive_count should be 1
            check_eq(CppBoundObject::alive_count, 1, "Only global object should remain");

            std::cout << "\n=== Comprehensive Mixed Scopes Log ===" << std::endl;
            for (size_t i = 0; i < operation_log.size(); ++i) {
                std::cout << i << ": " << operation_log[i] << std::endl;
            }

            // Verify we have destructor calls for everything except global
            int ctor_count = 0;
            int dtor_count = 0;
            for (const auto& op : operation_log) {
                // Count both "ctor" and "COPY" as constructions
                if (op.find("ctor") != std::string::npos || op.find("COPY") != std::string::npos) ctor_count++;
                if (op.find("dtor") != std::string::npos) dtor_count++;
            }

            std::cout << "\nCtor count: " << ctor_count << ", Dtor count: " << dtor_count << std::endl;

            // Should have ctor_count = dtor_count + 1 (the global object)
            check_eq(ctor_count, dtor_count + 1, "All non-global objects should be destroyed");
        });

        test("early_return_scope_cleanup", [this]() {
            auto eng = make_engine();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                int test_early_return(auto should_return) {
                    auto before_if = CppBoundObject("before_if");

                    if (should_return) {
                        auto in_if = CppBoundObject("in_if");
                        return 42;
                        // in_if should be destroyed here
                    }
                    // before_if should be destroyed here if we don't return early

                    return 0;
                }

                auto result1 = test_early_return(true);   // early return
                auto result2 = test_early_return(false);  // normal return

                result1 == 42 && result2 == 0
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "All objects destroyed despite early return");

            std::cout << "\n=== Early Return Scope Log ===" << std::endl;
            for (const auto& op : operation_log) {
                std::cout << op << std::endl;
            }
        });

        test("exception_scope_cleanup", [this]() {
            auto eng = make_engine();
            register_cpp_bound_object(*eng);
            CppBoundObject::reset();

            auto result = eng->execute(R"(
                void throwing_function() {
                    auto before_throw = CppBoundObject("before_throw");
                    // In the future when exceptions are supported, test:
                    // throw "error";
                    // before_throw should be destroyed here
                }

                // For now, just test normal flow
                throwing_function();

                true
            )");

            check_eq(result.as<bool>(), true);
            check_eq(CppBoundObject::alive_count, 0, "Objects destroyed in exception scenarios");
        });
    }

    void pre_test() override {
        CppBoundObject::reset();
    }

    void post_test() override {
        // Verify no leaks after each test
        if (CppBoundObject::alive_count > 1) {  // Allow 1 for global_obj in comprehensive test
            std::cerr << "WARNING: Potential memory leak, alive_count = "
                     << CppBoundObject::alive_count << std::endl;
        }
    }
};

// C++ unqualified-lookup order inside methods: locals -> class members -> globals.
// Pins the member-shadows-global contract (a same-named global must never hide a
// field or method inside the class's own methods) across every bare-identifier
// path: calls, reads, stores, compound stores, ++/--, index bases, ref args,
// closures, statics, inheritance, and hot-reload flips.
class member_shadowing_tests : public suite {
public:
    member_shadowing_tests() : suite("Member Shadowing Tests") {}

    static std::shared_ptr<jai::engine> make_stdlib_engine() {
        auto e = make_engine();
        stdlib::register_all(*e);
        return e;
    }

    void forge_tests() override {
        test("method_shadows_global_same_arity", [this]() {
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                string who = "";
                void greet(int n) { who = "global"; }
                class A {
                    void greet(int n) { who = "method"; }
                    void go() { greet(1); }
                }
                auto a = A();
                a.go();
                who
            )");
            check_eq(std::string("method"), r.as<std::string>());
        });

        test("method_shadows_global_wrong_arity_global", [this]() {
            // The jaidoom shape: 0-arg global + 1-arg method, bare 1-arg call.
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                string who = "";
                void sync_exit() { who = "global"; }
                class Level {
                    int exits = 0;
                    void sync_exit(int player) { who = "method " + to_string(player); exits += 1; }
                    void trigger(int player) { sync_exit(player); }
                }
                auto l = Level();
                l.trigger(3);
                who + "/" + to_string(l.exits)
            )");
            check_eq(std::string("method 3/1"), r.as<std::string>());
        });

        test("field_shadows_global_read_write_compound", [this]() {
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                int hp = 999;
                class Creature {
                    int hp = 5;
                    int read_hp() { return hp; }
                    void write_hp() { hp = 42; }
                    void bump_hp() { hp += 1; hp++; }
                }
                auto c = Creature();
                int before = c.read_hp();
                c.write_hp();
                c.bump_hp();
                to_string(before) + "/" + to_string(c.hp) + "/" + to_string(hp)
            )");
            check_eq(std::string("5/44/999"), r.as<std::string>());
        });

        test("local_shadows_field_shadows_global", [this]() {
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                int v = 1;
                class B {
                    int v = 2;
                    int local_wins() { int v = 3; return v; }
                    int field_wins() { return v; }
                    int param_wins(int v) { return v; }
                }
                auto b = B();
                to_string(b.local_wins()) + "/" + to_string(b.field_wins()) + "/" + to_string(b.param_wins(4))
            )");
            check_eq(std::string("3/2/4"), r.as<std::string>());
        });

        test("inherited_members_shadow_globals", [this]() {
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                int score = 100;
                string ping() { return "global"; }
                class Base {
                    int score = 7;
                    string ping() { return "base"; }
                }
                class Derived : Base {
                    string probe() { return ping() + "/" + to_string(score); }
                    void raise() { score += 3; }
                }
                auto d = Derived();
                d.raise();
                d.probe() + "/" + to_string(score)
            )");
            check_eq(std::string("base/10/100"), r.as<std::string>());
        });

        test("ctor_name_bare_call_still_constructs", [this]() {
            // The class-name global stays the constructor: a bare Fact(...) inside a
            // method of Fact must build a NEW instance, never self-bind as a member.
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                class Fact {
                    int n = 0;
                    Fact(int v) { n = v; }
                    Fact successor() { return Fact(n + 1); }
                }
                auto f = Fact(5);
                f.successor().n
            )");
            check_eq((int64_t)6, r.as_int());
        });

        test("closure_in_method_member_over_global", [this]() {
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                int fuel = 500;
                class Rocket {
                    int fuel = 3;
                    int probe() {
                        int local = 9;
                        auto f = [=]() { return fuel * 100 + local; };
                        return f();
                    }
                }
                auto rk = Rocket();
                rk.probe()
            )");
            check_eq((int64_t)309, r.as_int());
        });

        test("statics_shadow_globals_in_static_methods", [this]() {
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                int count = 1000;
                string tick() { return "global"; }
                class S {
                    static int count = 0;
                    static string tick() { count += 1; return "static"; }
                    static string go() { return tick(); }
                }
                S::go() + "/" + to_string(S::count) + "/" + to_string(count)
            )");
            check_eq(std::string("static/1/1000"), r.as<std::string>());
        });

        test("statics_shadow_globals_in_instance_methods", [this]() {
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                int total = 1000;
                class T {
                    static int total = 0;
                    void add(int n) { total += n; }
                }
                auto t = T();
                t.add(5);
                to_string(T::total) + "/" + to_string(total)
            )");
            check_eq(std::string("5/1000"), r.as<std::string>());
        });

        test("index_base_field_shadows_global", [this]() {
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                var grid = [100, 200, 300];
                class Board {
                    var grid = [1, 2, 3];
                    int read(int i) { return grid[i]; }
                    void write(int i, int v) { grid[i] = v; }
                    void bump(int i) { grid[i] += 10; }
                }
                auto b = Board();
                b.write(0, 7);
                b.bump(1);
                to_string(b.read(0)) + "/" + to_string(b.read(1)) + "/" + to_string(grid[0]) + "/" + to_string(grid[1])
            )");
            check_eq(std::string("7/12/100/200"), r.as<std::string>());
        });

        test("ref_arg_field_shadows_global", [this]() {
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                int ammo = 999;
                void reload(int& x) { x = 30; }
                class Gun {
                    int ammo = 0;
                    void refill() { reload(ammo); }
                }
                auto g = Gun();
                g.refill();
                to_string(g.ammo) + "/" + to_string(ammo)
            )");
            check_eq(std::string("30/999"), r.as<std::string>());
        });

        test("no_member_no_shadow_globals_unchanged", [this]() {
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                int power = 11;
                string shout() { return "global"; }
                class Plain {
                    int other = 0;
                    string go() { power += 1; return shout() + "/" + to_string(power); }
                }
                auto p = Plain();
                p.go()
            )");
            check_eq(std::string("global/12"), r.as<std::string>());
        });

        test("member_shadow_hot_ladder_warm", [this]() {
            // IC-warmth: bare shadowed field reads/writes in a hot loop must stay on
            // the member after the per-site caches arm.
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                int acc = 100000;
                class W {
                    int acc = 0;
                    int spin() {
                        for (int i = 0; i < 1000; i++) { acc += 1; }
                        return acc;
                    }
                }
                auto w = W();
                to_string(w.spin()) + "/" + to_string(acc)
            )");
            check_eq(std::string("1000/100000"), r.as<std::string>());
        });

        test("bare_method_call_hot_loop_stays_member", [this]() {
            auto e = make_stdlib_engine();
            auto r = e->execute(R"(
                int step() { return 1000; }
                class Walker {
                    int steps = 0;
                    int step() { return 1; }
                    int walk() {
                        int sum = 0;
                        for (int i = 0; i < 100; i++) { sum += step(); }
                        return sum;
                    }
                }
                auto wk = Walker();
                wk.walk()
            )");
            check_eq((int64_t)100, r.as_int());
        });

        test("hot_reload_flips_bare_resolution", [this]() {
            auto e = make_stdlib_engine();
            auto r1 = e->execute(R"(
                int power = 100;
                class R { int get() { int s = 0; for (int i = 0; i < 50; i++) { s = get_once(); } return s; } int get_once() { return power; } }
                auto r = R();
                r.get()
            )");
            check_eq((int64_t)100, r1.as_int());
            auto r2 = e->execute(R"(
                class R { int power = 7; int get() { int s = 0; for (int i = 0; i < 50; i++) { s = get_once(); } return s; } int get_once() { return power; } }
                r.get()
            )");
            check_eq((int64_t)7, r2.as_int());
            auto r3 = e->execute(R"(
                class R { int get() { int s = 0; for (int i = 0; i < 50; i++) { s = get_once(); } return s; } int get_once() { return power; } }
                r.get()
            )");
            check_eq((int64_t)100, r3.as_int());
        });

        test("member_shadow_is_by_name_not_signature", [this]() {
            // C++ contract: shadowing is by NAME. A member with no viable overload
            // hides the global rather than falling back to it -- the call errors.
            auto e = make_stdlib_engine();
            e->execute(R"(
                string flavor(int n) { return "global"; }
                class Tea {
                    string flavor() { return "member"; }
                    string sip() { return flavor(42); }
                }
                auto cup = Tea();
            )");
            check_throws([&]() { e->execute("cup.sip();"); });
            check_eq(std::string("member"), e->execute("cup.flavor()").as<std::string>());
            check_eq(std::string("global"), e->execute("flavor(1)").as<std::string>());
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::comprehensive_scope_tests)
FOUNDRY_REGISTER(jai::foundry::tests::member_shadowing_tests)
