// Single compilation unit for JaiScript
// This file includes all implementation files

#include "../include/jaiscript/jaiscript.hpp"

// Include all implementation files
#include "implementation/types.cpp"
#include "implementation/type_info.cpp"
#include "implementation/value.cpp"
#include "implementation/engine.cpp"
#include "implementation/lexer.cpp"
#include "implementation/parser.cpp"
#include "implementation/interpreter.cpp"
#include "implementation/runtime_errors.cpp"
#include "implementation/conversion_registry.cpp"
#include "implementation/class_registry.cpp"
#include "implementation/parse_errors.cpp"
#include "implementation/interpreter_dispatch.cpp"
#include "implementation/vm_backend_stub.cpp"