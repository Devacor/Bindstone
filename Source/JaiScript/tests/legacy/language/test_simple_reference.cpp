#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(SimpleReference)

JAI_TEST(reference_parameter_basic) {
    engine engine;
    
    // For now, let's just test that reference syntax is parsed
    try {
        engine.execute(R"(
            function test(int& x) {
                x = 100;
            }
        )");
        std::cout << "Reference parameter syntax is parsed correctly\n";
    } catch (const std::exception& e) {
        std::cout << "Failed to parse reference parameter: " << e.what() << "\n";
        expect_true(false);
    }
    
    expect_true(true);
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()