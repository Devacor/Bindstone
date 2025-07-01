#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    jai::stdlib::register_all(engine);
    
    // Add a debug version of format to understand what's happening
    engine.add_variadic_function("debug_format", [](const std::vector<jai::script_value>& args) -> jai::script_value {
        std::cout << "[DEBUG] format called with " << args.size() << " args" << std::endl;
        
        if (args.empty()) {
            std::cout << "[DEBUG] empty args, returning empty string" << std::endl;
            return jai::script_value("");
        }
        
        // Filter out control types
        std::vector<jai::script_value> filtered_args;
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& arg = args[i];
            auto type_info = arg.get_type_info();
            std::cout << "[DEBUG] arg " << i << " type: " 
                      << (type_info ? type_info->type_name : "null") << std::endl;
            
            if (!type_info || (type_info->type_name != "skip_newline_t" && 
                               type_info->type_name != "skip_flush_t")) {
                filtered_args.push_back(arg);
            }
        }
        
        std::cout << "[DEBUG] filtered args: " << filtered_args.size() << std::endl;
        
        if (filtered_args.empty()) {
            std::cout << "[DEBUG] filtered args empty, returning empty string" << std::endl;
            return jai::script_value("");
        }
        
        if (filtered_args.size() == 1) {
            std::cout << "[DEBUG] single arg, converting to string" << std::endl;
            std::string str = filtered_args[0].to_string();
            std::cout << "[DEBUG] string value: '" << str << "'" << std::endl;
            jai::script_value result(str);
            std::cout << "[DEBUG] created script_value, is_string: " << result.is_string() << std::endl;
            return result;
        }
        
        // Multiple args case...
        return jai::script_value("multiple args not implemented in debug");
    });
    
    try {
        std::cout << "Testing debug_format:\n" << std::endl;
        
        std::cout << "1. debug_format('test'):" << std::endl;
        auto result = engine.execute("debug_format('test')");
        std::cout << "Result is_string: " << result.is_string() << std::endl;
        if (result.is_string()) {
            std::cout << "Result value: '" << result.as<std::string>() << "'" << std::endl;
        }
        
        std::cout << "\n2. Regular format('test'):" << std::endl;
        result = engine.execute("format('test')");
        std::cout << "Result is_string: " << result.is_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}