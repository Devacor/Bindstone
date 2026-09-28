#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/testing/temporary_directory.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class include_import_tests : public suite {
public:
    include_import_tests() : suite("Include/Import Tests") {}
    
    void forge_tests() override {
        // Helper to create a temporary file
        auto create_temp_file = [](const std::string& filename, const std::string& content) {
            std::filesystem::path temp_dir = jai::foundry::test_temp_directory();
            std::filesystem::path file_path = temp_dir / filename;
            std::ofstream file(file_path);
            file << content;
            file.close();
            return file_path.string();
        };
        
        // Helper to clean up temporary file
        auto cleanup_temp_file = [](const std::string& path) {
            std::filesystem::remove(path);
        };
        
        test("basic_include", [=, this]() {
            auto eng = make_engine();
            
            // Create a temporary file to include
            auto math_file = create_temp_file("math_lib.jai", R"(
                function add(int a, int b) -> int {
                    return a + b;
                }
                auto PI = 3.14159;
            )");
            
            // Add temp directory to include paths
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            
            // Include the file
            eng->execute("include \"math_lib.jai\"");
            
            // Verify functions and variables are available
            check_eq(eng->execute("add(2, 3)").as<int>(), 5);
            check_eq(eng->execute("PI").as<double>(), 3.14159);
            
            cleanup_temp_file(math_file);
        });
        
        test("include_with_angle_brackets", [=, this]() {
            auto eng = make_engine();
            
            // Create a temporary file
            auto lib_file = create_temp_file("stdlib.jai", R"(
                function multiply(int a, int b) -> int {
                    return a * b;
                }
            )");
            
            // Add temp directory to include paths
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            
            // Include with angle brackets
            eng->execute("include <stdlib.jai>");
            
            // Verify function is available
            check_eq(eng->execute("multiply(4, 5)").as<int>(), 20);
            
            cleanup_temp_file(lib_file);
        });
        
        test("multiple_includes_execute_multiple_times", [=, this]() {
            auto eng = make_engine();
            
            // Create a file with side effects
            auto counter_file = create_temp_file("counter.jai", R"(
                auto counter = 0;
            )");
            
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            
            // Initialize counter
            eng->execute("include \"counter.jai\"");
            
            // Now create increment file
            cleanup_temp_file(counter_file);
            counter_file = create_temp_file("counter.jai", R"(
                // counter already exists from first include, so we can increment it
                counter = counter + 1;
            )");
            
            // Include multiple times - each should execute
            eng->execute("include \"counter.jai\"");
            check_eq(eng->execute("counter").as<int>(), 1);
            
            eng->execute("include \"counter.jai\"");
            check_eq(eng->execute("counter").as<int>(), 2);
            
            eng->execute("include \"counter.jai\"");
            check_eq(eng->execute("counter").as<int>(), 3);
            
            cleanup_temp_file(counter_file);
        });
        
        test("basic_import_once_behavior", [=, this]() {
            auto eng = make_engine();
            
            // Create a file with side effects
            auto module_file = create_temp_file("module.jai", R"(
                auto module_loaded = 1;
                
                function get_module_count() -> int {
                    return module_loaded;
                }
            )");
            
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            eng->set_import_behavior(engine::import_behavior::once);
            
            // Import multiple times - should only execute once
            eng->execute("import \"module.jai\"");
            check_eq(eng->execute("get_module_count()").as<int>(), 1);
            
            eng->execute("import \"module.jai\"");
            check_eq(eng->execute("get_module_count()").as<int>(), 1);
            
            eng->execute("import \"module.jai\"");
            check_eq(eng->execute("get_module_count()").as<int>(), 1);
            
            cleanup_temp_file(module_file);
        });
        
        test("import_file_timestamp_behavior", [=, this]() {
            auto eng = make_engine();
            
            // Create initial file
            auto test_file = create_temp_file("timestamp_test.jai", R"(
                auto version = 1;
            )");
            
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            eng->set_import_behavior(engine::import_behavior::file_timestamp);
            
            // First import
            eng->execute("import \"timestamp_test.jai\"");
            check_eq(eng->execute("version").as<int>(), 1);
            
            // Import again without modification - should not re-import
            eng->execute("import \"timestamp_test.jai\"");
            check_eq(eng->execute("version").as<int>(), 1);
            
            // Modify the file (with small delay to ensure timestamp changes)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::ofstream file(test_file);
            file << "auto version = 2;";
            file.close();
            
            // Import again - should re-import due to changed timestamp
            eng->execute("import \"timestamp_test.jai\"");
            check_eq(eng->execute("version").as<int>(), 2);
            
            cleanup_temp_file(test_file);
        });
        
        test("import_always_behavior", [=, this]() {
            auto eng = make_engine();
            
            // Create a file with counter
            auto always_file = create_temp_file("always_import.jai", R"(
                auto import_count = 0;
            )");
            
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            eng->set_import_behavior(engine::import_behavior::always);
            
            // Initialize counter
            eng->execute("import \"always_import.jai\"");
            
            // Now create increment file
            cleanup_temp_file(always_file);
            always_file = create_temp_file("always_import.jai", R"(
                import_count = import_count + 1;
            )");
            
            // Import multiple times - should execute every time
            eng->execute("import \"always_import.jai\"");
            check_eq(eng->execute("import_count").as<int>(), 1);
            
            eng->execute("import \"always_import.jai\"");
            check_eq(eng->execute("import_count").as<int>(), 2);
            
            eng->execute("import \"always_import.jai\"");
            check_eq(eng->execute("import_count").as<int>(), 3);
            
            cleanup_temp_file(always_file);
        });
        
        test("include_path_resolution", [=, this]() {
            auto eng = make_engine();
            
            // Create files in different directories
            auto temp_dir = jai::foundry::test_temp_directory();
            auto lib_dir = temp_dir / "lib";
            auto scripts_dir = temp_dir / "scripts";
            
            std::filesystem::create_directories(lib_dir);
            std::filesystem::create_directories(scripts_dir);
            
            // Create same filename in different directories
            std::ofstream lib_file(lib_dir / "utils.jai");
            lib_file << "auto from_lib = true;";
            lib_file.close();
            
            std::ofstream scripts_file(scripts_dir / "utils.jai");
            scripts_file << "auto from_scripts = true;";
            scripts_file.close();
            
            // Test path priority - first added path takes precedence
            eng->add_include_path(lib_dir.string());
            eng->add_include_path(scripts_dir.string());
            
            eng->execute("include \"utils.jai\"");
            check(eng->execute("from_lib").as<bool>());
            check_throws([&]() { eng->execute("from_scripts"); });
            
            // Clean up
            std::filesystem::remove_all(lib_dir);
            std::filesystem::remove_all(scripts_dir);
        });
        
        test("import_tracking_api", [=, this]() {
            auto eng = make_engine();
            
            // Create test files
            auto file1 = create_temp_file("tracked1.jai", "auto file1_loaded = true;");
            auto file2 = create_temp_file("tracked2.jai", "auto file2_loaded = true;");
            
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            eng->set_import_behavior(engine::import_behavior::once);
            
            // Initially no imports
            check(eng->get_imported_files().empty());
            
            // Import first file
            eng->execute("import \"tracked1.jai\"");
            check(eng->is_imported(std::filesystem::canonical(file1).string()));
            check(!eng->is_imported(std::filesystem::canonical(file2).string()));
            check_eq(eng->get_imported_files().size(), 1);
            
            // Import second file
            eng->execute("import \"tracked2.jai\"");
            check(eng->is_imported(std::filesystem::canonical(file1).string()));
            check(eng->is_imported(std::filesystem::canonical(file2).string()));
            check_eq(eng->get_imported_files().size(), 2);
            
            // Reset specific import
            eng->reset_import(std::filesystem::canonical(file1).string());
            check(!eng->is_imported(std::filesystem::canonical(file1).string()));
            check(eng->is_imported(std::filesystem::canonical(file2).string()));
            
            // Reset all imports
            eng->reset_imports();
            check(eng->get_imported_files().empty());
            
            cleanup_temp_file(file1);
            cleanup_temp_file(file2);
        });
        
        test("hash_prefix_ignored", [=, this]() {
            auto eng = make_engine();
            
            // Create a test file
            auto hash_file = create_temp_file("hash_test.jai", R"(
                auto with_hash = 42;
            )");
            
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            
            // Test that # prefix is ignored
            eng->execute("#include \"hash_test.jai\"");
            check_eq(eng->execute("with_hash").as<int>(), 42);
            
            // Also works with import
            eng->execute("#import <hash_test.jai>");
            
            cleanup_temp_file(hash_file);
        });
        
        test("file_not_found_error", [=, this]() {
            auto eng = make_engine();
            
            // Try to include non-existent file
            check_throws([&]() {
                eng->execute("include \"non_existent_file.jai\"");
            });
            
            check_throws([&]() {
                eng->execute("import <another_missing_file.jai>");
            });
        });
        
        test("nested_includes", [=, this]() {
            auto eng = make_engine();
            
            // Create nested include files
            auto inner_file = create_temp_file("inner.jai", R"(
                auto inner_value = 100;
            )");
            
            auto outer_file = create_temp_file("outer.jai", R"(
                include "inner.jai"
                auto outer_value = inner_value + 50;
            )");
            
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            
            // Include outer file which includes inner
            eng->execute("include \"outer.jai\"");
            
            check_eq(eng->execute("inner_value").as<int>(), 100);
            check_eq(eng->execute("outer_value").as<int>(), 150);
            
            cleanup_temp_file(inner_file);
            cleanup_temp_file(outer_file);
        });
        
        test("dynamic_include_with_variable", [=, this]() {
            auto eng = make_engine();
            
            // Create a test file
            auto dynamic_file = create_temp_file("dynamic_content.jai", R"(
                auto dynamic_value = 42;
                function get_dynamic() -> int {
                    return dynamic_value * 2;
                }
            )");
            
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            
            // Set up a variable with the filename
            eng->execute("auto filename = \"dynamic_content.jai\";");
            
            // Include using the variable
            eng->execute("include(filename);");
            
            // Verify the content was included
            check_eq(eng->execute("dynamic_value").as<int>(), 42);
            check_eq(eng->execute("get_dynamic()").as<int>(), 84);
            
            cleanup_temp_file(dynamic_file);
        });
        
        test("include_is_textual_the_file_runs_at_the_include_site", [=, this]() {
            // Dev (2026-09-12): includes are the includer's scope. The included declarations belong to the including unit (a
            // later `var x` in the includer writes the included x in place, and the included function sees it), and an include
            // inside a block or a function body scopes with it (docs/global_shadowing.md)
            auto eng = make_engine();
            jai::stdlib::register_all(*eng);
            auto lib = create_temp_file("textual_lib.jai", R"(
                var x = 3;
                function readx() { return x; }
            )");
            auto blk = create_temp_file("textual_blk.jai", R"(
                var y = 9;
            )");
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            check_eq(std::string("3 5 5"), eng->execute(R"(var x = 1; include "textual_lib.jai"; var after = x; var x = 5; to_string(after) + " " + to_string(x) + " " + to_string(readx());)").as<std::string>());
            check_eq(std::string("9 gone 9 gone"), eng->execute(R"(string r = ""; { include "textual_blk.jai"; r = to_string(y); } try { r = r + " " + to_string(y); } catch (e) { r = r + " gone"; } function f() { include "textual_blk.jai"; return y; } r = r + " " + to_string(f()); try { r = r + " " + to_string(y); } catch (e) { r = r + " gone"; } r;)").as<std::string>());
            cleanup_temp_file(lib);
            cleanup_temp_file(blk);
        });
        test("a_literal_include_inside_a_function_is_spliced_into_the_function", [=, this]() {
            // row 126 (Dev, 2026-09-12: an include inside a function sees its parameters and locals, pay for what you use): a
            // literal include in a function body is parsed into the body (C++), so the checker and both backends see one
            // function; the included text reads and writes the parameter, declares a local the function then uses, defines a
            // function; a nested literal include splices too
            auto eng = make_engine();
            jai::stdlib::register_all(*eng);
            auto local = create_temp_file("splice_local.jai", "x = x + 1; x;");
            auto lib = create_temp_file("splice_lib.jai", "var y = x * 2; function twice(int v) { return v * 2; } include \"splice_inner.jai\";");
            auto inner = create_temp_file("splice_inner.jai", "var z = x + 100;");
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            check_eq((int64_t)6, eng->execute("int run(int x) { include(\"splice_local.jai\"); return x; } run(5);").as_int());
            check_eq((int64_t)6, eng->execute("int run2(int x) { include \"splice_local.jai\"; return x; } run2(5);").as_int());
            check_eq((int64_t)125, eng->execute("int both(int x) { include \"splice_lib.jai\"; return y + twice(x) + z; } both(5);").as_int());   // 10 + 10 + 105
            check_eq((int64_t)7, eng->execute("int blk(int x) { { include \"splice_local.jai\"; } { include \"splice_local.jai\"; } return x; } blk(5);").as_int());   // each block's include writes the parameter
            cleanup_temp_file(local);
            cleanup_temp_file(lib);
            cleanup_temp_file(inner);
        });
        test("a_computed_include_path_inside_a_function_stays_a_runtime_include", [=, this]() {
            // pay for what you use: a computed path cannot be spliced, so it runs in the function's environment and does not
            // see the frame (the checker names the included x undeclared at the include's load)
            auto eng = make_engine();
            jai::stdlib::register_all(*eng);
            auto local = create_temp_file("splice_dyn.jai", "x = x + 1; x;");
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            check_throws([&]() { eng->execute("int run(int x) { var p = \"splice_dyn.jai\"; include(p); return x; } run(5);"); });
            cleanup_temp_file(local);
        });
        test("a_changed_spliced_file_reparses_its_includer", [=, this]() {
            // the parse cache stamps the spliced files: the includer's text is unchanged, the included file is not
            auto eng = make_engine();
            jai::stdlib::register_all(*eng);
            auto local = create_temp_file("splice_reload.jai", "x = x + 1; x;");
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            const std::string dir = jai::foundry::test_temp_directory().string();
            const std::string includer = dir + "/splice_includer.jai";
            const char* text = "int run(int x) { include \"splice_reload.jai\"; return x; } return run(5);";
            check_eq((int64_t)6, eng->execute_file_source(includer, text).as_int());
            local = create_temp_file("splice_reload.jai", "x = x + 1000; x;");   // a different size: the stamp moves even within one clock tick
            check_eq((int64_t)1005, eng->execute_file_source(includer, text).as_int());
            cleanup_temp_file(local);
        });
        test("a_changed_spliced_file_reparses_its_includer_on_every_cache_tier", [=, this]() {
            // Astra's finding of 2026-09-16 on 126.1's cache path: the same engine's execute_file (the file cache's fast path) and a literal
            // execute (the literal cache's fast path) kept the old parse when only the spliced file changed (3, 3, 3 for 3, 11, 119); every tier
            // now revalidates the spliced stamps before a hit runs. Different sizes move the stamp within one clock tick
            auto eng = make_engine();
            jai::stdlib::register_all(*eng);
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            auto dep = create_temp_file("splice_live_dep.jai", "return 3;");
            auto parent = create_temp_file("splice_live_parent.jai", "int probe() { include \"splice_live_dep.jai\"; return 7; } return probe();");
            check_eq((int64_t)3, eng->execute_file(parent).as_int());
            dep = create_temp_file("splice_live_dep.jai", "return 11;");
            check_eq((int64_t)11, eng->execute_file(parent).as_int());
            dep = create_temp_file("splice_live_dep.jai", "return 119;");
            check_eq((int64_t)119, eng->execute_file(parent).as_int());
            dep = create_temp_file("splice_live_dep.jai", "return 5;");
            check_eq((int64_t)5, eng->execute("int lit() { include \"splice_live_dep.jai\"; return 7; } return lit();").as_int());
            dep = create_temp_file("splice_live_dep.jai", "return 55;");
            check_eq((int64_t)55, eng->execute("int lit() { include \"splice_live_dep.jai\"; return 7; } return lit();").as_int());
            cleanup_temp_file(dep);
            cleanup_temp_file(parent);
        });
        test("a_textual_include_shared_by_two_units_resolves_each_units_cells", [=, this]() {
            // row 123.5 (Astra, 2026-09-12 19:32): an included file's compiled program and the functions it defines belong to
            // the including unit; two units including the same file each get their own, so the VM's lookup caches never serve
            // another unit's binding (docs/global_shadowing.md)
            auto eng = make_engine();
            jai::stdlib::register_all(*eng);
            auto reader = create_temp_file("unit_reader.jai", "x;");
            auto writer = create_temp_file("unit_writer.jai", "x = x + 1; x;");
            auto fnlib = create_temp_file("unit_fnlib.jai", "function getx() { return x; }");
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            const std::string dir = jai::foundry::test_temp_directory().string();
            const std::string unitA = dir + "/unit_A.jai", unitB = dir + "/unit_B.jai";   // a file unit is its path: the content is a reload
            eng->execute_file_source(unitA, "var x = 11;");
            eng->execute_file_source(unitB, "var x = 22;");
            std::string out;
            for (int k = 0; k < 2; ++k) {
                out += eng->execute_file_source(unitA, "return include(\"unit_reader.jai\");").to_string() + " ";
                out += eng->execute_file_source(unitB, "return include(\"unit_reader.jai\");").to_string() + " ";
            }
            check_eq(std::string("11 22 11 22 "), out);
            out.clear();
            for (int k = 0; k < 2; ++k) {
                out += eng->execute_file_source(unitA, "return include(\"unit_writer.jai\");").to_string() + " ";
                out += eng->execute_file_source(unitB, "return include(\"unit_writer.jai\");").to_string() + " ";
            }
            check_eq(std::string("12 23 13 24 "), out);
            out.clear();
            for (int k = 0; k < 2; ++k) {
                out += eng->execute_file_source(unitA, "include \"unit_fnlib.jai\"; return getx();").to_string() + " ";
                out += eng->execute_file_source(unitB, "include \"unit_fnlib.jai\"; return getx();").to_string() + " ";
            }
            check_eq(std::string("13 24 13 24 "), out);
            cleanup_temp_file(reader);
            cleanup_temp_file(writer);
            cleanup_temp_file(fnlib);
        });
        test("dynamic_import_with_expression", [=, this]() {
            auto eng = make_engine();
            
            // Create test files
            auto moduleA = create_temp_file("moduleA.jai", R"(
                auto module_letter = "A";
            )");
            
            auto moduleB = create_temp_file("moduleB.jai", R"(
                auto module_letter = "B";
            )");
            
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            eng->set_import_behavior(engine::import_behavior::once);
            
            // Import using string concatenation
            eng->execute("auto module_name = \"module\" + \"A\" + \".jai\";");
            eng->execute("import(module_name);");
            check_eq(eng->execute("module_letter").as<std::string>(), "A");
            
            // Try to import module B
            eng->execute("module_name = \"module\" + \"B\" + \".jai\";");
            eng->execute("import(module_name);");
            
            // Now we should have module B imported too
            eng->execute("auto letterB = \"B\";");
            eng->execute("import(\"module\" + letterB + \".jai\");");
            
            // Since we're using 'once' behavior, importing again should not change anything. The same reader text
            // executed again resolves as of this run, so it sees module B's declaration (docs/global_shadowing.md)
            check_eq(eng->execute("module_letter").as<std::string>(), "B");

            cleanup_temp_file(moduleA);
            cleanup_temp_file(moduleB);
        });

        // === include in expression position (Dev ruling 2026-07: include returns its value) ===
        // `var cfg = include "file.jai";` evaluates to the included file's result — exactly
        // what engine::execute returns for that source. Statement-position include is
        // unchanged (value discarded). include always RUNS the file (no cache), so its
        // value is always fresh — no interaction with import's once-cache.
        test("include_expression_returns_value", [=, this]() {
            auto config_file = create_temp_file("expr_config.jai", R"(
                { "width": 800, "height": 600 };
            )");
            auto expr_file = create_temp_file("expr_calc.jai", R"(
                var acc = 0;
                for (int i = 1; i <= 4; i++) { acc += i; }
                acc * 10;
            )");
            for (bool use_vm : {false, true}) {
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                eng->add_include_path(jai::foundry::test_temp_directory().string());
                auto r = eng->execute(R"(
                    var cfg = include "expr_config.jai";
                    auto calc = include "expr_calc.jai";
                    cfg["width"] + cfg["height"] + calc;
                )");
                check_eq((int64_t)1500, r.as_int(), use_vm ? "vm include value" : "interp include value");
            }
            cleanup_temp_file(config_file);
            cleanup_temp_file(expr_file);
        });

        test("include_expression_dynamic_path_and_nesting", [=, this]() {
            auto leaf = create_temp_file("expr_leaf.jai", "21;");
            for (bool use_vm : {false, true}) {
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                eng->add_include_path(jai::foundry::test_temp_directory().string());
                // computed path form in expression position + in-call use
                auto r = eng->execute(R"(
                    function twice(v) { return v * 2; }
                    var part = "leaf";
                    twice(include("expr_" + part + ".jai"));
                )");
                check_eq((int64_t)42, r.as_int(), use_vm ? "vm dynamic include" : "interp dynamic include");
            }
            cleanup_temp_file(leaf);
        });

        test("include_statement_position_unchanged", [=, this]() {
            // Statement include still runs for side effects and DISCARDS its value; it
            // also always re-runs (fresh evaluation each time, unlike import)
            auto counter_file = create_temp_file("expr_counter.jai", R"(
                bump_count();
                777;
            )");
            for (bool use_vm : {false, true}) {
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                int bumps = 0;
                eng->add_function("bump_count", [&bumps]() { bumps++; });
                eng->add_include_path(jai::foundry::test_temp_directory().string());
                auto r = eng->execute(R"(
                    include "expr_counter.jai";
                    include "expr_counter.jai";
                    var v = include "expr_counter.jai";
                    v;
                )");
                check_eq((int64_t)777, r.as_int(), "expression include still returns");
                check_eq(3, bumps, use_vm ? "vm include always runs" : "interp include always runs");
            }
            cleanup_temp_file(counter_file);
        });

        // === jaibite disk cache (Dev ruling 2026-07): every file-based load maintains a
        // sibling <stem>.jaibite — strictly-newer mtime + matching registration
        // fingerprint loads instead of parsing; anything else reparses and rewrites.
        // Deterministic mtime orderings via the std::filesystem::last_write_time SETTER.

        auto sibling_of = [](const std::string& src) {
            return std::filesystem::path(src).replace_extension(".jaibite").string();
        };
        auto starts_with_jbit = [](const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            char magic[4] = {};
            f.read(magic, 4);
            return f.gcount() == 4 && magic[0] == 'J' && magic[1] == 'B' && magic[2] == 'I' && magic[3] == 'T';
        };
        auto force_older_than = [](const std::string& target, const std::string& reference) {
            auto ref = std::filesystem::last_write_time(reference);
            std::filesystem::last_write_time(target, ref - std::chrono::hours(1));
        };

        test("jaibite_cache_creates_sibling_on_file_loads", [=, this]() {
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_make.jai", "auto cache_make_v = 11;");
                std::filesystem::remove(sibling_of(src));
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                eng->add_include_path(jai::foundry::test_temp_directory().string());
                eng->execute("include \"cache_make.jai\"");
                check(std::filesystem::exists(sibling_of(src)), "include writes the sibling");
                check(starts_with_jbit(sibling_of(src)), "sibling is a jaibite (JBIT magic)");

                std::filesystem::remove(sibling_of(src));
                eng->execute_file(src);
                check(std::filesystem::exists(sibling_of(src)), "execute_file writes the sibling");

                std::filesystem::remove(sibling_of(src));
                eng->execute("import \"cache_make.jai\"");
                check(std::filesystem::exists(sibling_of(src)), "import writes the sibling");

                cleanup_temp_file(src);
                std::filesystem::remove(sibling_of(src));
            }
        });

        test("jaibite_cache_fresh_sibling_skips_parse", [=, this]() {
            // Proof the parse is skipped: a fresh (strictly newer, fingerprint-matching)
            // sibling loads fine even when the .jai itself no longer parses.
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_skip.jai", "42;");
                std::filesystem::remove(sibling_of(src));
                {
                    auto writer = engine::make();
                    if (use_vm) { writer->set_backend(jai::backend_type::vm); }
                    writer->add_include_path(jai::foundry::test_temp_directory().string());
                    check_eq((int64_t)42, writer->execute("var r = include \"cache_skip.jai\"; r;").as_int());
                }
                {
                    std::ofstream f(src, std::ios::trunc);
                    f << "%%% not a script %%%";
                }
                force_older_than(src, sibling_of(src));
                auto reader = engine::make();
                if (use_vm) { reader->set_backend(jai::backend_type::vm); }
                reader->add_include_path(jai::foundry::test_temp_directory().string());
                check_eq((int64_t)42, reader->execute("var r = include \"cache_skip.jai\"; r;").as_int(),
                         use_vm ? "vm: cache hit skipped the parse" : "interp: cache hit skipped the parse");
                cleanup_temp_file(src);
                std::filesystem::remove(sibling_of(src));
            }
        });

        test("jaibite_cache_loaded_ast_keeps_file_attribution", [=, this]() {
            // Composition pin with the debugger's path stamping: an AST loaded from the
            // sibling (parse provably skipped — the source is unparseable) still carries
            // the .jai filename in its source_locations, so stack traces name the file.
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_attr.jai", "throw \"attr\";");
                std::filesystem::remove(sibling_of(src));
                {
                    auto writer = engine::make();
                    if (use_vm) { writer->set_backend(jai::backend_type::vm); }
                    try { writer->execute_file(src); } catch (...) {}
                }
                {
                    std::ofstream f(src, std::ios::trunc);
                    f << "%%%";
                }
                force_older_than(src, sibling_of(src));
                auto reader = engine::make();
                if (use_vm) { reader->set_backend(jai::backend_type::vm); }
                try { reader->execute_file(src); } catch (...) {}
                auto trace = reader->last_stack_trace();
                check(!trace.empty(), "trace captured from cache-loaded AST");
                check_eq(src, trace.back().file,
                         use_vm ? "vm: cache-loaded AST names the .jai" : "interp: cache-loaded AST names the .jai");
                cleanup_temp_file(src);
                std::filesystem::remove(sibling_of(src));
            }
        });

        test("jaibite_cache_edit_goes_stale_equal_mtime_included", [=, this]() {
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_edit.jai", "1;");
                std::filesystem::remove(sibling_of(src));
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                eng->add_include_path(jai::foundry::test_temp_directory().string());
                check_eq((int64_t)1, eng->execute("var a = include \"cache_edit.jai\"; a;").as_int());
                // edit lands on the very next include, same engine run; equal mtimes
                // must read as stale (pin: strictly-newer freshness rule)
                {
                    std::ofstream f(src, std::ios::trunc);
                    f << "2;";
                }
                std::filesystem::last_write_time(src, std::filesystem::last_write_time(sibling_of(src)));
                check_eq((int64_t)2, eng->execute("var b = include \"cache_edit.jai\"; b;").as_int(),
                         "equal mtimes = stale -> reparse");
                cleanup_temp_file(src);
                std::filesystem::remove(sibling_of(src));
            }
        });

        test("jaibite_cache_corrupt_sibling_falls_back_and_rewrites", [=, this]() {
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_corrupt.jai", "7;");
                {
                    std::ofstream f(sibling_of(src), std::ios::binary | std::ios::trunc);
                    f << "JBITgarbage-not-a-real-bite";
                }
                force_older_than(src, sibling_of(src));   // corrupt sibling looks "fresh"
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                eng->add_include_path(jai::foundry::test_temp_directory().string());
                check_eq((int64_t)7, eng->execute("var c = include \"cache_corrupt.jai\"; c;").as_int(),
                         "corrupt sibling -> silent reparse");
                check(starts_with_jbit(sibling_of(src)), "sibling rewritten valid");
                // the rewritten sibling really loads: clobber the source again
                {
                    std::ofstream f(src, std::ios::trunc);
                    f << "%%%";
                }
                force_older_than(src, sibling_of(src));
                auto eng2 = engine::make();
                if (use_vm) { eng2->set_backend(jai::backend_type::vm); }
                eng2->add_include_path(jai::foundry::test_temp_directory().string());
                check_eq((int64_t)7, eng2->execute("var d = include \"cache_corrupt.jai\"; d;").as_int());
                cleanup_temp_file(src);
                std::filesystem::remove(sibling_of(src));
            }
        });

        test("jaibite_cache_fingerprint_mismatch_reparses", [=, this]() {
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_fp.jai", "100;");
                std::filesystem::remove(sibling_of(src));
                {   // engine A stamps its (extra-function) fingerprint into the sibling
                    auto a = engine::make();
                    if (use_vm) { a->set_backend(jai::backend_type::vm); }
                    a->add_function("fp_extra", [](script_int v) -> script_int { return v; });
                    a->add_include_path(jai::foundry::test_temp_directory().string());
                    check_eq((int64_t)100, a->execute("var r = include \"cache_fp.jai\"; r;").as_int());
                }
                // source now says 200, forced older than the sibling (mtime says "fresh")
                {
                    std::ofstream f(src, std::ios::trunc);
                    f << "200;";
                }
                force_older_than(src, sibling_of(src));
                {   // engine B: different registration surface -> stale -> parses the source
                    auto b = engine::make();
                    if (use_vm) { b->set_backend(jai::backend_type::vm); }
                    b->add_include_path(jai::foundry::test_temp_directory().string());
                    check_eq((int64_t)200, b->execute("var r = include \"cache_fp.jai\"; r;").as_int(),
                             "fingerprint mismatch -> reparse");
                }
                // B's reparse rewrote the sibling under B's (bare) surface; an engine
                // with the same bare surface now loads it (fingerprint matches)
                force_older_than(src, sibling_of(src));
                {
                    auto c = engine::make();
                    if (use_vm) { c->set_backend(jai::backend_type::vm); }
                    c->add_include_path(jai::foundry::test_temp_directory().string());
                    check_eq((int64_t)200, c->execute("var r = include \"cache_fp.jai\"; r;").as_int(),
                             "matching fingerprint -> sibling loads");
                }
                cleanup_temp_file(src);
                std::filesystem::remove(sibling_of(src));
            }
        });

        test("jaibite_cache_version_mismatch_reparses_and_rewrites", [=, this]() {
            // Format-version stamp (Dev ruling 2026-07-11): a sibling from an older
            // parser/format silently reparses + rewrites — stale parse-time semantics
            // (e.g. slot assignment changes) must never load
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_ver.jai", "42;");
                {   // valid magic, WRONG version, then padding past the header minimum
                    std::ofstream f(sibling_of(src), std::ios::binary | std::ios::trunc);
                    const uint8_t magic[4] = { 'J', 'B', 'I', 'T' };
                    const uint32_t bad_version = 0x7FFFFFFF;
                    const uint32_t flags = 0;
                    const uint64_t fingerprint = 0;
                    f.write(reinterpret_cast<const char*>(magic), 4);
                    f.write(reinterpret_cast<const char*>(&bad_version), 4);
                    f.write(reinterpret_cast<const char*>(&flags), 4);
                    f.write(reinterpret_cast<const char*>(&fingerprint), 8);
                }
                force_older_than(src, sibling_of(src));   // stale-version sibling looks "fresh"
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                eng->add_include_path(jai::foundry::test_temp_directory().string());
                check_eq((int64_t)42, eng->execute("var v = include \"cache_ver.jai\"; v;").as_int(),
                         "version mismatch -> silent reparse");
                check(starts_with_jbit(sibling_of(src)), "sibling rewritten");
                // the rewrite carries the CURRENT version: clobber the source and reload
                {
                    std::ofstream f(src, std::ios::trunc);
                    f << "%%%";
                }
                force_older_than(src, sibling_of(src));
                auto eng2 = engine::make();
                if (use_vm) { eng2->set_backend(jai::backend_type::vm); }
                eng2->add_include_path(jai::foundry::test_temp_directory().string());
                check_eq((int64_t)42, eng2->execute("var w = include \"cache_ver.jai\"; w;").as_int(),
                         "rewritten sibling loads under the current version");
                cleanup_temp_file(src);
                std::filesystem::remove(sibling_of(src));
            }
        });

        test("jaibite_cache_disable_switch", [=, this]() {
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_off.jai", "5;");
                std::filesystem::remove(sibling_of(src));
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                check(eng->jaibite_cache(), "cache defaults ON");
                eng->jaibite_cache(false);
                check(!eng->jaibite_cache(), "setter/getter");
                eng->add_include_path(jai::foundry::test_temp_directory().string());
                check_eq((int64_t)5, eng->execute("var r = include \"cache_off.jai\"; r;").as_int());
                check(!std::filesystem::exists(sibling_of(src)), "disabled -> no sibling written");
                cleanup_temp_file(src);
            }
        });

        test("jaibite_cache_write_failure_is_silent_and_counted", [=, this]() {
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_ro.jai", "9;");
                std::filesystem::remove(sibling_of(src));
                std::filesystem::create_directory(sibling_of(src));   // sibling path unwritable
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                eng->add_include_path(jai::foundry::test_temp_directory().string());
                check_eq((int64_t)9, eng->execute("var r = include \"cache_ro.jai\"; r;").as_int(),
                         "unwritable sibling never surfaces as an error");
                check(eng->jaibite_cache_write_failures() >= 1, "failure counted for diagnostics");
                std::filesystem::remove(sibling_of(src));
                cleanup_temp_file(src);
            }
        });

        test("import_is_a_statement_that_takes_no_scope", [=, this]() {
            // Dev's ruling (2026-09-27): an import may stand wherever a statement may (a function, a block, a loop, a lambda) and
            // always runs at program scope; directly in a class or a namespace body it is a parse error naming the rule
            auto lib = create_temp_file("lib_anywhere.jai", "int lib_fn() { return 11; }\n");
            const char* nested[] = {
                "int f() { import \"lib_anywhere.jai\"; return lib_fn(); } f();",
                "if (true) { import \"lib_anywhere.jai\"; } lib_fn();",
                "if (true) import \"lib_anywhere.jai\"; lib_fn();",
                "var h = function() { import(\"lib_anywhere.jai\"); return lib_fn(); }; h();",
            };
            const char* members[] = {
                "class C { import \"lib_anywhere.jai\"; int v = 0; } 4;",
                "namespace ns { import \"lib_anywhere.jai\"; int g() { return 2; } } ns::g();",
            };
            for (bool use_vm : {false, true}) {
                for (const char* src : nested) {
                    auto eng = engine::make();
                    if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                    eng->add_include_path(jai::foundry::test_temp_directory().string());
                    check_eq((int64_t)11, eng->execute(src).as_int(), src);
                }
                for (const char* src : members) {
                    auto eng = engine::make();
                    if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                    std::string text;
                    try { (void)eng->execute(src); } catch (const std::exception& ex) { text = ex.what(); }
                    check_true(text.find("import is a statement, not a member: it is not allowed directly in a class or a namespace body") != std::string::npos, std::string("rejected with the rule: ") + src + " -> " + text);
                }
            }
            cleanup_temp_file(lib);
        });

        test("an_import_anywhere_runs_its_module_at_program_scope_once_per_change", [=, this]() {
            // wherever it is reached: the first reach runs the module as its own unit, a reach with the file's (mtime, size)
            // stamp unchanged costs the stamp check only, a touched file re-runs in place (functions and classes replace, live
            // instances migrate, initializers re-run); a re-run checks the module before any of it runs, and a failure is
            // catchable and leaves the program as it was
            const auto dir = jai::foundry::test_temp_directory();
            int tick = 0;
            auto write_module = [&](const std::string& name, const std::string& body) {
                const auto path = dir / name;
                { std::ofstream(path, std::ios::binary | std::ios::trunc) << body; }
                std::error_code ec;
                std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2 * ++tick), ec);
            };
            for (bool proofs : {true, false}) {
                auto eng = make_engine();
                jai::stdlib::register_all(*eng);
                eng->type_proofs(proofs);
                eng->add_include_path(dir.string());
                int runs = 0;
                eng->add_function("module_ran", [&runs]() { ++runs; });
                write_module("ia_mod.jai", "module_ran();\nint ia_value() { return 7; }\nint IA_LIMIT = 3;\n");
                eng->execute("void load() { import \"ia_mod.jai\"; }\nload();\nload();");
                check_eq(1, runs, "the first reach runs the module; an unchanged reach does not");
                check_eq((int64_t)7, eng->execute("ia_value();").as_int(), "its declarations are at program scope");
                check_eq((int64_t)3, eng->execute("IA_LIMIT;").as_int());
                eng->execute("for (int i = 0; i < 5; i++) { if (i % 2 == 0) { import \"ia_mod.jai\"; } }");
                check_eq(1, runs, "an if in a loop reaches it three times, the stamp holds");
                write_module("ia_mod.jai", "module_ran();\nint ia_value() { return 8; }\nint IA_LIMIT = 9;\n");
                eng->execute("for (int i = 0; i < 3; i++) { load(); }");
                check_eq(2, runs, "a touched module re-runs once");
                check_eq((int64_t)8, eng->execute("ia_value();").as_int(), "its function replaced in place");
                check_eq((int64_t)9, eng->execute("IA_LIMIT;").as_int(), "its initializer re-ran");
                write_module("ia_co.jai", "int ia_co_value() { return 21; }\n");
                check_eq((int64_t)21, eng->execute("coroutine int ia_gen() { import \"ia_co.jai\"; yield ia_co_value(); }\nint got = 0;\nfor (auto v : ia_gen()) { got = v; }\ngot;").as_int(), "from a coroutine");
                write_module("ia_lam.jai", "int ia_lam_value() { return 5; }\n");
                check_eq((int64_t)5, eng->execute("var ia_lam = []() { import \"ia_lam.jai\"; return ia_lam_value(); };\nia_lam();").as_int(), "from a lambda");
                write_module("ia_cls.jai", "class IaBox { int v = 1; int get() { return v; } }\n");
                eng->execute("void load_cls() { import \"ia_cls.jai\"; }\nload_cls();\nIaBox box = IaBox();\nbox.v = 4;");
                write_module("ia_cls.jai", "class IaBox { int v = 1; int extra = 5; int get() { return v * 100 + extra; } }\n");
                check_eq((int64_t)405, eng->execute("load_cls();\nbox.get();").as_int(), "the live instance migrated: its field kept, the new field and method arrived");
                write_module("ia_mod.jai", "module_ran();\nint ia_value() { return \"text\"; }\n");
                const std::string checked = eng->execute("var m = \"\";\ntry { load(); } catch (e) { m = e; }\nm;").to_string();
                check_true(checked.rfind("static check failed:\n", 0) == 0 && checked.find("ia_mod.jai:2:") != std::string::npos, checked);
                check_eq(2, runs, "none of the failing module ran");
                check_eq((int64_t)8, eng->execute("ia_value();").as_int(), "the program is unchanged");
                write_module("ia_mod.jai", "module_ran();\nint ia_value() { return 10; }\nthrow \"module failed\";\n");
                check_eq(std::string("module failed"), eng->execute("var m2 = \"\";\ntry { load(); } catch (e) { m2 = e; }\nm2;").to_string(), "a runtime error in the run is catchable");
                check_eq(3, runs);
                write_module("ia_mod.jai", "module_ran();\nint ia_value() { return 11; }\n");
                eng->disable(jai::filesystem);
                check_eq(std::string("Insufficient privilege: filesystem."), eng->execute("var m3 = \"\";\ntry { load(); } catch (e) { m3 = e; }\nm3;").to_string());
                eng->enable(jai::filesystem);
                eng->execute("load();");
                check_eq(4, runs);
                const std::string walled = eng->execute("array<int> xs = [1, 2, 3];\nvar m4 = \"\";\ntry { parallel_for (auto& x : xs) { import \"ia_mod.jai\"; x = x + 1; } } catch (e) { m4 = e; }\nm4;").to_string();
                check_true(walled.find("include/import is not allowed in a parallel body") != std::string::npos, walled);
                // the dev-reload idiom: a module list included from the loop's function; only a touched module re-runs, and a
                // touched module that fails its check leaves everything as it was
                int ran_a = 0, ran_b = 0;
                eng->add_function("ran_a", [&ran_a]() { ++ran_a; });
                eng->add_function("ran_b", [&ran_b]() { ++ran_b; });
                write_module("ia_list_a.jai", "ran_a();\nint ia_a() { return 1; }\n");
                write_module("ia_list_b.jai", "ran_b();\nint ia_b() { return 2; }\n");
                write_module("ia_list.jai", "import \"ia_list_a.jai\";\nimport \"ia_list_b.jai\";\n");
                eng->execute("void tick() { include \"ia_list.jai\"; }\nfor (int i = 0; i < 4; i++) { tick(); }");
                check_eq(std::string("1 1"), std::to_string(ran_a) + " " + std::to_string(ran_b));
                write_module("ia_list_a.jai", "ran_a();\nint ia_a() { return 10; }\n");
                eng->execute("for (int i = 0; i < 4; i++) { tick(); }");
                check_eq(std::string("2 1"), std::to_string(ran_a) + " " + std::to_string(ran_b), "only the touched module re-ran");
                check_eq((int64_t)12, eng->execute("ia_a() + ia_b();").as_int());
                write_module("ia_list_a.jai", "ran_a();\nint ia_a() { return 1.5 + \"x\"; }\nint ia_a2() { return 3; }\n");
                const std::string list_fail = eng->execute("var lf = \"\";\nfor (int i = 0; i < 2; i++) { try { tick(); } catch (e) { lf = e; } }\nlf;").to_string();
                check_true(list_fail.find("ia_list_a.jai:2:") != std::string::npos, list_fail);
                check_eq(std::string("2 1"), std::to_string(ran_a) + " " + std::to_string(ran_b), "the failing module ran nothing");
                check_eq((int64_t)12, eng->execute("ia_a() + ia_b();").as_int(), "the program kept the code it had");
                // the checker gathers a nested literal import as a unit of the program
                write_module("ia_seven.jai", "int ia_seven() { return 7; }\n");
                auto strict = make_engine();
                strict->type_proofs(proofs);
                strict->add_include_path(dir.string());
                check_eq((int64_t)7, strict->execute("void load_seven() { import \"ia_seven.jai\"; }\nload_seven();\nia_seven();").as_int());
            }
            for (const char* f : {"ia_mod.jai", "ia_co.jai", "ia_lam.jai", "ia_cls.jai", "ia_seven.jai", "ia_list.jai", "ia_list_a.jai", "ia_list_b.jai"}) { cleanup_temp_file((dir / f).string()); }
        });

        test("the_ladder_answers_exactly_while_it_remembers_its_answers", [=, this]() {
            // resolution is remembered per asking folder, and a file appearing on an earlier rung or the answer disappearing
            // is seen at once
            const auto base = jai::foundry::test_temp_directory() / "ladder_memo";
            const auto asking = base / "asking";
            const auto lib = base / "lib";
            std::filesystem::create_directories(asking);
            std::filesystem::create_directories(lib);
            { std::ofstream(lib / "lm.jai") << "1;"; }
            std::filesystem::remove(asking / "lm.jai");
            for (bool use_vm : {false, true}) {
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                eng->add_include_path(lib.string());
                const std::string root = (asking / "root.jai").string();
                auto ask = [&]() { return eng->execute_file_source(root, "resolve_path(\"lm.jai\");").to_string(); };
                const std::string from_lib = ask();
                check_true(from_lib.find("ladder_memo/lib/lm.jai") != std::string::npos, from_lib);
                check_eq(from_lib, ask(), "remembered");
                { std::ofstream(asking / "lm.jai") << "2;"; }
                const std::string sibling = ask();
                check_true(sibling.find("ladder_memo/asking/lm.jai") != std::string::npos, "an earlier rung's new file wins: " + sibling);
                std::filesystem::remove(asking / "lm.jai");
                check_eq(from_lib, ask(), "the vanished answer falls back down the ladder");
                std::filesystem::remove(lib / "lm.jai");
                check_eq(std::string(), ask(), "no rung holds it");
                { std::ofstream(lib / "lm.jai") << "1;"; }
            }
            std::filesystem::remove_all(base);
        });

        test("an_import_says_whether_its_module_ran", [=, this]() {
            // import(...) is an expression: true when a module (re)ran, false when it had nothing to do; the statement form
            // yields nothing; a group is true when any of its modules ran
            const auto dir = jai::foundry::test_temp_directory();
            int tick = 300;
            auto write_module = [&](const std::string& name, const std::string& body) {
                const auto path = dir / name;
                { std::ofstream(path, std::ios::binary | std::ios::trunc) << body; }
                std::error_code ec;
                std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2 * ++tick), ec);
            };
            for (bool use_vm : {false, true}) {
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                jai::stdlib::register_all(*eng);
                eng->import_recheck_interval(0);
                eng->add_include_path(dir.string());
                write_module("ir_a.jai", "int IR_A = 1;\n");
                write_module("ir_b.jai", "int IR_B = 1;\n");
                check_true(eng->execute("import(\"ir_a.jai\");").as_bool(), "the first reach runs the module");
                check_false(eng->execute("import(\"ir_a.jai\");").as_bool(), "an unchanged module does not run");
                write_module("ir_a.jai", "int IR_A = 2;\n");
                check_eq(std::string("true|2"), eng->execute("bool ran = import(\"ir_a.jai\");\nto_string(ran) + \"|\" + to_string(IR_A);").to_string());
                check_eq(std::string("true|false"), eng->execute("bool g1 = import(\"ir_a.jai\", \"ir_b.jai\");\nbool g2 = import([\"ir_a.jai\", \"ir_b.jai\"]);\nto_string(g1) + \"|\" + to_string(g2);").to_string(),
                         "a group is true when any module ran (ir_b's first run), then false");
                check_eq(std::string("skip"), eng->execute("string r = \"skip\";\nif (import(\"ir_b.jai\")) { r = \"ran\"; }\nr;").to_string());
                std::string bad;
                try { eng->execute("var p = 5;\nimport(p);"); } catch (const std::exception& ex) { bad = ex.what(); }
                check_true(bad.find("import(...) takes paths: strings or arrays of strings") != std::string::npos, bad);
                std::string unknown;
                try { eng->execute("import(\"ir_a.jai\", jai::forse);"); } catch (const std::exception& ex) { unknown = ex.what(); }
                check_true(unknown.find("Unknown import directive 'jai::forse' (the directive is jai::force)") != std::string::npos, unknown);
            }
            for (const char* f : {"ir_a.jai", "ir_b.jai"}) { cleanup_temp_file((dir / f).string()); }
        });

        test("one_check_pass_covers_every_import_a_frame_reaches", [=, this]() {
            // the engine-wide pass: the first import reached after the window opens a pass, every import reached in it stats its
            // modules once, the pass closes when a site reaches a path it already checked (the frame loop came around) or at the
            // next top-level execute, and between passes an import does no file work - a frame checks every import or none
            const auto dir = jai::foundry::test_temp_directory();
            int tick = 500;
            auto write_module = [&](const std::string& name, const std::string& body) {
                const auto path = dir / name;
                { std::ofstream(path, std::ios::binary | std::ios::trunc) << body; }
                std::error_code ec;
                std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2 * ++tick), ec);
            };
            for (bool use_vm : {false, true}) {
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                jai::stdlib::register_all(*eng);
                check_near(0.2, eng->import_recheck_interval(), 1e-12);
                eng->add_include_path(dir.string());
                std::map<std::string, int> runs;
                eng->add_function("pass_ran", [&runs](std::string m) { ++runs[m]; });
                eng->import_recheck_interval(1.0e6);   // nothing expires by itself: every pass below is opened on purpose
                write_module("pp_one.jai", "pass_ran(\"one\");\n");
                write_module("pp_two.jai", "pass_ran(\"two\");\n");
                write_module("pp_nest.jai", "import \"pp_one.jai\";\npass_ran(\"nest\");\n");
                eng->execute("void frame() { import \"pp_one.jai\"; import \"pp_nest.jai\"; import \"pp_two.jai\"; }\nframe();");
                check_true(runs["one"] == 1 && runs["two"] == 1 && runs["nest"] == 1, "the first frame loads every module");
                const uint64_t warm = eng->import_stat_count();
                eng->execute("for (int i = 0; i < 100; i++) { frame(); }");
                check_eq(warm, eng->import_stat_count(), "between passes an import does no file work (0 queries)");
                write_module("pp_two.jai", "pass_ran(\"two\");\n");
                write_module("pp_nest.jai", "import \"pp_one.jai\";\npass_ran(\"nest\");\n");
                eng->execute("frame();");
                check_eq(1, runs["two"], "a touch waits for the next pass");
                eng->import_recheck_interval(1.0e6);   // the window starts over: the next import reached opens a pass
                const uint64_t before = eng->import_stat_count();
                eng->execute("for (int i = 0; i < 5; i++) { frame(); }");
                check_true(runs["nest"] == 2 && runs["two"] == 2 && runs["one"] == 1, "one frame took both changes (the nested import did not end the pass)");
                check_eq(before + 3, eng->import_stat_count(), "one query per module in the pass, none in the frames after it");
                write_module("pp_one.jai", "pass_ran(\"one\");\n");
                eng->execute("frame();");
                check_eq(1, runs["one"], "still inside the window");
                eng->execute("import(\"pp_one.jai\", jai::force);");
                check_eq(2, runs["one"], "jai::force opens a pass now");
                eng->execute("import(\"pp_one.jai\", jai::force);");
                check_eq(2, runs["one"], "an unchanged module does not run on a forced check");
                // a computed site remembers the paths it ran: a module it never ran loads at once, pass or not
                write_module("pp_late.jai", "pass_ran(\"late\");\n");
                eng->execute("array<string> MODS = [\"pp_one.jai\", \"pp_two.jai\"];\nvoid scan() { for (auto m : MODS) { import(m); } }\nscan();\nMODS.push(\"pp_late.jai\");\nscan();");
                check_eq(1, runs["late"], "a new path in the list loads on its first reach");
                const uint64_t steady = eng->import_stat_count();
                eng->execute("for (int i = 0; i < 50; i++) { scan(); }");
                check_eq(steady, eng->import_stat_count(), "the computed site skips between passes too");
                // 0: every reach checks
                eng->import_recheck_interval(0);
                const uint64_t zero = eng->import_stat_count();
                eng->execute("for (int i = 0; i < 4; i++) { import \"pp_one.jai\"; }");
                check_eq(zero + 4, eng->import_stat_count(), "0 checks on every reach");
                check_near(0.0, eng->execute("import_recheck_interval();").as_float(), 1e-12);
                eng->disable(jai::filesystem);
                check_eq(std::string("Insufficient privilege: filesystem."), eng->execute("var m = \"\";\ntry { frame(); } catch (e) { m = e; }\nm;").to_string(), "a disabled privilege is never skipped");
                eng->enable(jai::filesystem);
            }
            for (const char* f : {"pp_one.jai", "pp_two.jai", "pp_nest.jai", "pp_late.jai"}) { cleanup_temp_file((dir / f).string()); }
        });

        test("a_group_import_checks_every_changed_module_before_any_runs", [=, this]() {
            // import("a", "b", ...) / import(array): one throttle decision, every changed module parsed and checked (knowing each
            // other's names) before any runs, a failure anywhere is one error that applies nothing, the changes then wait until
            // a failing file changes again, and the modules run in the listed order
            const auto dir = jai::foundry::test_temp_directory();
            int tick = 700;
            auto write_module = [&](const std::string& name, const std::string& body) {
                const auto path = dir / name;
                { std::ofstream(path, std::ios::binary | std::ios::trunc) << body; }
                std::error_code ec;
                std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2 * ++tick), ec);
            };
            for (bool use_vm : {false, true}) {
                for (bool proofs : {true, false}) {
                    auto eng = engine::make();
                    if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                jai::stdlib::register_all(*eng);
                    if (!proofs) { eng->type_proofs(false); }
                    eng->add_include_path(dir.string());
                    eng->import_recheck_interval(0);
                    write_module("gi_a.jai", "int GA = 1;\nint ga_one() { return 1; }\n");
                    write_module("gi_b.jai", "int GB = GA * 10 + ga_one();\n");
                    check_eq(std::string("true|11"), eng->execute("array<string> MODS = [\"gi_a.jai\", \"gi_b.jai\"];\nbool r = import(MODS);\nto_string(r) + \"|\" + to_string(GB);").to_string());
                    write_module("gi_a.jai", "int GA = 2;\nint ga_one() { return 1; }\nint ga_two() { return 2; }\n");
                    write_module("gi_b.jai", "int GB = \"broken\";\n");
                    std::string failed = eng->execute("string m = \"\";\ntry { import(MODS); } catch (e) { m = e; }\nm;").to_string();
                    check_true(failed.find("gi_b.jai") != std::string::npos, "one catchable error names the failing module: " + failed);
                    check_eq(std::string("1|11"), eng->execute("to_string(GA) + \"|\" + to_string(GB);").to_string(), "nothing applied: gi_a's good change waits");
                    check_eq(std::string("false|1"), eng->execute("bool again = import(MODS);\nto_string(again) + \"|\" + to_string(GA);").to_string(), "the group waits quietly until the failing file changes");
                    write_module("gi_b.jai", "int GB = GA * 10 + ga_two();\n");
                    check_eq(std::string("true|2|22"), eng->execute("bool fixed = import(\"gi_a.jai\", \"gi_b.jai\");\nto_string(fixed) + \"|\" + to_string(GA) + \"|\" + to_string(GB);").to_string(),
                             "the fix applies both in order, gi_b checked against gi_a's new function");
                    write_module("gi_a.jai", "int GA = \"x\";\n");
                    write_module("gi_b.jai", "int GB = \"y\";\n");
                    std::string both = eng->execute("string m = \"\";\ntry { import(MODS); } catch (e) { m = e; }\nm;").to_string();
                    check_true(both.find("gi_a.jai") != std::string::npos && both.find("gi_b.jai") != std::string::npos, "two failures, one error: " + both);
                    check_eq(std::string("2|22"), eng->execute("to_string(GA) + \"|\" + to_string(GB);").to_string());
                }
            }
            for (const char* f : {"gi_a.jai", "gi_b.jai"}) { cleanup_temp_file((dir / f).string()); }
        });

        test("a_root_file_lists_its_check_errors_under_its_path", [=, this]() {
            // the root file's listing names the file like an included or imported one does; source text stays "script"
            const std::string path = (jai::foundry::test_temp_directory() / "root_listing.jai").generic_string();
            for (bool use_vm : {false, true}) {
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                std::string from_file, from_text;
                try { eng->execute_file_source(path, "int x = \"s\";\n"); } catch (const std::exception& ex) { from_file = ex.what(); }
                try { eng->execute("int y = \"s\";\n"); } catch (const std::exception& ex) { from_text = ex.what(); }
                check_true(from_file.find(path + ":1:5: error: cannot initialize 'x'") != std::string::npos, from_file);
                check_true(from_file.find("script:1:5") == std::string::npos, from_file);
                check_true(from_text.find("script:1:5: error: cannot initialize 'y'") != std::string::npos, from_text);
            }
        });

        test("a_module_rerun_keeps_a_variable_declared_without_an_initializer", [=, this]() {
            // Dev's ruling (2026-09-27): a same-unit re-run resets a variable only when its declaration assigns; `T x;` keeps the
            // value (the first run defaults it), unless its declared type changed, which resets it to the new type's default
            const auto dir = jai::foundry::test_temp_directory();
            int tick = 100;
            auto write_module = [&](const std::string& name, const std::string& body) {
                const auto path = dir / name;
                { std::ofstream(path, std::ios::binary | std::ios::trunc) << body; }
                std::error_code ec;
                std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2 * ++tick), ec);
            };
            for (bool proofs : {true, false}) {
                auto eng = make_engine();
                jai::stdlib::register_all(*eng);
                eng->type_proofs(proofs);
                eng->add_include_path(dir.string());
                const std::string body = "int kills;\nint resets = 0;\narray<int> pack;\nclass KeepBox { int n = 0; }\nKeepBox held;\nvar loose;\n"
                                         "void kill() { kills += 1; resets += 1; pack.push(kills); held.n = kills; loose = kills; }\n";
                write_module("ia_state.jai", body);
                eng->execute("void load_state() { import \"ia_state.jai\"; }\nload_state();");
                check_eq(std::string("0 0 0 0 null"), eng->execute("to_string(kills) + \" \" + to_string(resets) + \" \" + to_string(pack.size()) + \" \" + to_string(held.n) + \" \" + to_string(loose);").to_string(), "the first run defaults");
                eng->execute("kill(); kill(); kill();");
                write_module("ia_state.jai", body + "// touched\n");
                eng->execute("load_state();");
                check_eq(std::string("3 0 3 3 3"), eng->execute("to_string(kills) + \" \" + to_string(resets) + \" \" + to_string(pack.size()) + \" \" + to_string(held.n) + \" \" + to_string(loose);").to_string(),
                         "declarations without an initializer keep their values; the one that assigns resets");
                write_module("ia_state.jai", "float kills;\nint resets = 0;\narray<int> pack;\nclass KeepBox { int n = 0; }\nKeepBox held;\nvar loose;\n");
                eng->execute("load_state();");
                check_eq(std::string("float 0.0 3"), eng->execute("type_of(kills) + \" \" + to_string(kills) + \" \" + to_string(pack.size());").to_string(), "a declared type change resets to the new default");
                // the host's re-run of the same file is the same unit and keeps too
                const std::string host_path = (dir / "ia_host_state.jai").string();
                check_eq((int64_t)0, eng->execute_file_source(host_path, "int hp;\nhp;").as_int());
                eng->execute("hp = 42;");
                check_eq((int64_t)42, eng->execute_file_source(host_path, "int hp;\nhp;").as_int());
                check_eq((int64_t)5, eng->execute_file_source(host_path, "int hp = 5;\nhp;").as_int());
            }
            cleanup_temp_file((dir / "ia_state.jai").string());
        });

        test("a_leaf_the_vm_inlines_is_replaced_by_its_modules_rerun_inside_the_hot_loop", [=, this]() {
            // the loop's own import re-runs the touched module mid-loop: the call that was grafted sees the new body from the
            // next iteration on, on both backends
            const auto dir = jai::foundry::test_temp_directory();
            const auto path = dir / "ia_leaf.jai";
            int tick = 200;
            auto write_leaf = [&](const std::string& body) {
                { std::ofstream(path, std::ios::binary | std::ios::trunc) << body; }
                std::error_code ec;
                std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2 * ++tick), ec);
            };
            for (bool proofs : {true, false}) {
                write_leaf("int ia_leaf(int x) { return x + 1; }\n");
                auto eng = make_engine();
                eng->type_proofs(proofs);
                eng->add_include_path(dir.string());
                eng->add_function("touch_leaf", [&]() { write_leaf("int ia_leaf(int x) { return x + 1000; }\n"); });
                const auto total = eng->execute("import \"ia_leaf.jai\";\n"
                                                "int run() { int total = 0; for (int i = 0; i < 2000; i++) { import \"ia_leaf.jai\"; total += ia_leaf(i); if (i == 999) { touch_leaf(); } } return total; }\n"
                                                "run();").as_int();
                check_eq((int64_t)3000000, total);
            }
            std::filesystem::remove(path);
        });

        test("import_mtime_gate_sits_above_disk_cache", [=, this]() {
            // import's once-per-change gate decides IF the file runs at all; the disk
            // cache only decides parse-vs-load when it does (pin: hot-reload semantics).
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_imp.jai", "cache_imp_bump();");
                std::filesystem::remove(sibling_of(src));
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                int bumps = 0;
                eng->add_function("cache_imp_bump", [&bumps]() { bumps++; });
                eng->add_include_path(jai::foundry::test_temp_directory().string());
                eng->set_import_behavior(engine::import_behavior::file_timestamp);
                eng->import_recheck_interval(0);   // the touch and the re-import happen within the default window
                eng->execute("import \"cache_imp.jai\"");
                eng->execute("import \"cache_imp.jai\"");
                check_eq(1, bumps, "unchanged file: import runs once");
                {
                    std::ofstream f(src, std::ios::trunc);
                    f << "cache_imp_bump(); cache_imp_bump();";
                }
                auto bumped = std::filesystem::last_write_time(sibling_of(src)) + std::chrono::seconds(2);
                std::filesystem::last_write_time(src, bumped);   // deterministic "edited later"
                eng->execute("import \"cache_imp.jai\"");
                check_eq(3, bumps, "mtime change: import re-runs the edited body");
                eng->execute("import \"cache_imp.jai\"");
                check_eq(3, bumps, "no further change: gated again");
                cleanup_temp_file(src);
                std::filesystem::remove(sibling_of(src));
            }
        });

        // A trace with each frame's directory dropped: "  at f (c.jai:1)\n..."
        auto trace_files = [](const std::string& trace) {
            std::string out;
            size_t start = 0;
            while (start < trace.size()) {
                size_t end = trace.find('\n', start);
                if (end == std::string::npos) { end = trace.size(); }
                std::string line = trace.substr(start, end - start);
                size_t open = line.find('(');
                size_t colon = line.rfind(':');
                if (open != std::string::npos && colon != std::string::npos && colon > open) {
                    size_t sep = line.find_last_of("/\\", colon);
                    if (sep != std::string::npos && sep > open) { line.erase(open + 1, sep - open); }
                }
                out += line + "\n";
                start = end + 1;
            }
            return out;
        };

        test("an_error_in_an_imported_top_level_names_the_imported_frames_and_the_import_chain", [=, this]() {
            // the imported unit's frames lead, the importer's frame at the import follows, byte-identical on both backends
            create_temp_file("imp_err_c.jai", "int boom() { array<int> a = [1]; return a[5]; }\nint X = boom();\n");
            create_temp_file("imp_err_d.jai", "import \"imp_err_c.jai\";\n");
            create_temp_file("imp_err_t.jai", "var pad = 1;\nthrow \"from the top\";\n");
            create_temp_file("imp_err_u.jai", "function run(string p) {\n\tinclude(p);\n}\nrun(\"imp_err_t.jai\");\n");
            auto eng = make_engine();
            jai::stdlib::register_all(*eng);
            eng->add_include_path(jai::foundry::test_temp_directory().string());
            std::string what;
            try { eng->execute("var before = 1;\nimport \"imp_err_d.jai\";\n"); } catch (const std::exception& ex) { what = ex.what(); }
            check_eq(std::string("Array index 5 out of bounds for array of size 1: Array index out of bounds"), what);
            check_eq(std::string("  at boom (imp_err_c.jai:1)\n  at <script> (imp_err_c.jai:2)\n  at <script> (imp_err_d.jai:1)\n  at <script> (<script>:2)\n"),
                     trace_files(eng->format_stack_trace()));
            what.clear();
            try { eng->execute("include \"imp_err_u.jai\";\n"); } catch (const std::exception& ex) { what = ex.what(); }
            check_eq(std::string("from the top"), what);
            check_eq(std::string("  at <script> (imp_err_t.jai:2)\n  at run (imp_err_u.jai:2)\n  at <script> (imp_err_u.jai:4)\n  at <script> (<script>:1)\n"),
                     trace_files(eng->format_stack_trace()));
            for (const char* f : {"imp_err_c.jai", "imp_err_d.jai", "imp_err_t.jai", "imp_err_u.jai"}) { cleanup_temp_file((jai::foundry::test_temp_directory() / f).string()); }
        });

        test("file_data_answers_a_files_metadata_by_the_one_ladder", [=, this]() {
            // engine-core: path, filename, extension (with its dot), created/modified (ms since the Unix epoch), size, is_directory;
            // a relative name resolves like resolve_path; nothing there answers null (a watcher polls it without try/catch)
            const auto dir = jai::foundry::test_temp_directory() / "file_data_probe";
            std::filesystem::create_directories(dir);
            const auto file = dir / "save.dat";
            for (bool use_vm : {false, true}) {
                { std::ofstream(file, std::ios::binary | std::ios::trunc) << "12345"; }
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                jai::stdlib::register_all(*eng);
                eng->add_include_path(dir.string());
                auto d = eng->execute("var d = file_data(\"save.dat\"); d;");
                check_true(d.is_map(), "a map of the file's metadata");
                const std::string path = eng->execute("file_data(\"save.dat\")[\"path\"];").to_string();
                check_true(path.find("file_data_probe/save.dat") != std::string::npos && path.find('\\') == std::string::npos, path);
                check_eq(path, eng->execute("resolve_path(\"save.dat\");").to_string(), "the ladder's canonical spelling");
                check_eq(std::string("save.dat|.dat|5|false"), eng->execute("var d = file_data(\"save.dat\"); d[\"filename\"] + \"|\" + d[\"extension\"] + \"|\" + to_string(d[\"size\"]) + \"|\" + to_string(d[\"is_directory\"]);").to_string());
                const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                const int64_t modified = eng->execute("file_data(\"save.dat\")[\"modified\"];").as_int();
                check_true(modified > now_ms - 3600000 && modified <= now_ms + 60000, "modified is ms since the Unix epoch");
                auto created = eng->execute("file_data(\"save.dat\")[\"created\"];");
                check_true(created.is_null() || (created.is_int() && created.as_int() <= modified + 1000), "created: ms, or null where unavailable");
                // a rewrite moves modified (stamped forward so the clock's granularity cannot hide it)
                { std::ofstream(file, std::ios::binary | std::ios::trunc) << "1234567"; }
                std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(5));
                check_true(eng->execute("file_data(\"save.dat\")[\"modified\"];").as_int() > modified, "a rewrite moves modified");
                check_eq(script_int(7), eng->execute("file_data(\"save.dat\")[\"size\"];").as_int());
                check_true(eng->execute("file_data(\"no_such_file.dat\") == null;").as_bool(), "missing -> null");
                check_eq(std::string("true|0|"), eng->execute("var f = file_data(\"" + dir.generic_string() + "\"); to_string(f[\"is_directory\"]) + \"|\" + to_string(f[\"size\"]) + \"|\" + f[\"extension\"];").to_string());
                eng->disable(jai::filesystem);
                std::string denied;
                try { denied = eng->execute("var m = \"\"; try { file_data(\"save.dat\"); } catch (e) { m = e; } m;").to_string(); } catch (const std::exception& ex) { denied = ex.what(); }
                check_eq(std::string("Insufficient privilege: filesystem."), denied);
            }
            std::filesystem::remove_all(dir);
        });

        test("an_enum_in_a_file_included_inside_a_function_or_block_is_local_and_warns", [=, this]() {
            // include is textual: the file's enum is declared where the include runs; inside a function or a block that is a
            // local enum, so the program-scope enum keeps its values, and the include's report says so (both backends)
            create_temp_file("enum_inc.jai", "enum Color { cyan, magenta, yellow, key }\nvar seen = Color.yellow;\n");
            for (bool proofs : {true, false}) {
                auto eng = make_engine();
                jai::stdlib::register_all(*eng);
                eng->type_proofs(proofs);
                eng->add_include_path(jai::foundry::test_temp_directory().string());
                std::vector<std::string> channel;
                eng->set_script_warning_handler([&channel](const std::string& w) { channel.push_back(w); });
                check_eq((int64_t)2, eng->execute("enum Color { red, green, blue }\nvoid reload(string p) { include(p); }\nreload(\"enum_inc.jai\");\nColor.blue;").as_int());
                check_true(channel.size() == 1 && channel[0].find("enum 'Color' declared in function 'reload' is local to it") != std::string::npos, "the host's warning channel carries it");
                const check_report& r = eng->last_check_diagnostics();
                bool warned = false;
                for (const auto& d : r.diagnostics) {
                    if (d.severity == check_diagnostic::level::warning && d.message.find("enum_inc.jai:1:6: enum 'Color' declared in function 'reload' is local to it: the program-scope 'Color' keeps its values") != std::string::npos) { warned = true; }
                }
                check_true(warned, "the include's report names the enum and the function");
                check_eq((int64_t)2, eng->execute("string q = \"enum_inc.jai\";\n{ include(q); }\nColor.blue;").as_int());
                warned = false;
                for (const auto& d : eng->last_check_diagnostics().diagnostics) {
                    if (d.message.find("enum 'Color' declared in a block is local to it") != std::string::npos) { warned = true; }
                }
                check_true(warned, "a block include warns too");
                eng->execute("string top = \"enum_inc.jai\";\ninclude(top);");
                for (const auto& d : eng->last_check_diagnostics().diagnostics) {
                    check_true(d.message.find("is local to it") == std::string::npos, "a program-scope include replaces nothing silently: it is the program scope");
                }
            }
            cleanup_temp_file((jai::foundry::test_temp_directory() / "enum_inc.jai").string());
        });

        test("an_error_in_an_included_file_is_caught_like_inline_code", [=, this]() {
            // include is textual: a throw keeps its value and type, a runtime error is caught with its text alone, a load failure
            // (static check, parse) is caught with its listing, which names the included file; both backends, proofs on and off
            const std::string dir = jai::foundry::test_temp_directory().string();
            const std::string path = (jai::foundry::test_temp_directory() / "inc_err.jai").string();
            auto run = [&](jai::engine& e, const std::string& body) {
                create_temp_file("inc_err.jai", body);
                return e.execute("var r = null;\ntry { include(\"inc_err.jai\"); r = \"no error\"; } catch (e) { r = e; }\nr;");
            };
            for (bool proofs : {true, false}) {
                auto eng = make_engine();
                jai::stdlib::register_all(*eng);
                eng->type_proofs(proofs);
                eng->add_include_path(dir);
                check_eq(std::string("boom"), run(*eng, "throw \"boom\";\n").to_string());
                auto typed = run(*eng, "throw 42;\n");
                check_true(typed.is_int(), "a thrown int stays an int across the include");
                check_eq((int64_t)42, typed.as_int());
                check_eq(std::string("Cannot initialize 'x' (declared int) with string"), run(*eng, "var s = \"s\";\nint x = s;\n").to_string());
                check_eq(std::string("Array index 5 out of bounds for array of size 1"), run(*eng, "int boom() { array<int> a = [1]; return a[5]; }\nboom();\n").to_string());
                const std::string checked = run(*eng, "int x = \"s\";\n").to_string();
                check_true(checked.rfind("static check failed:\n", 0) == 0, checked);
                check_true(checked.find("inc_err.jai:1:5: error: cannot initialize 'x' declared 'int' with 'string'\n    int x = \"s\";\n        ^\n") != std::string::npos, checked);
                check_true(checked.find("script:1:5") == std::string::npos, checked);
                const std::string parsed = run(*eng, "void f(int var) {}\n").to_string();
                check_true(parsed.rfind("Parse error: ", 0) == 0 && parsed.find("inc_err.jai:1:12:") != std::string::npos, parsed);
                // from a function body, a computed path; the program goes on after every catch
                create_temp_file("inc_err.jai", "throw \"deep\";\n");
                check_eq(std::string("deep after"), eng->execute("string go(string p) { try { include(p); } catch (e) { return e + \" after\"; } return \"none\"; }\ngo(\"inc_err.jai\");").to_string());
                // a nested include: the inner failure crosses both boundaries
                create_temp_file("inc_err_outer.jai", "include(\"inc_err.jai\");\n");
                check_eq(std::string("deep"), eng->execute("var q = null;\ntry { include(\"inc_err_outer.jai\"); } catch (e) { q = e; }\nq;").to_string());
                // after a failed include the includer's own location queries name the includer again
                const std::string root = (jai::foundry::test_temp_directory() / "inc_err_root.jai").string();
                const std::string here = eng->execute_file_source(root, "var p = \"\";\ntry { include(\"inc_err.jai\"); } catch (e) { }\np = current_script_path();\np;").to_string();
                check_true(here.size() >= 16 && here.substr(here.size() - 16) == "inc_err_root.jai", here);
            }
            cleanup_temp_file(path);
            cleanup_temp_file((jai::foundry::test_temp_directory() / "inc_err_outer.jai").string());
        });
    }
};

}

FOUNDRY_REGISTER(jai::foundry::tests::include_import_tests)
