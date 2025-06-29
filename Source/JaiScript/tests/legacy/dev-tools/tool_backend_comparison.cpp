#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <chrono>
#include <map>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

struct TestRunResult {
    std::string test_name;
    bool interpreter_passed;
    bool jvm_passed;
    double interpreter_time_ms;
    double jvm_time_ms;
    std::string interpreter_output;
    std::string jvm_output;
};

// Run a command and capture output
std::pair<bool, std::string> run_command(const std::string& command) {
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return {false, "Failed to run command"};
    }
    
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    
    int status = pclose(pipe);
    bool success = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
    
    return {success, output};
}

// Run a test with specific backend
std::pair<bool, double> run_test_with_backend(const std::string& test_path, const std::string& backend) {
    // Set environment variable
    setenv("JAISCRIPT_BACKEND", backend.c_str(), 1);
    
    auto start = std::chrono::high_resolution_clock::now();
    auto [success, output] = run_command(test_path);
    auto end = std::chrono::high_resolution_clock::now();
    
    double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    return {success, elapsed_ms};
}

int main(int argc, char* argv[]) {
    std::vector<std::string> test_executables;
    
    // Get list of test executables
    if (argc > 1) {
        // Specific tests provided
        for (int i = 1; i < argc; ++i) {
            test_executables.push_back(argv[i]);
        }
    } else {
        // Find all test executables in bin directory
        auto [success, output] = run_command("find bin -name 'test_*' -type f | sort");
        if (!success) {
            std::cerr << "Failed to find test executables\n";
            return 1;
        }
        
        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) {
                test_executables.push_back(line);
            }
        }
    }
    
    if (test_executables.empty()) {
        std::cerr << "No test executables found\n";
        return 1;
    }
    
    std::cout << "Running " << test_executables.size() << " tests with both backends...\n\n";
    
    std::vector<TestRunResult> results;
    
    // Run each test with both backends
    for (const auto& test_path : test_executables) {
        TestRunResult result;
        
        // Extract test name from path
        size_t last_slash = test_path.find_last_of('/');
        result.test_name = (last_slash != std::string::npos) 
                          ? test_path.substr(last_slash + 1) 
                          : test_path;
        
        std::cout << "Running " << result.test_name << "...\n";
        
        // Run with interpreter
        {
            auto [success, time] = run_test_with_backend(test_path, "interpreter");
            result.interpreter_passed = success;
            result.interpreter_time_ms = time;
        }
        
        // Run with JVM
        {
            auto [success, time] = run_test_with_backend(test_path, "jvm");
            result.jvm_passed = success;
            result.jvm_time_ms = time;
        }
        
        results.push_back(result);
    }
    
    // Clear environment variable
    unsetenv("JAISCRIPT_BACKEND");
    
    // Print summary
    std::cout << "\n\n=== Backend Comparison Summary ===\n\n";
    
    std::cout << std::left << std::setw(40) << "Test Name" 
              << std::setw(15) << "Interpreter" 
              << std::setw(15) << "JVM"
              << std::setw(15) << "Interp Time"
              << std::setw(15) << "JVM Time"
              << std::setw(15) << "Speedup"
              << "\n";
    std::cout << std::string(110, '-') << "\n";
    
    int total_tests = 0;
    int interpreter_passed = 0;
    int jvm_passed = 0;
    int both_passed = 0;
    double total_interpreter_time = 0;
    double total_jvm_time = 0;
    
    // Categorize results
    std::vector<std::string> only_interpreter_passed;
    std::vector<std::string> only_jvm_passed;
    std::vector<std::string> both_failed;
    
    for (const auto& result : results) {
        total_tests++;
        
        std::cout << std::left << std::setw(40) << result.test_name;
        
        // Interpreter result
        if (result.interpreter_passed) {
            std::cout << std::setw(15) << "✓ PASS";
            interpreter_passed++;
        } else {
            std::cout << std::setw(15) << "✗ FAIL";
        }
        
        // JVM result
        if (result.jvm_passed) {
            std::cout << std::setw(15) << "✓ PASS";
            jvm_passed++;
        } else {
            std::cout << std::setw(15) << "✗ FAIL";
        }
        
        // Timing
        std::cout << std::setw(15) << std::fixed << std::setprecision(2) 
                  << result.interpreter_time_ms << " ms";
        std::cout << std::setw(15) << std::fixed << std::setprecision(2) 
                  << result.jvm_time_ms << " ms";
        
        // Speedup
        if (result.jvm_time_ms > 0 && result.interpreter_passed && result.jvm_passed) {
            double speedup = result.interpreter_time_ms / result.jvm_time_ms;
            std::cout << std::setw(15) << std::fixed << std::setprecision(2) 
                      << speedup << "x";
            total_interpreter_time += result.interpreter_time_ms;
            total_jvm_time += result.jvm_time_ms;
            both_passed++;
        } else {
            std::cout << std::setw(15) << "N/A";
        }
        
        std::cout << "\n";
        
        // Categorize
        if (result.interpreter_passed && !result.jvm_passed) {
            only_interpreter_passed.push_back(result.test_name);
        } else if (!result.interpreter_passed && result.jvm_passed) {
            only_jvm_passed.push_back(result.test_name);
        } else if (!result.interpreter_passed && !result.jvm_passed) {
            both_failed.push_back(result.test_name);
        }
    }
    
    std::cout << "\n=== Statistics ===\n";
    std::cout << "Total tests run: " << total_tests << "\n";
    std::cout << "Interpreter passed: " << interpreter_passed << " (" 
              << std::fixed << std::setprecision(1) 
              << (100.0 * interpreter_passed / total_tests) << "%)\n";
    std::cout << "JVM passed: " << jvm_passed << " (" 
              << std::fixed << std::setprecision(1) 
              << (100.0 * jvm_passed / total_tests) << "%)\n";
    std::cout << "Both passed: " << both_passed << "\n";
    
    if (both_passed > 0) {
        double avg_speedup = total_interpreter_time / total_jvm_time;
        std::cout << "\nPerformance comparison (tests that passed both):\n";
        std::cout << "Total interpreter time: " << std::fixed << std::setprecision(2) 
                  << total_interpreter_time << " ms\n";
        std::cout << "Total JVM time: " << std::fixed << std::setprecision(2) 
                  << total_jvm_time << " ms\n";
        std::cout << "Average speedup: " << std::fixed << std::setprecision(2) 
                  << avg_speedup << "x\n";
    }
    
    // Show discrepancies
    if (!only_interpreter_passed.empty()) {
        std::cout << "\n=== Tests that only passed with interpreter ===\n";
        for (const auto& test : only_interpreter_passed) {
            std::cout << "  - " << test << "\n";
        }
    }
    
    if (!only_jvm_passed.empty()) {
        std::cout << "\n=== Tests that only passed with JVM ===\n";
        for (const auto& test : only_jvm_passed) {
            std::cout << "  - " << test << "\n";
        }
    }
    
    if (!both_failed.empty()) {
        std::cout << "\n=== Tests that failed with both backends ===\n";
        for (const auto& test : both_failed) {
            std::cout << "  - " << test << "\n";
        }
    }
    
    // Return non-zero if there are discrepancies
    return (only_interpreter_passed.empty() && only_jvm_passed.empty()) ? 0 : 1;
}