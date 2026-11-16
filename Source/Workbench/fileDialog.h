#ifndef _WORKBENCH_FILEDIALOG_H_
#define _WORKBENCH_FILEDIALOG_H_

#include <string>

namespace Workbench {

	// The platform's own file pickers for scenes and BindSnaps, opening in a_startFolder. Each returns the chosen
	// path, relative to the working directory when the file is under it, or an empty string when the user cancels
	// (or the platform has no picker wired up yet).
	std::string openSceneFileDialog(const std::string& a_startFolder);
	std::string saveSceneFileDialog(const std::string& a_startFolder, const std::string& a_suggestedName);

}

#endif
