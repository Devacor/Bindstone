#pragma once
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

namespace jai::foundry::tests {

class member_access_tests : public suite {
public:
    member_access_tests() : suite("Member Access Tests") {}
    void forge_tests() override;
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::member_access_tests)