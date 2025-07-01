#include <iostream>
#include <string>
#include <vector>

// Simplified version of the format logic for testing
std::string test_format(const std::string& format_str, const std::vector<std::string>& args) {
    std::string result;
    size_t pos = 0;
    size_t next_arg = 0;
    
    while (pos < format_str.length()) {
        // Look for both { and } to handle escaping
        size_t next_open = format_str.find('{', pos);
        size_t next_close = format_str.find('}', pos);
        
        // Handle }} escaping
        if (next_close != std::string::npos && 
            (next_open == std::string::npos || next_close < next_open)) {
            // Found } before { or no { left
            result += format_str.substr(pos, next_close - pos);
            if (next_close + 1 < format_str.length() && format_str[next_close + 1] == '}') {
                // }} -> }
                result += '}';
                pos = next_close + 2;
                std::cout << "  [}} escape at " << next_close << ", continuing at " << pos << "]" << std::endl;
                continue;
            } else {
                // Single } - just append it
                result += '}';
                pos = next_close + 1;
                std::cout << "  [single } at " << next_close << ", continuing at " << pos << "]" << std::endl;
                continue;
            }
        }
        
        if (next_open == std::string::npos) {
            // No more format specifiers, append rest of string
            result += format_str.substr(pos);
            break;
        }
        
        // Append text before the brace
        result += format_str.substr(pos, next_open - pos);
        
        // Check for escaped brace  
        if (next_open + 1 < format_str.length() && format_str[next_open + 1] == '{') {
            result += '{';
            pos = next_open + 2;
            std::cout << "  [{{ escape at " << next_open << ", continuing at " << pos << "]" << std::endl;
            continue;
        }
        
        // Find closing brace
        size_t close_brace = format_str.find('}', next_open);
        if (close_brace == std::string::npos) {
            // Invalid format string, append rest as-is
            result += format_str.substr(next_open);
            break;
        }
        
        // Extract content between braces
        std::string spec = format_str.substr(next_open + 1, close_brace - next_open - 1);
        std::cout << "  [placeholder {" << spec << "} at " << next_open << "]" << std::endl;
        
        if (spec.empty()) {
            // {} - use next argument
            if (next_arg < args.size()) {
                result += args[next_arg];
                next_arg++;
            }
        } else {
            // {n} - use positional argument
            try {
                size_t arg_index = std::stoull(spec);
                if (arg_index < args.size()) {
                    result += args[arg_index];
                }
            } catch (...) {
                // Invalid index, ignore
                result += "{" + spec + "}";
            }
        }
        
        pos = close_brace + 1;
    }
    
    return result;
}

void test(const std::string& name, const std::string& format, const std::vector<std::string>& args = {}) {
    std::cout << "\nTest: " << name << std::endl;
    std::cout << "Input: \"" << format << "\"" << std::endl;
    std::string result = test_format(format, args);
    std::cout << "Output: \"" << result << "\"" << std::endl;
}

int main() {
    test("Single {", "{");
    test("Single }", "}");
    test("{{ escape", "{{");
    test("}} escape", "}}");
    test("{{}}", "{{}}");
    test("{{0}}", "{{0}}");
    test("{{{0}}} with arg", "{{{0}}}", {"TEST"});
    test("{{{{}}}}", "{{{{}}}}");
    test("a{{b}}c", "a{{b}}c");
    test("{{}} = {}", "{{}} = {}", {"REPLACED"});
    
    return 0;
}