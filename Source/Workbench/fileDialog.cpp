#include "fileDialog.h"
#include <algorithm>
#include <filesystem>
#include <iterator>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#pragma comment(lib, "comdlg32.lib")
#endif

namespace Workbench {

	bool moveToRecycleBin(const std::string& a_path) {
		std::error_code failed;
#ifdef _WIN32
		// the shell wants a full path with back slashes, ended by two nulls
		std::wstring from = std::filesystem::absolute(a_path, failed).make_preferred().wstring();
		from.push_back(L'\0');
		SHFILEOPSTRUCTW operation{};
		operation.wFunc = FO_DELETE;
		operation.pFrom = from.c_str();
		operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
		return SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted;
#else
		return std::filesystem::remove_all(a_path, failed) > 0 && !failed;
#endif
	}

	namespace {
		std::string relativeToWorkingDirectory(const std::filesystem::path& a_chosen) {
			std::error_code ignored;
			auto relative = std::filesystem::relative(a_chosen, std::filesystem::current_path(ignored), ignored);
			if (ignored || relative.empty() || *relative.begin() == "..") {
				return a_chosen.generic_string();
			}
			return relative.generic_string();
		}

#ifdef _WIN32
		const wchar_t* const sceneFilter = L"Scenes and BindSnaps (*.scene;*.bindsnap)\0*.scene;*.bindsnap\0All files (*.*)\0*.*\0";
		const wchar_t* const layoutFilter = L"Workbench layouts (*.bindlayout)\0*.bindlayout\0All files (*.*)\0*.*\0";

		std::string runFileDialog(bool a_save, const std::string& a_startFolder, const std::string& a_suggestedName, const wchar_t* a_filter, const wchar_t* a_extension) {
			wchar_t chosen[MAX_PATH * 4] = {};
			std::wstring suggested = std::filesystem::path(a_suggestedName).wstring();
			suggested.copy(chosen, std::min(suggested.size(), std::size(chosen) - 1));
			std::error_code ignored;
			std::wstring startFolder = std::filesystem::absolute(a_startFolder, ignored).make_preferred().wstring();

			OPENFILENAMEW dialog = {};
			dialog.lStructSize = sizeof(dialog);
			dialog.lpstrFilter = a_filter;
			dialog.lpstrFile = chosen;
			dialog.nMaxFile = static_cast<DWORD>(std::size(chosen));
			dialog.lpstrInitialDir = startFolder.c_str();
			dialog.lpstrDefExt = a_extension;
			// the working directory holds Assets/: the picker must leave it alone
			dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (a_save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
			bool accepted = a_save ? GetSaveFileNameW(&dialog) != 0 : GetOpenFileNameW(&dialog) != 0;
			return accepted ? relativeToWorkingDirectory(std::filesystem::path(chosen)) : std::string();
		}
#else
		const wchar_t* const sceneFilter = L"";
		const wchar_t* const layoutFilter = L"";

		std::string runFileDialog(bool, const std::string&, const std::string&, const wchar_t*, const wchar_t*) {
			return {};
		}
#endif
	}

	std::string openSceneFileDialog(const std::string& a_startFolder) {
		return runFileDialog(false, a_startFolder, "", sceneFilter, L"scene");
	}

	std::string saveSceneFileDialog(const std::string& a_startFolder, const std::string& a_suggestedName) {
		return runFileDialog(true, a_startFolder, a_suggestedName, sceneFilter, L"scene");
	}

	std::string openLayoutFileDialog(const std::string& a_startFolder) {
		return runFileDialog(false, a_startFolder, "", layoutFilter, L"bindlayout");
	}

	std::string saveLayoutFileDialog(const std::string& a_startFolder, const std::string& a_suggestedName) {
		return runFileDialog(true, a_startFolder, a_suggestedName, layoutFilter, L"bindlayout");
	}

}
