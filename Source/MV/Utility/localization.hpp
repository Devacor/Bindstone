/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

#ifndef _MV_LOCALIZTION_H_
#define _MV_LOCALIZTION_H_

#include <map>
#include <vector>
#include <algorithm>

#include "MV/Utility/generalUtility.h"
#include "MV/Utility/signal.hpp"

namespace MV {
	class Localization {
	public:
		bool setLanguage(const std::string &language) {
			int languageIndex = std::distance(languages.begin(), std::find(languages.begin(), languages.end(), toLower(language)));
			if (languageIndex > languages.size()) {
				languageIndex = 0;
			}
		}

		template <typename ...T>
		std::string get(const std::string &key, )

	private:
		std::vector<std::string> languages;
		std::map<std::string, std::vector<std::string>> localizedKeys;
	};
}
#endif