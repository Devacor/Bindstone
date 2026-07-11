#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
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
            std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
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
        
        test("basic_include", [&]() {
            auto eng = make_engine();
            
            // Create a temporary file to include
            auto math_file = create_temp_file("math_lib.jai", R"(
                function add(int a, int b) -> int {
                    return a + b;
                }
                auto PI = 3.14159;
            )");
            
            // Add temp directory to include paths
            eng->add_include_path(std::filesystem::temp_directory_path().string());
            
            // Include the file
            eng->execute("include \"math_lib.jai\"");
            
            // Verify functions and variables are available
            check_eq(eng->execute("add(2, 3)").as<int>(), 5);
            check_eq(eng->execute("PI").as<double>(), 3.14159);
            
            cleanup_temp_file(math_file);
        });
        
        test("include_with_angle_brackets", [&]() {
            auto eng = make_engine();
            
            // Create a temporary file
            auto lib_file = create_temp_file("stdlib.jai", R"(
                function multiply(int a, int b) -> int {
                    return a * b;
                }
            )");
            
            // Add temp directory to include paths
            eng->add_include_path(std::filesystem::temp_directory_path().string());
            
            // Include with angle brackets
            eng->execute("include <stdlib.jai>");
            
            // Verify function is available
            check_eq(eng->execute("multiply(4, 5)").as<int>(), 20);
            
            cleanup_temp_file(lib_file);
        });
        
        test("multiple_includes_execute_multiple_times", [&]() {
            auto eng = make_engine();
            
            // Create a file with side effects
            auto counter_file = create_temp_file("counter.jai", R"(
                auto counter = 0;
            )");
            
            eng->add_include_path(std::filesystem::temp_directory_path().string());
            
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
        
        test("basic_import_once_behavior", [&]() {
            auto eng = make_engine();
            
            // Create a file with side effects
            auto module_file = create_temp_file("module.jai", R"(
                auto module_loaded = 1;
                
                function get_module_count() -> int {
                    return module_loaded;
                }
            )");
            
            eng->add_include_path(std::filesystem::temp_directory_path().string());
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
        
        test("import_file_timestamp_behavior", [&]() {
            auto eng = make_engine();
            
            // Create initial file
            auto test_file = create_temp_file("timestamp_test.jai", R"(
                auto version = 1;
            )");
            
            eng->add_include_path(std::filesystem::temp_directory_path().string());
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
        
        test("import_always_behavior", [&]() {
            auto eng = make_engine();
            
            // Create a file with counter
            auto always_file = create_temp_file("always_import.jai", R"(
                auto import_count = 0;
            )");
            
            eng->add_include_path(std::filesystem::temp_directory_path().string());
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
        
        test("include_path_resolution", [&]() {
            auto eng = make_engine();
            
            // Create files in different directories
            auto temp_dir = std::filesystem::temp_directory_path();
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
        
        test("import_tracking_api", [&]() {
            auto eng = make_engine();
            
            // Create test files
            auto file1 = create_temp_file("tracked1.jai", "auto file1_loaded = true;");
            auto file2 = create_temp_file("tracked2.jai", "auto file2_loaded = true;");
            
            eng->add_include_path(std::filesystem::temp_directory_path().string());
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
        
        test("hash_prefix_ignored", [&]() {
            auto eng = make_engine();
            
            // Create a test file
            auto hash_file = create_temp_file("hash_test.jai", R"(
                auto with_hash = 42;
            )");
            
            eng->add_include_path(std::filesystem::temp_directory_path().string());
            
            // Test that # prefix is ignored
            eng->execute("#include \"hash_test.jai\"");
            check_eq(eng->execute("with_hash").as<int>(), 42);
            
            // Also works with import
            eng->execute("#import <hash_test.jai>");
            
            cleanup_temp_file(hash_file);
        });
        
        test("file_not_found_error", [&]() {
            auto eng = make_engine();
            
            // Try to include non-existent file
            check_throws([&]() {
                eng->execute("include \"non_existent_file.jai\"");
            });
            
            check_throws([&]() {
                eng->execute("import <another_missing_file.jai>");
            });
        });
        
        test("nested_includes", [&]() {
            auto eng = make_engine();
            
            // Create nested include files
            auto inner_file = create_temp_file("inner.jai", R"(
                auto inner_value = 100;
            )");
            
            auto outer_file = create_temp_file("outer.jai", R"(
                include "inner.jai"
                auto outer_value = inner_value + 50;
            )");
            
            eng->add_include_path(std::filesystem::temp_directory_path().string());
            
            // Include outer file which includes inner
            eng->execute("include \"outer.jai\"");
            
            check_eq(eng->execute("inner_value").as<int>(), 100);
            check_eq(eng->execute("outer_value").as<int>(), 150);
            
            cleanup_temp_file(inner_file);
            cleanup_temp_file(outer_file);
        });
        
        test("dynamic_include_with_variable", [&]() {
            auto eng = make_engine();
            
            // Create a test file
            auto dynamic_file = create_temp_file("dynamic_content.jai", R"(
                auto dynamic_value = 42;
                function get_dynamic() -> int {
                    return dynamic_value * 2;
                }
            )");
            
            eng->add_include_path(std::filesystem::temp_directory_path().string());
            
            // Set up a variable with the filename
            eng->execute("auto filename = \"dynamic_content.jai\";");
            
            // Include using the variable
            eng->execute("include(filename);");
            
            // Verify the content was included
            check_eq(eng->execute("dynamic_value").as<int>(), 42);
            check_eq(eng->execute("get_dynamic()").as<int>(), 84);
            
            cleanup_temp_file(dynamic_file);
        });
        
        test("dynamic_import_with_expression", [&]() {
            auto eng = make_engine();
            
            // Create test files
            auto moduleA = create_temp_file("moduleA.jai", R"(
                auto module_letter = "A";
            )");
            
            auto moduleB = create_temp_file("moduleB.jai", R"(
                auto module_letter = "B";
            )");
            
            eng->add_include_path(std::filesystem::temp_directory_path().string());
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
            
            // Since we're using 'once' behavior, importing again should not change anything
            check_eq(eng->execute("module_letter").as<std::string>(), "B");

            cleanup_temp_file(moduleA);
            cleanup_temp_file(moduleB);
        });

        // === include in expression position (Dev ruling 2026-07: include returns its value) ===
        // `var cfg = include "file.jai";` evaluates to the included file's result — exactly
        // what engine::execute returns for that source. Statement-position include is
        // unchanged (value discarded). include always RUNS the file (no cache), so its
        // value is always fresh — no interaction with import's once-cache.
        test("include_expression_returns_value", [&]() {
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
                eng->add_include_path(std::filesystem::temp_directory_path().string());
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

        test("include_expression_dynamic_path_and_nesting", [&]() {
            auto leaf = create_temp_file("expr_leaf.jai", "21;");
            for (bool use_vm : {false, true}) {
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                eng->add_include_path(std::filesystem::temp_directory_path().string());
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

        test("include_statement_position_unchanged", [&]() {
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
                eng->add_include_path(std::filesystem::temp_directory_path().string());
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

        test("jaibite_cache_creates_sibling_on_file_loads", [&]() {
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_make.jai", "auto cache_make_v = 11;");
                std::filesystem::remove(sibling_of(src));
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                eng->add_include_path(std::filesystem::temp_directory_path().string());
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

        test("jaibite_cache_fresh_sibling_skips_parse", [&]() {
            // Proof the parse is skipped: a fresh (strictly newer, fingerprint-matching)
            // sibling loads fine even when the .jai itself no longer parses.
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_skip.jai", "42;");
                std::filesystem::remove(sibling_of(src));
                {
                    auto writer = engine::make();
                    if (use_vm) { writer->set_backend(jai::backend_type::vm); }
                    writer->add_include_path(std::filesystem::temp_directory_path().string());
                    check_eq((int64_t)42, writer->execute("var r = include \"cache_skip.jai\"; r;").as_int());
                }
                {
                    std::ofstream f(src, std::ios::trunc);
                    f << "%%% not a script %%%";
                }
                force_older_than(src, sibling_of(src));
                auto reader = engine::make();
                if (use_vm) { reader->set_backend(jai::backend_type::vm); }
                reader->add_include_path(std::filesystem::temp_directory_path().string());
                check_eq((int64_t)42, reader->execute("var r = include \"cache_skip.jai\"; r;").as_int(),
                         use_vm ? "vm: cache hit skipped the parse" : "interp: cache hit skipped the parse");
                cleanup_temp_file(src);
                std::filesystem::remove(sibling_of(src));
            }
        });

        test("jaibite_cache_loaded_ast_keeps_file_attribution", [&]() {
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

        test("jaibite_cache_edit_goes_stale_equal_mtime_included", [&]() {
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_edit.jai", "1;");
                std::filesystem::remove(sibling_of(src));
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                eng->add_include_path(std::filesystem::temp_directory_path().string());
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

        test("jaibite_cache_corrupt_sibling_falls_back_and_rewrites", [&]() {
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_corrupt.jai", "7;");
                {
                    std::ofstream f(sibling_of(src), std::ios::binary | std::ios::trunc);
                    f << "JBITgarbage-not-a-real-bite";
                }
                force_older_than(src, sibling_of(src));   // corrupt sibling looks "fresh"
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                eng->add_include_path(std::filesystem::temp_directory_path().string());
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
                eng2->add_include_path(std::filesystem::temp_directory_path().string());
                check_eq((int64_t)7, eng2->execute("var d = include \"cache_corrupt.jai\"; d;").as_int());
                cleanup_temp_file(src);
                std::filesystem::remove(sibling_of(src));
            }
        });

        test("jaibite_cache_fingerprint_mismatch_reparses", [&]() {
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_fp.jai", "100;");
                std::filesystem::remove(sibling_of(src));
                {   // engine A stamps its (extra-function) fingerprint into the sibling
                    auto a = engine::make();
                    if (use_vm) { a->set_backend(jai::backend_type::vm); }
                    a->add_function("fp_extra", [](script_int v) -> script_int { return v; });
                    a->add_include_path(std::filesystem::temp_directory_path().string());
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
                    b->add_include_path(std::filesystem::temp_directory_path().string());
                    check_eq((int64_t)200, b->execute("var r = include \"cache_fp.jai\"; r;").as_int(),
                             "fingerprint mismatch -> reparse");
                }
                // B's reparse rewrote the sibling under B's (bare) surface; an engine
                // with the same bare surface now loads it (fingerprint matches)
                force_older_than(src, sibling_of(src));
                {
                    auto c = engine::make();
                    if (use_vm) { c->set_backend(jai::backend_type::vm); }
                    c->add_include_path(std::filesystem::temp_directory_path().string());
                    check_eq((int64_t)200, c->execute("var r = include \"cache_fp.jai\"; r;").as_int(),
                             "matching fingerprint -> sibling loads");
                }
                cleanup_temp_file(src);
                std::filesystem::remove(sibling_of(src));
            }
        });

        test("jaibite_cache_version_mismatch_reparses_and_rewrites", [&]() {
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
                eng->add_include_path(std::filesystem::temp_directory_path().string());
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
                eng2->add_include_path(std::filesystem::temp_directory_path().string());
                check_eq((int64_t)42, eng2->execute("var w = include \"cache_ver.jai\"; w;").as_int(),
                         "rewritten sibling loads under the current version");
                cleanup_temp_file(src);
                std::filesystem::remove(sibling_of(src));
            }
        });

        test("jaibite_cache_disable_switch", [&]() {
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_off.jai", "5;");
                std::filesystem::remove(sibling_of(src));
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                check(eng->jaibite_cache(), "cache defaults ON");
                eng->jaibite_cache(false);
                check(!eng->jaibite_cache(), "setter/getter");
                eng->add_include_path(std::filesystem::temp_directory_path().string());
                check_eq((int64_t)5, eng->execute("var r = include \"cache_off.jai\"; r;").as_int());
                check(!std::filesystem::exists(sibling_of(src)), "disabled -> no sibling written");
                cleanup_temp_file(src);
            }
        });

        test("jaibite_cache_write_failure_is_silent_and_counted", [&]() {
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_ro.jai", "9;");
                std::filesystem::remove(sibling_of(src));
                std::filesystem::create_directory(sibling_of(src));   // sibling path unwritable
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                eng->add_include_path(std::filesystem::temp_directory_path().string());
                check_eq((int64_t)9, eng->execute("var r = include \"cache_ro.jai\"; r;").as_int(),
                         "unwritable sibling never surfaces as an error");
                check(eng->jaibite_cache_write_failures() >= 1, "failure counted for diagnostics");
                std::filesystem::remove(sibling_of(src));
                cleanup_temp_file(src);
            }
        });

        test("import_mtime_gate_sits_above_disk_cache", [&]() {
            // import's once-per-change gate decides IF the file runs at all; the disk
            // cache only decides parse-vs-load when it does (pin: hot-reload semantics).
            for (bool use_vm : {false, true}) {
                auto src = create_temp_file("cache_imp.jai", "cache_imp_bump();");
                std::filesystem::remove(sibling_of(src));
                auto eng = engine::make();
                if (use_vm) { eng->set_backend(jai::backend_type::vm); }
                int bumps = 0;
                eng->add_function("cache_imp_bump", [&bumps]() { bumps++; });
                eng->add_include_path(std::filesystem::temp_directory_path().string());
                eng->set_import_behavior(engine::import_behavior::file_timestamp);
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
    }
};

}

FOUNDRY_REGISTER(jai::foundry::tests::include_import_tests)