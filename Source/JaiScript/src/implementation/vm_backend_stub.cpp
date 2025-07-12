#include "../../include/jaiscript/jvm/vm_backend.hpp"

namespace jai {
namespace jvm {

std::unique_ptr<vm_backend> create_vm_backend(string_symbolizer* symbolizer, std::shared_ptr<environment> env) {
    throw std::runtime_error("VM backend not available in this build");
}

} // namespace jvm
} // namespace jai