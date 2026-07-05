#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <cassert>
#include <utility>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Unregistered class for opaque cpp_bound pins (make_value(T*) on a type no engine knows).
struct t1_opaque_probe { int v = 1; };

class cpp_bound_tests : public suite {
public:
    cpp_bound_tests() : suite("C++ Bound Values") {}
    
    void forge_tests() override {
        test("primitive_types_read_write", [this]() {
            auto eng = jai::foundry::make_engine();
            
            int counter = 100;
            double temperature = 20.5;
            std::string name = "Initial";
            bool flag = false;
            
            // Add global references
            eng->add_global_ref("counter", counter);
            eng->add_global_ref("temperature", temperature);
            eng->add_global_ref("name", name);
            eng->add_global_ref("flag", flag);
            
            // Test reading from script
            auto result = eng->execute("counter");
            check_eq(result.template as<int>(), 100, "Read C++ int from script");
            
            result = eng->execute("temperature");
            check_eq(result.template as<double>(), 20.5, "Read C++ double from script");
            
            result = eng->execute("name");
            check_eq(result.template as<std::string>(), "Initial", "Read C++ string from script");
            
            result = eng->execute("flag");
            check_eq(result.template as<bool>(), false, "Read C++ bool from script");
            
            // Test writing from script
            eng->execute("counter = 200;");
            check_eq(counter, 200, "Write to C++ int from script");
            
            eng->execute("temperature = 30.5;");
            check_eq(temperature, 30.5, "Write to C++ double from script");
            
            eng->execute("name = \"Updated\";");
            check_eq(name, "Updated", "Write to C++ string from script");
            
            eng->execute("flag = true;");
            check_eq(flag, true, "Write to C++ bool from script");
            
            // Test arithmetic operations
            eng->execute("counter = counter + 50;");
            check_eq(counter, 250, "Arithmetic on C++ bound int");
            
            // Test string concatenation
            eng->execute("name = name + \" Again\";");
            check_eq(name, "Updated Again", "String concatenation on C++ bound string");
        });
        
        test("cpp_changes_visible_in_script", [this]() {
            auto eng = jai::foundry::make_engine();
            
            int health = 100;
            eng->add_global_ref("health", health);
            
            // Change in C++
            health = 75;
            
            // Read from script
            auto result = eng->execute("health");
            check_eq(result.template as<int>(), 75, "C++ changes visible in script");
        });
        
        test("integration_with_functions", [this]() {
            auto eng = jai::foundry::make_engine();
            
            int score = 0;
            eng->add_global_ref("score", score);
            
            eng->add_function("add_score", [&score](int points) {
                score += points;
            });
            
            eng->execute("add_score(10);");
            check_eq(score, 10, "Function modifies C++ bound variable");
            
            eng->execute("score = score * 2;");
            check_eq(score, 20, "Script modifies same variable");
        });
        
        test("mixed_operations", [this]() {
            auto eng = jai::foundry::make_engine();
            
            int x = 10;
            int y = 20;
            eng->add_global_ref("x", x);
            eng->add_global_ref("y", y);
            eng->add_global("z", 30);  // Regular global for comparison
            
            // Mixed arithmetic
            auto result = eng->execute("x + y + z");
            check_eq(result.template as<int>(), 60, "Mixed arithmetic with bound and regular values");
            
            // Modify bound values
            eng->execute("x = x * 2; y = y + 5;");
            check_eq(x, 20, "x modified correctly");
            check_eq(y, 25, "y modified correctly");
            
            // Check they can be used in expressions
            result = eng->execute("x > y");
            check_eq(result.template as<bool>(), false, "Comparison with bound values");
        });

        // ---- Stage-0 pin tests (thin_value spec section 6): current cpp_bound semantics
        // ---- that must survive the 56->32 byte script_value fold, green on HEAD.

        // char is integral, so make_cpp_bound's integral branch wins: a bound char is a
        // width-1 bound INT today (reads as its code point) — pinned as-is.
        test("pin_bound_char_read_write", [this]() {
            auto eng = jai::foundry::make_engine();
            char ch = 'a';
            eng->add_global_ref("bch", ch);
            check_eq((int64_t)97, eng->execute("bch").as_int(), "bound char reads as int code point");
            check_true(eng->execute("bch == 97").as_bool(), "bound char == int");
            eng->execute("bch = 122;");
            check_eq('z', ch, "write to C++ char from script (low byte)");
            ch = 'q';
            check_eq((int64_t)113, eng->execute("bch").as_int(), "C++ char change visible in script");
        });

        test("pin_copy_alias_clone_detach", [this]() {
            auto eng = jai::foundry::make_engine();
            int cav = 100;
            eng->add_global_ref("cav", cav);
            auto v = eng->execute("cav");
            check_true(v.is_cpp_bound(), "execute returns a bound copy");
            cav = 42;
            check_eq(42, v.template as<int>(), "bound copy reads the live variable");
            script_value copyOfBound = v;
            cav = 7;
            check_true(copyOfBound.is_cpp_bound(), "copy of bound stays bound");
            check_eq(7, copyOfBound.template as<int>(), "copy of bound aliases the same variable");
            auto detached = v.clone();
            check_false(detached.is_cpp_bound(), "clone of bound primitive detaches");
            check_eq(7, detached.template as<int>(), "clone snapshots the live value");
            cav = 9;
            check_eq(7, detached.template as<int>(), "clone unaffected by later C++ writes");
            check_eq(9, v.template as<int>(), "original copy still live");
        });

        test("pin_moved_from_bound", [this]() {
            auto eng = jai::foundry::make_engine();
            int mvv = 5;
            eng->add_global_ref("mvv", mvv);
            auto v = eng->execute("mvv");
            check_true(v.is_cpp_bound(), "pre-move: bound");
            script_value w = std::move(v);
            check_true(w.is_cpp_bound(), "moved-to keeps the binding");
            check_false(v.is_cpp_bound(), "moved-from loses the binding");
            check_true(v.is_null(), "moved-from reads as null");
            check_null(v.get_engine(), "moved-from engine is null");
            v = w;
            check_true(v.is_cpp_bound(), "moved-from is reassignable");
            mvv = 11;
            check_eq(11, v.template as<int>(), "reassigned value aliases again");
        });

        test("pin_bound_int_operator_reads", [this]() {
            auto eng = jai::foundry::make_engine();
            script_int ba = 7;
            script_int bb = 3;
            eng->add_global_ref("ba", ba);
            eng->add_global_ref("bb", bb);
            check_eq((int64_t)10, eng->execute("ba + 3").as_int(), "bound +");
            check_eq((int64_t)10, eng->execute("3 + ba").as_int(), "bound + (rhs)");
            check_eq((int64_t)5, eng->execute("ba - 2").as_int(), "bound -");
            check_eq((int64_t)3, eng->execute("10 - ba").as_int(), "bound - (rhs)");
            check_eq((int64_t)14, eng->execute("ba * 2").as_int(), "bound *");
            check_eq((int64_t)3, eng->execute("ba / 2").as_int(), "bound /");
            check_eq((int64_t)2, eng->execute("14 / ba").as_int(), "bound / (rhs)");
            check_eq((int64_t)3, eng->execute("ba % 4").as_int(), "bound %");
            check_eq((int64_t)2, eng->execute("16 % ba").as_int(), "bound % (rhs)");
            check_true(eng->execute("ba < 10").as_bool(), "bound <");
            check_true(eng->execute("ba <= 7").as_bool(), "bound <=");
            check_true(eng->execute("ba > 3").as_bool(), "bound >");
            check_false(eng->execute("ba >= 8").as_bool(), "bound >=");
            check_true(eng->execute("ba == 7").as_bool(), "bound ==");
            check_true(eng->execute("7 == ba").as_bool(), "bound == (rhs)");
            check_false(eng->execute("ba != 7").as_bool(), "bound !=");
            check_eq((int64_t)-1, eng->execute("ba <=> 9").as_int(), "bound <=> less");
            check_eq((int64_t)0, eng->execute("ba <=> 7").as_int(), "bound <=> equal");
            check_eq((int64_t)1, eng->execute("9 <=> ba").as_int(), "bound <=> (rhs)");
            check_eq((int64_t)5, eng->execute("ba & 5").as_int(), "bound &");
            check_eq((int64_t)15, eng->execute("ba | 8").as_int(), "bound |");
            check_eq((int64_t)6, eng->execute("ba ^ 1").as_int(), "bound ^");
            check_eq((int64_t)14, eng->execute("ba << 1").as_int(), "bound <<");
            check_eq((int64_t)3, eng->execute("ba >> 1").as_int(), "bound >>");
            check_eq((int64_t)-7, eng->execute("-ba").as_int(), "unary - on bound");
            check_eq((int64_t)-8, eng->execute("~ba").as_int(), "unary ~ on bound");
            check_false(eng->execute("!ba").as_bool(), "unary ! on bound");
            check_eq((int64_t)10, eng->execute("ba + bb").as_int(), "bound + bound");
            check_true(eng->execute("bb < ba").as_bool(), "bound < bound");
            check_false(eng->execute("ba == bb").as_bool(), "bound == bound");
            eng->execute("ba = ba + 50;");   // the exact pinned shape
            check_eq((int64_t)57, ba, "counter = counter + 50 writes through");
        });

        test("pin_bound_float_operator_reads", [this]() {
            auto eng = jai::foundry::make_engine();
            double bf = 2.5;
            script_int bi = 7;
            eng->add_global_ref("bf", bf);
            eng->add_global_ref("bi", bi);
            check_eq(3.0, eng->execute("bf + 0.5").as_float(), "bound float +");
            check_eq(3.0, eng->execute("0.5 + bf").as_float(), "bound float + (rhs)");
            check_eq(2.0, eng->execute("bf - 0.5").as_float(), "bound float -");
            check_eq(5.0, eng->execute("bf * 2.0").as_float(), "bound float *");
            check_eq(5.0, eng->execute("bf / 0.5").as_float(), "bound float /");
            check_eq(2.0, eng->execute("5.0 / bf").as_float(), "bound float / (rhs)");
            check_true(eng->execute("bf < 3.0").as_bool(), "bound float <");
            check_true(eng->execute("bf <= 2.5").as_bool(), "bound float <=");
            check_true(eng->execute("bf > 2.0").as_bool(), "bound float >");
            check_false(eng->execute("bf >= 2.6").as_bool(), "bound float >=");
            check_true(eng->execute("bf == 2.5").as_bool(), "bound float ==");
            check_false(eng->execute("bf != 2.5").as_bool(), "bound float !=");
            check_eq((int64_t)-1, eng->execute("bf <=> 3.0").as_int(), "bound float <=>");
            check_eq(-2.5, eng->execute("-bf").as_float(), "unary - on bound float");
            check_false(eng->execute("!bf").as_bool(), "unary ! on bound float");
            check_eq(5.0, eng->execute("bf * 2").as_float(), "bound float * int");
            check_eq(9.5, eng->execute("bi + bf").as_float(), "bound int + bound float");
        });

        test("pin_bound_in_if_eq_tostring_callarg_mapkey", [this]() {
            auto eng = jai::foundry::make_engine();
            jai::stdlib::register_all(*eng);
            int bc = 100;
            double bt = 20.5;
            eng->add_global_ref("bc", bc);
            eng->add_global_ref("bt", bt);
            check_eq((int64_t)1, eng->execute("var r6 = 0; if (bc) { r6 = 1; } r6").as_int(), "bound in if");
            check_true(eng->execute("bc == 100").as_bool(), "bound == int");
            check_true(eng->execute("bt == 20.5").as_bool(), "bound == float");
            check_eq(std::string("100"), eng->execute("to_string(bc)").as_string(), "to_string(bound)");
            eng->add_function("pin_twice", [](script_int n) { return n * 2; });
            check_eq((int64_t)200, eng->execute("pin_twice(bc)").as_int(), "bound as call argument");
            check_eq((int64_t)5, eng->execute("var m6 = {}; m6[bc] = 5; m6[100]").as_int(), "bound int as map key");
        });

        test("pin_same_as_bound_aliases", [this]() {
            auto eng = jai::foundry::make_engine();
            script_int sharedInt = 7;
            eng->add_global_ref("sa", sharedInt);
            eng->add_global_ref("sb", sharedInt);
            check_true(eng->execute("sa.same_as(sb)").as_bool(), "two aliases of one C++ int are same_as");
            check_true(eng->execute("sa.same_as(7)").as_bool(), "bound same_as equal plain literal");
            check_true(eng->execute("var pn7 = 7; sa.same_as(pn7)").as_bool(), "bound same_as equal plain var");
            check_false(eng->execute("sa.same_as(8)").as_bool(), "bound same_as unequal int");
        });

        // Section-12.5 ruling: compound on a bound value stored AS an element/member/field
        // decode-reads the LIVE C++ value, then detach-and-replaces the slot (C++ untouched).
        test("pin_element_compound_detach_and_replace", [this]() {
            auto eng = jai::foundry::make_engine();
            int c8 = 100;
            eng->add_global_ref("c8", c8);
            eng->execute("var arr8 = [c8]; var m8 = {\"k\": c8};");
            c8 = 40;
            eng->execute("arr8[0] += 5;");
            check_eq(40, c8, "array-element compound leaves C++ variable untouched");
            check_eq((int64_t)45, eng->execute("arr8[0]").as_int(), "array-element compound decode-reads live");
            eng->execute("m8[\"k\"] += 2;");
            check_eq(40, c8, "map-value compound leaves C++ variable untouched");
            check_eq((int64_t)42, eng->execute("m8[\"k\"]").as_int(), "map-value compound decode-reads live");
            c8 = 1000;
            check_eq((int64_t)45, eng->execute("arr8[0]").as_int(), "array element detached after compound");
            check_eq((int64_t)42, eng->execute("m8[\"k\"]").as_int(), "map value detached after compound");
        });

        // Assigning a bound value INTO an object member detaches it at store time today
        // (unlike array/map slots, which keep the alias) — the member snapshots the value
        // at assignment; compound then operates on the plain snapshot. Pinned as-is.
        test("pin_member_compound_detach_and_replace", [this]() {
            auto eng = jai::foundry::make_engine();
            int c9 = 100;
            eng->add_global_ref("c9", c9);
            eng->execute(R"(
                class Pin9 { var f = 0; void bump() { f += 5; } }
                var o9 = Pin9(); o9.f = c9;
                var p9 = Pin9(); p9.f = c9;
            )");
            c9 = 40;
            eng->execute("o9.f += 5;");
            check_eq(40, c9, "member compound leaves C++ variable untouched");
            check_eq((int64_t)105, eng->execute("o9.f").as_int(), "member snapshot taken at assign, compound on plain");
            eng->execute("p9.bump();");
            check_eq(40, c9, "implicit-this compound leaves C++ variable untouched");
            check_eq((int64_t)105, eng->execute("p9.f").as_int(), "implicit-this compound on plain snapshot");
            c9 = 7;
            check_eq((int64_t)105, eng->execute("o9.f").as_int(), "member stays detached");
            check_eq((int64_t)105, eng->execute("p9.f").as_int(), "field stays detached");
        });

        test("pin_ref_lvalue_bound_int_index", [this]() {
            auto eng = jai::foundry::make_engine();
            script_int bidx = 2;
            eng->add_global_ref("bidx", bidx);
            check_eq((int64_t)3, eng->execute("var arrB = [1, 2, 3]; auto& rb = arrB[bidx]; rb").as_int(),
                "ref-lvalue indexing reads via a live bound-int index");
            check_eq((int64_t)31, eng->execute("var arrA = [10, 20, 30, 40]; auto& ra = arrA[bidx]; ra = ra + 1; arrA[2]").as_int(),
                "ref-lvalue bound-int index write-through");
        });

        test("pin_truthiness_both_backends", [this]() {
            auto runMatrix = [this](bool useVm) {
                const std::string tag = useVm ? "vm: " : "interp: ";
                auto eng = jai::engine::make();
                if (useVm) { eng->set_backend(jai::backend_type::vm); }
                script_int bz = 0;
                script_int bn = 3;
                double bfz = 0.0;
                double bfn = 1.5;
                t1_opaque_probe probe;
                eng->add_global_ref("bz", bz);
                eng->add_global_ref("bn", bn);
                eng->add_global_ref("bfz", bfz);
                eng->add_global_ref("bfn", bfn);
                eng->add_global("bop", eng->make_value(&probe));
                check_eq((int64_t)1, eng->execute("var r1 = 1; if (bz) { r1 = 2; } r1").as_int(), tag + "zero bound int falsy in if");
                check_true(eng->execute("!bz").as_bool(), tag + "!zero-bound-int");
                check_eq((int64_t)2, eng->execute("var r2 = 1; if (bn) { r2 = 2; } r2").as_int(), tag + "nonzero bound int truthy in if");
                check_false(eng->execute("!bn").as_bool(), tag + "!nonzero-bound-int");
                check_eq((int64_t)1, eng->execute("var r3 = 1; if (bfz) { r3 = 2; } r3").as_int(), tag + "0.0 bound float falsy in if");
                check_true(eng->execute("!bfz").as_bool(), tag + "!zero-bound-float");
                check_eq((int64_t)2, eng->execute("var r4 = 1; if (bfn) { r4 = 2; } r4").as_int(), tag + "nonzero bound float truthy in if");
                check_eq((int64_t)1, eng->execute("var r5 = 1; if (bop) { r5 = 2; } r5").as_int(), tag + "opaque bound falsy in if");
                check_true(eng->execute("!bop").as_bool(), tag + "!opaque-bound");
                check_eq((int64_t)3, eng->execute("var steps = 0; while (bn) { steps = steps + 1; bn = bn - 1; } steps").as_int(), tag + "while on bound int");
                check_eq((int64_t)0, bn, tag + "loop decrement wrote through");
                check_eq((int64_t)1, eng->execute("var r7 = 1; while (bop) { r7 = 2; break; } r7").as_int(), tag + "while on opaque bound never enters");
            };
            runMatrix(false);
            runMatrix(true);
        });

        test("pin_reads_through_refs", [this]() {
            auto eng = jai::foundry::make_engine();
            jai::stdlib::register_all(*eng);
            script_int bg = 100;
            eng->add_global_ref("bg", bg);
            // ref declaration over a bound global
            check_eq((int64_t)101, eng->execute("auto& r12a = bg; r12a + 1").as_int(), "arithmetic through ref decl");
            check_true(eng->execute("auto& r12b = bg; r12b == 100").as_bool(), "comparison through ref decl");
            check_eq((int64_t)1, eng->execute("auto& r12c = bg; var t12 = 0; if (r12c) { t12 = 1; } t12").as_int(), "if through ref decl");
            check_eq(std::string("100"), eng->execute("auto& r12d = bg; to_string(r12d)").as_string(), "to_string through ref decl");
            check_eq((int64_t)-100, eng->execute("auto& r12e = bg; -r12e").as_int(), "unary - through ref decl");
            // by-ref lambda capture of a bound global
            check_eq((int64_t)101, eng->execute("var f12 = [&bg]() -> auto { return bg + 1; }; f12()").as_int(), "arithmetic through by-ref capture");
            check_true(eng->execute("var g12 = [&bg]() -> auto { return bg == 100; }; g12()").as_bool(), "comparison through by-ref capture");
            // by-ref parameter bound to a bound global
            check_eq((int64_t)102, eng->execute("function rd12(int& t) { return t + 2; } rd12(bg)").as_int(), "arithmetic through by-ref param");
            check_true(eng->execute("function rc12(int& t) { return t >= 100; } rc12(bg)").as_bool(), "comparison through by-ref param");
        });

        test("pin_unregistered_class_bound", [this]() {
            auto eng = jai::foundry::make_engine();
            t1_opaque_probe probe;
            probe.v = 5;
            auto val = eng->make_value(&probe);
            check_true(val.is_cpp_bound(), "make_value(T*) on unregistered type is bound");
            check_true(val.is_null(), "opaque bound reads as null (pinned as-is)");
            auto& extracted = val.template as<t1_opaque_probe&>();
            extracted.v = 42;
            check_eq(42, probe.v, "as<T&> extraction aliases the live object");
            check(val.template get_cpp_bound_as<t1_opaque_probe>() == &probe, "get_cpp_bound_as returns the live pointer");
            eng->add_global("bop13", val);
            check_true(eng->execute("bop13 == null").as_bool(), "opaque bound null-comparison (pinned as-is)");
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::cpp_bound_tests)