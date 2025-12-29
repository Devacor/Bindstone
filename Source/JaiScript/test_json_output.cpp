#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/properties/macros.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <iostream>

using namespace jai;

class test_object : public property_owner<test_object> {
public:
    JAI_PROPERTY((int), health, 100);
    JAI_PROPERTY((float), speed, 5.5f);
};

int main() {
    test_object obj;
    obj.health = 42;
    obj.speed = 3.14f;
    
    serialization::json_archive_writer writer;
    writer.begin_object("test_object", 1);
    obj.property_mgr.save(writer);
    writer.end_object();
    
    std::cout << writer.str() << std::endl;
    return 0;
}
