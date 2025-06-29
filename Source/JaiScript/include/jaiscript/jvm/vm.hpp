#pragma once

// Convenience header that includes all VM components
#include "vm_types.hpp"
#include "bytecode.hpp"
#include "compiler.hpp"
#include "virtual_machine.hpp"
#include "vm_backend.hpp"
#include "vm_executor.hpp"
#include "vm_compiler.hpp"

namespace jai {
namespace jvm {

// Convenience aliases
using vm = virtual_machine;

} // namespace jvm
} // namespace jai