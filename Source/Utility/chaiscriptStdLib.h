#ifndef _MV_CHAISCRIPT_STDLIB_
#define _MV_CHAISCRIPT_STDLIB_

#include <memory>
#include <string>
#include <vector>

namespace chaiscript {
	class Module;
}

//this separation of compilation module improves compile time.
namespace MV {
	std::shared_ptr<chaiscript::Module> create_chaiscript_stdlib();
	std::vector<std::string> chaiscript_module_paths();
	std::vector<std::string> chaiscript_use_paths();
}

#endif