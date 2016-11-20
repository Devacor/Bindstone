#include "Utility/chaiscriptStdLib.h"
#include "chaiscript/chaiscript_stdlib.hpp"

namespace MV {
	std::shared_ptr<chaiscript::Module> create_chaiscript_stdlib(){
		return chaiscript::Std_Lib::library();
	}

	std::vector<std::string> chaiscript_module_paths() {
		return{ "" };
	}

	std::vector<std::string> chaiscript_use_paths() {
		return {"./Assets/Interface/", "./Assets/Scripts/"};
	}
}