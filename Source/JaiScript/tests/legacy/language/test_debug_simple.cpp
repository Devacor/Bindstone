#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(DebugSimple)

JAI_TEST(no_custom_subscript) {
    engine engine;
    
    // Just test built-in array subscript without any custom functions
    engine.execute("var arr = [10, 20, 30]; var a1 = arr[1];");
    expect_eq(engine.get_variable("a1").as<int>(), 20);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()