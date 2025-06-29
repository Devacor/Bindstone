#include <iostream>
#include <jaiscript/serialization/binary_archive.hpp>
#include <jaiscript/core/value.hpp>

using namespace jai;
using namespace jai::serialization;

int main() {
    try {
        std::cout << "=== Direct Binary Archive Test ===" << std::endl;
        
        // Test basic binary serialization
        binary_archive_writer writer;
        
        std::cout << "1. Testing integer 42:" << std::endl;
        script_value test_value(static_cast<script_int>(42));
        writer.write_value(test_value);
        
        const auto& data = writer.data();
        std::cout << "   Binary size: " << data.size() << " bytes" << std::endl;
        std::cout << "   First few bytes (hex): ";
        for (size_t i = 0; i < std::min(size_t(10), data.size()); ++i) {
            printf("%02x ", data[i]);
        }
        std::cout << std::endl;
        
        // Test deserialization
        std::cout << "\n2. Testing deserialization:" << std::endl;
        binary_archive_reader reader(data);
        script_value restored = reader.read_value();
        
        std::cout << "   Restored type: " << static_cast<int>(restored.type()) << std::endl;
        if (restored.is_int()) {
            std::cout << "   Restored value: " << restored.as<script_int>() << std::endl;
            std::cout << "   SUCCESS: Round trip worked!" << std::endl;
        } else {
            std::cout << "   FAILED: Not an integer" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}