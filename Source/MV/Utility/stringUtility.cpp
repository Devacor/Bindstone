#include "stringUtility.h"
namespace MV {

	std::string toString(wchar_t wc) {
		std::vector<char> c(MB_CUR_MAX);
		mbstate_t ignore;
		memset(&ignore, '\0', sizeof(ignore));
		size_t totalBytes = wcrtomb(&c[0], wc, &ignore);
		std::string result;
		for (size_t i = 0; i < totalBytes; ++i) {
			result += c[i];
		}
		return result;
	}

	wchar_t toWide(char c) {
		wchar_t wc;
		mbtowc(&wc, &c, 1);
		return wc;
	}

	std::string to_string(wchar_t wc) {
		return toString(wc);
	}
	wchar_t to_wide(char c) {
		return toWide(c);
	}

	std::string to_string(const std::wstring& ws) {
		return toString(ws);
	}
	std::wstring to_wide(const std::string& s) {
		return toWide(s);
	}

	std::string toString(const std::wstring& ws) {
		std::string s;
		std::for_each(ws.begin(), ws.end(), [&](const wchar_t& wc) {
			s += toString(wc);
			});
		return s;
	}

	std::wstring toWide(const std::string& s) {
		std::wstring ws;
		std::transform(s.begin(), s.end(), std::back_inserter(ws), [](char c) {
			return toWide(c);
			});
		return ws;
	}

}
