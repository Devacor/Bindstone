#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;
using namespace jai::test;

// Test class with various properties
class Person {
public:
    std::string name;
    int age;
    double height;
    bool active;
    
    Person() : name("Unknown"), age(0), height(0.0), active(false) {}
    Person(const std::string& n, int a, double h, bool act) 
        : name(n), age(a), height(h), active(act) {}
    
    std::string get_name() const { return name; }
    void setName(const std::string& n) { name = n; }
    int getAge() const { return age; }
    void setAge(int a) { age = a; }
    
    std::string to_string() const {
        return "Person(" + name + ", " + std::to_string(age) + ")";
    }
};

// Nested class to test object serialization
class Address {
public:
    std::string street;
    std::string city;
    int zipCode;
    
    Address() : street(""), city(""), zipCode(0) {}
    Address(const std::string& s, const std::string& c, int z) 
        : street(s), city(c), zipCode(z) {}
};

class Employee {
public:
    Person person;
    Address address;
    double salary;
    
    Employee() : salary(0.0) {}
    Employee(const Person& p, const Address& a, double s) 
        : person(p), address(a), salary(s) {}
};

JAI_TEST_SUITE(JSONCppBoundTests)

JAI_TEST(to_json_bound_class) {
    engine engine;
    stdlib::register_all(engine);
    
    // Register Person class
    make_class_builder<Person>(engine, "Person")
        .constructor<>()
        .constructor<const std::string&, int, double, bool>()
        .property("name", &Person::name)
        .property("age", &Person::age)
        .property("height", &Person::height)
        .property("active", &Person::active)
        .method("to_string", &Person::to_string)
        .build();
    
    // Create and test a Person object
    script_value result = engine.execute(R"(
        var person = Person("John Doe", 30, 5.9, true);
        to_json(person)
    )");
    
    std::string json = result.as_string();
    
    // Should contain the type and all properties
    expect_true(json.find("\"_type_\": \"Person\"") != std::string::npos);
    expect_true(json.find("\"name\": \"John Doe\"") != std::string::npos);
    expect_true(json.find("\"age\": 30") != std::string::npos);
    expect_true(json.find("\"height\": 5.9") != std::string::npos);
    expect_true(json.find("\"active\": true") != std::string::npos);
}

JAI_TEST(to_json_nested_bound_classes) {
    engine engine;
    stdlib::register_all(engine);
    
    // Register Address class
    make_class_builder<Address>(engine, "Address")
        .constructor<>()
        .constructor<const std::string&, const std::string&, int>()
        .property("street", &Address::street)
        .property("city", &Address::city)
        .property("zipCode", &Address::zipCode)
        .build();
    
    // Register Person class  
    make_class_builder<Person>(engine, "Person")
        .constructor<const std::string&, int, double, bool>()
        .property("name", &Person::name)
        .property("age", &Person::age)
        .build();
        
    // Register Employee class
    make_class_builder<Employee>(engine, "Employee")
        .constructor<>()
        .property("person", &Employee::person)
        .property("address", &Employee::address)
        .property("salary", &Employee::salary)
        .build();
    
    // Test nested object serialization
    script_value result = engine.execute(R"(
        var emp = Employee();
        emp.person = Person("Jane Smith", 28, 5.6, true);
        emp.address = Address("123 Main St", "New York", 10001);
        emp.salary = 75000.0;
        to_json(emp, 2)
    )");
    
    std::string json = result.as_string();
    
    // Should contain nested structure
    expect_true(json.find("\"_type_\": \"Employee\"") != std::string::npos);
    expect_true(json.find("\"person\":") != std::string::npos);
    expect_true(json.find("\"address\":") != std::string::npos);
    expect_true(json.find("\"salary\": 75000.0") != std::string::npos);
}

JAI_TEST(mixed_json_with_bound_classes) {
    engine engine;
    stdlib::register_all(engine);
    
    // Register Person class
    make_class_builder<Person>(engine, "Person")
        .constructor<const std::string&, int, double, bool>()
        .property("name", &Person::name)
        .property("age", &Person::age)
        .build();
    
    // Test mixing bound objects with native types
    script_value result = engine.execute(R"(
        var data = {
            "title": "Employee List",
            "count": 2,
            "employees": [
                Person("Alice", 25, 5.5, true),
                Person("Bob", 32, 6.0, false)
            ],
            "metadata": {
                "version": 1.0,
                "updated": true
            }
        };
        to_json(data)
    )");
    
    std::string json = result.as_string();
    
    // Should contain mixed structure
    expect_true(json.find("\"title\":\"Employee List\"") != std::string::npos);
    expect_true(json.find("\"count\":2") != std::string::npos);
    expect_true(json.find("\"_type_\":\"Person\"") != std::string::npos);
    expect_true(json.find("\"Alice\"") != std::string::npos);
    expect_true(json.find("\"Bob\"") != std::string::npos);
}

JAI_TEST(from_json_to_map_with_bound_classes) {
    engine engine;
    stdlib::register_all(engine);
    
    // Register Person class
    make_class_builder<Person>(engine, "Person")
        .constructor<const std::string&, int, double, bool>()
        .property("name", &Person::name)
        .property("age", &Person::age)
        .build();
    
    // Note: from_json returns maps/arrays, not bound objects
    // This tests that serialized bound objects can be read back as maps
    script_value result = engine.execute(R"(
        var person = Person("Test User", 40, 6.1, true);
        var json = to_json(person);
        var data = from_json(json);
        data
    )");
    
    expect_true(result.is_map());
    auto& map = result.as_map();
    
    // Should have the type info and properties as a map
    expect_eq(map.at(script_value("_type_")).as_string(), "Person");
    expect_eq(map.at(script_value("name")).as_string(), "Test User");
    expect_eq(map.at(script_value("age")).as_int(), 40);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()