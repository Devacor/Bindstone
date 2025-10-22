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
            auto eng = engine::make();
            
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
            auto eng = engine::make();
            
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
            auto eng = engine::make();
            
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
            auto eng = engine::make();
            
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
            auto eng = engine::make();
            
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
            auto eng = engine::make();
            
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
            auto eng = engine::make();
            
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
            auto eng = engine::make();
            
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
            auto eng = engine::make();
            
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
            auto eng = engine::make();
            
            // Try to include non-existent file
            check_throws([&]() {
                eng->execute("include \"non_existent_file.jai\"");
            });
            
            check_throws([&]() {
                eng->execute("import <another_missing_file.jai>");
            });
        });
        
        test("nested_includes", [&]() {
            auto eng = engine::make();
            
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
            auto eng = engine::make();
            
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
            auto eng = engine::make();
            
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
    }
};

}

FOUNDRY_REGISTER(jai::foundry::tests::include_import_tests)