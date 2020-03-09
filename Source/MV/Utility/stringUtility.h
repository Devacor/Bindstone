#ifndef _MV_STRING_UTILITY_H_
#define _MV_STRING_UTILITY_H_

#include <string>
#include <vector>
#include <algorithm>
#include "MV/Utility/require.hpp"

namespace MV {

	struct StringContains {
		StringContains(const std::string& a_input) :characters(a_input) {}
		bool operator()(const char& c) const {
			return characters.find(c, 0) != std::string::npos;
		}
		std::string characters;
	};

	template <typename T>
	std::vector<std::string> explode(const std::string& a_input, T a_pred) {
		std::vector<std::string> result{};
		if (a_input.empty()) { return result; }
		result.push_back("");
		for (char c : a_input) {
			if (a_pred(c)) {
				result.push_back("");
			}
			else {
				result.back().push_back(c);
			}
		}
		return result;
	}

	inline std::string toLower(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](char c) { return std::tolower(c); });
		return s;
	}

	inline std::string toUpper(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](char c) { return std::toupper(c); });
		return s;
	}

	inline bool startsWith(const std::string& a_str, const std::string& a_prefix) {
		if (a_prefix.size() > a_str.size()) { return false; }
		return a_str.compare(0, a_prefix.size(), a_prefix);
	}

	inline void toLowerInPlace(std::string& s) {
		std::transform(s.begin(), s.end(), s.begin(), [](char c) { return std::tolower(c); });
	}

	inline void toUpperInPlace(std::string& s) {
		std::transform(s.begin(), s.end(), s.begin(), [](char c) { return std::toupper(c); });
	}

	inline std::string toUpperFirstChar(std::string s) {
		if (s.empty()) { return {}; }
		s[0] = std::toupper(s[0]);
		return s;
	}

	inline bool replaceFirst(std::string& str, const std::string& from, const std::string& to) {
		size_t start_pos = str.find(from);
		if (start_pos == std::string::npos)
			return false;
		str.replace(start_pos, from.length(), to);
		return true;
	}

	inline bool replaceAll(std::string& str, const std::string& from, const std::string& to) {
		bool replaced = false;
		while (replaceFirst(str, from, to)) {
			replaced = true;
		}
		return replaced;
	}

	inline std::string filenameFromPath(const std::string& a_path) {
		auto backslashLoc = a_path.rfind("\\");
		auto forwardslashLoc = a_path.rfind("/");
		size_t slashLoc = 0;
		if (backslashLoc != std::string::npos && forwardslashLoc != std::string::npos) {
			slashLoc = std::max(backslashLoc, forwardslashLoc) + 1;
		}
		else if (backslashLoc != std::string::npos || forwardslashLoc != std::string::npos) {
			slashLoc = std::min(backslashLoc, forwardslashLoc) + 1;
		}
		return a_path.substr(slashLoc, a_path.length() - slashLoc);
	}

	std::string toString(wchar_t wc);
	wchar_t toWide(char c);

	std::string to_string(wchar_t wc);
	wchar_t to_wide(char c);

	std::string toString(const std::wstring& ws);
	std::wstring toWide(const std::string& s);

	std::string to_string(const std::wstring& ws);
	std::wstring to_wide(const std::string& s);

	std::istream& getline_platform_agnostic(std::istream& is, std::string& t);
}

#endif
