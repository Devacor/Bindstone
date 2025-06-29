#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jvm/vm_backend.hpp>
#include <iostream>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(VMDebugEnabled)

JAI_TEST(vm_for_loop_with_debug) {
    engine vm_engine;
    
    // Create VM backend and enable debug mode
    auto backend = jvm::create_vm_backend();
    if (auto* vm_backend = dynamic_cast<jvm::vm_backend*>(backend.get())) {
        vm_backend->set_debug_mode(true);
    }
    vm_engine.set_backend(std::move(backend));
    
    std::cout << "\n=== Executing for loop with VM debug mode ===\n";
    
    script_value result = vm_engine.execute(R"(
        var sum = 0;
        for (var i = 0; i < 3; i = i + 1) {
            sum = sum + i;
        }
        sum;
    )");
    
    std::cout << "\nResult type: " << static_cast<int>(result.type());
    if (result.is_int()) {
        std::cout << ", value: " << result.as_int() << "\n";
        expect_eq(result.as_int(), 3);
    } else {
        std::cout << " (not an integer!)\n";
        expect_true(false);
    }
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()