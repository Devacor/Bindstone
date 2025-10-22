#pragma once
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

namespace jai::foundry::tests {

class assignment_tests : public suite {
public:
    assignment_tests() : suite("Assignment Operations") {}
    void forge_tests() override;
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::assignment_tests)