#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <script.jai>" << std::endl;
        return 1;
    }

    // Read script file
    std::ifstream file(argv[1]);
    if (!file) {
        std::cerr << "Failed to open file: " << argv[1] << std::endl;
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string script = buffer.str();

    // Create engine
    auto eng = jai::engine::make();
    jai::stdlib::register_all(*eng);

    try {
        auto result = eng->execute(script);
        std::cout << "Result: " << result.to_string() << std::endl;
        std::cout << "Result type: " << static_cast<int>(result.type()) << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}