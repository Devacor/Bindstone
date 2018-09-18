#include "MV/Utility/chaiscriptStdLib.h"
#include "chaiscript/chaiscript_stdlib.hpp"

namespace MV {
	std::vector<std::string> chaiscript_module_paths() {
		return{ "" };
	}

	std::vector<std::string> chaiscript_use_paths() {
		return {"./Assets/Interface/", "./Assets/Scripts/", ""};
	}
}