#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/type_info.hpp>

using namespace jai::foundry;
using namespace jai;

namespace jai::foundry::tests {

class type_info_tests : public suite {
public:
    type_info_tests() : suite("Type Info") {}
    
    void forge_tests() override {
        test("recursive_reference_type_creation", [this]() {
            // Test creating a reference type info
            auto int_type = type_info::make_int();
            check(int_type != nullptr);
            
            auto ref_type = type_info::make_reference(int_type);
            check(ref_type != nullptr);
            check(ref_type->is_reference());
            
            // Test creating a reference to a reference (should work)
            auto ref_ref_type = type_info::make_reference(ref_type);
            check(ref_ref_type != nullptr);
            check(ref_ref_type->is_reference());
        });
        
        test("type_info_equality", [this]() {
            // Test that same types are equal
            auto int_type1 = type_info::make_int();
            auto int_type2 = type_info::make_int();
            check_eq(int_type1, int_type2);
            
            // Test that different types are not equal
            auto float_type = type_info::make_float();
            check(int_type1 != float_type);
        });
        
        test("array_type_creation", [this]() {
            auto int_type = type_info::make_int();
            auto array_type = type_info::make_array(int_type);
            
            check(array_type != nullptr);
            check(array_type->is_array());
            check_eq(array_type->element_type(), int_type);
        });
        
        test("map_type_creation", [this]() {
            auto string_type = type_info::make_string();
            auto int_type = type_info::make_int();
            auto map_type = type_info::make_map(string_type, int_type);
            
            check(map_type != nullptr);
            check(map_type->is_map());
            check_eq(map_type->key_type(), string_type);
            check_eq(map_type->value_type(), int_type);
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::type_info_tests)