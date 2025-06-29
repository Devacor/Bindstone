#pragma once

#include "virtual_machine.hpp"

namespace jai {
namespace jvm {

// Alias for consistency with test expectations
using vm_executor = jai::jvm::virtual_machine;

// VM execution context (used by function executor)
using vm_context = jai::jvm::execution_context;

} // namespace jvm
} // namespace jai