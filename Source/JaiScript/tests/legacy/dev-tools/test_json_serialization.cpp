#include "jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/serialization/binary_archive.hpp>

using namespace jai;
using namespace jai::test;
using namespace jai::serialization;

// Test class for JSON serialization
struct Person {
    std::string name = "John";
    int age = 30;
    float height = 5.9f;
    bool active = true;
};

struct Company {
    std::string name = "TechCorp";
    std::vector<Person> employees;
    int32_t founded = 2020;
};

JAI_TEST_SUITE(JsonSerializationTests)

JAI_TEST(basic_json_archive) {
    json_archive_writer writer;
    
    writer.write_int32(42);
    writer.write_string("hello");
    writer.write_float64(3.14159);
    writer.write_bool(true);
    
    std::string json = writer.str();
    expect_true(json.find("42") != std::string::npos);
    expect_true(json.find("\"hello\"") != std::string::npos);
    expect_true(json.find("3.14159") != std::string::npos);
    expect_true(json.find("true") != std::string::npos);
}

JAI_TEST(json_object_structure) {
    json_archive_writer writer(2);  // 2-space indent
    
    writer.begin_object("Person", 1);
    writer.write_property_name("name");
    writer.write_string("Alice");
    writer.write_property_name("age");
    writer.write_int32(25);
    writer.write_property_name("active");
    writer.write_bool(true);
    writer.end_object();
    
    std::string json = writer.str();
    
    // Check structure
    expect_true(json.find("\"_type_\": \"Person\"") != std::string::npos);
    expect_true(json.find("\"name\": \"Alice\"") != std::string::npos);
    expect_true(json.find("\"age\": 25") != std::string::npos);
    expect_true(json.find("\"active\": true") != std::string::npos);
}

JAI_TEST(json_array_structure) {
    json_archive_writer writer(2);
    
    writer.begin_array(3);
    writer.write_int32(1);
    writer.write_int32(2);
    writer.write_int32(3);
    writer.end_array();
    
    std::string json = writer.str();
    expect_true(json.find("[") != std::string::npos);
    expect_true(json.find("1") != std::string::npos);
    expect_true(json.find("2") != std::string::npos);
    expect_true(json.find("3") != std::string::npos);
    expect_true(json.find("]") != std::string::npos);
}

JAI_TEST(json_nested_objects) {
    json_archive_writer writer(2);
    
    writer.begin_object("Company", 1);
    writer.write_property_name("name");
    writer.write_string("TechCorp");
    
    writer.write_property_name("employees");
    writer.begin_array(2);
    
    // First employee
    writer.begin_object("Person", 1);
    writer.write_property_name("name");
    writer.write_string("Alice");
    writer.write_property_name("age");
    writer.write_int32(30);
    writer.end_object();
    
    // Second employee
    writer.begin_object("Person", 1);
    writer.write_property_name("name");
    writer.write_string("Bob");
    writer.write_property_name("age");
    writer.write_int32(25);
    writer.end_object();
    
    writer.end_array();
    writer.end_object();
    
    std::string json = writer.str();
    
    // Verify nested structure
    expect_true(json.find("\"_type_\": \"Company\"") != std::string::npos);
    expect_true(json.find("\"employees\": [") != std::string::npos);
    expect_true(json.find("Alice") != std::string::npos);
    expect_true(json.find("Bob") != std::string::npos);
}

JAI_TEST(json_special_characters) {
    json_archive_writer writer;
    
    // Test string escaping
    writer.write_string("Hello \"World\"\n\tTab\r\nNewline\\Backslash");
    
    std::string json = writer.str();
    expect_true(json.find("\\\"") != std::string::npos);  // Escaped quote
    expect_true(json.find("\\n") != std::string::npos);   // Escaped newline
    expect_true(json.find("\\t") != std::string::npos);   // Escaped tab
    expect_true(json.find("\\\\") != std::string::npos);  // Escaped backslash
}

JAI_TEST(json_type_aware_serialization) {
    json_archive_writer writer;
    
    // Test that JSON doesn't need type-aware serialization
    // All numeric types become JSON numbers
    auto int8_type = type_info::make_int8();
    auto float32_type = type_info::make_float32();
    
    writer.write_value_typed(script_value(127), int8_type);
    writer.write_value_typed(script_value(3.14f), float32_type);
    
    std::string json = writer.str();
    expect_true(json.find("127") != std::string::npos);
    expect_true(json.find("3.14") != std::string::npos);
}

JAI_TEST(json_vs_binary_size_comparison) {
    // Create same data in both formats
    struct TestData {
        int32_t id = 12345;
        std::string name = "Test Object";
        float value = 3.14159f;
        bool active = true;
    };
    
    // Binary format
    binary_archive_writer bin_writer;
    bin_writer.begin_object("TestData", 1);
    bin_writer.write_property_name("id");
    bin_writer.write_int32(12345);
    bin_writer.write_property_name("name");
    bin_writer.write_string("Test Object");
    bin_writer.write_property_name("value");
    bin_writer.write_float32(3.14159f);
    bin_writer.write_property_name("active");
    bin_writer.write_bool(true);
    bin_writer.end_object();
    
    // JSON format
    json_archive_writer json_writer(0);  // No indentation for fair comparison
    json_writer.begin_object("TestData", 1);
    json_writer.write_property_name("id");
    json_writer.write_int32(12345);
    json_writer.write_property_name("name");
    json_writer.write_string("Test Object");
    json_writer.write_property_name("value");
    json_writer.write_float32(3.14159f);
    json_writer.write_property_name("active");
    json_writer.write_bool(true);
    json_writer.end_object();
    
    size_t binary_size = bin_writer.data().size();
    size_t json_size = json_writer.str().size();
    
    // JSON should be larger than binary (human readable vs compact)
    expect_true(json_size > binary_size);
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()