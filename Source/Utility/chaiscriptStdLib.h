#ifndef _MV_CHAISCRIPT_STDLIB_
#define _MV_CHAISCRIPT_STDLIB_

#include <memory>

namespace chaiscript {
	class Module;
}

//this separation of compilation module improves compile time.
namespace MV {
	std::shared_ptr<chaiscript::Module> create_chaiscript_stdlib();
}

#endif