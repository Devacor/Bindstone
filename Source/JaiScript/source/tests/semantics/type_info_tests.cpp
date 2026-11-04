#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/type_info.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai::foundry;
using namespace jai;

namespace jai::foundry::tests {

class type_info_tests : public suite {
public:
    type_info_tests() : suite("Type Info") {}

    void forge_tests() override {
        test("recursive_reference_type_creation", [this]() {
            // Create engine to get type_info objects
            auto eng = make_engine();

            // Test creating a reference type info
            auto int_type = detail::engine_access::get_type_info_int(*eng);
            check(int_type != nullptr);

            auto ref_type = detail::engine_access::get_type_info_reference(*eng, int_type);
            check(ref_type != nullptr);
            check(ref_type->is_reference());

            // Test creating a reference to a reference (should work)
            auto ref_ref_type = detail::engine_access::get_type_info_reference(*eng, ref_type);
            check(ref_ref_type != nullptr);
            check(ref_ref_type->is_reference());
        });

        test("type_info_equality", [this]() {
            // Create engine to get type_info objects
            auto eng = make_engine();

            // Test that same types are equal (should return same pointer due to interning)
            auto int_type1 = detail::engine_access::get_type_info_int(*eng);
            auto int_type2 = detail::engine_access::get_type_info_int(*eng);
            check_eq(int_type1, int_type2);

            // Test that different types are not equal
            auto float_type = detail::engine_access::get_type_info_float(*eng);
            check(int_type1 != float_type);
        });

        test("array_type_creation", [this]() {
            // Create engine to get type_info objects
            auto eng = make_engine();

            auto int_type = detail::engine_access::get_type_info_int(*eng);
            auto array_type = detail::engine_access::get_type_info_array(*eng, int_type);

            check(array_type != nullptr);
            check(array_type->is_array());
            check_eq(array_type->element_type().get(), int_type);
        });

        test("map_type_creation", [this]() {
            // Create engine to get type_info objects
            auto eng = make_engine();

            auto string_type = detail::engine_access::get_type_info_string(*eng);
            auto int_type = detail::engine_access::get_type_info_int(*eng);
            auto map_type = detail::engine_access::get_type_info_map(*eng, string_type, int_type);

            check(map_type != nullptr);
            check(map_type->is_map());
            check_eq(map_type->key_type().get(), string_type);
            check_eq(map_type->value_type().get(), int_type);
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::type_info_tests)