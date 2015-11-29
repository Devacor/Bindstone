#include "Utility/chaiscriptStdLib.h"
#include "chaiscript/chaiscript_stdlib.hpp"

namespace MV {
	std::shared_ptr<chaiscript::Module> create_chaiscript_stdlib(){
		return chaiscript::Std_Lib::library();
	}
}