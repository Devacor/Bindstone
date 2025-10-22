#include "jaiscript/jvm/vm_backend.hpp"
#include "jaiscript/detail/interpreter_backend.hpp"

namespace jai::jvm {

std::unique_ptr<vm_backend> create_vm_backend(string_symbolizer* symbolizer, std::shared_ptr<environment> env) {
    //TODO: Implement full VM backend - currently returns interpreter backend cast as execution_backend
    // This is a temporary fallback for testing until VM implementation is complete
    auto interpreter = std::make_unique<interpreter_backend>(symbolizer, env);
    return std::unique_ptr<vm_backend>(reinterpret_cast<vm_backend*>(interpreter.release()));
}

}