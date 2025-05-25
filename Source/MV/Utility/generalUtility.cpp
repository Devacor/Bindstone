#include "generalUtility.h"
#include <iterator>
#include <vector>
#include <cmath>
#include <map>
#include <algorithm>
#include <fstream>
#include "SDL.h"
#include "scopeGuard.hpp"
#include <filesystem>
#include "stringUtility.h"
#include "log.h"

#ifndef APPLICATION_ORG
#define APPLICATION_ORG "SnapJaw"
#endif

#ifndef APPLICATION_NAME
#define APPLICATION_NAME "Bindstone"
#endif

#ifdef __APPLE__
	#include "CoreFoundation/CoreFoundation.h"
	#include <assert.h>
	#include <stdbool.h>
	#include <sys/types.h>
	#include <unistd.h>
	#include <sys/sysctl.h>
#endif

#ifdef _WIN32
	#include <windows.h>
#endif

namespace MV {
#ifdef __APPLE__
	//from http://developer.apple.com/library/mac/#qa/qa1361/_index.html
	static bool AmIBeingDebugged(void)
	 // Returns true if the current process is being debugged (either
	 // running under the debugger or has a debugger attached post facto).
	{
		int					  junk;
		int					  mib[4];
		struct kinfo_proc	info;
		size_t				  size;

		// Initialize the flags so that, if sysctl fails for some bizarre
		// reason, we get a predictable result.

		info.kp_proc.p_flag = 0;

		// Initialize mib, which tells sysctl the info we want, in this case
		// we're looking for information about a specific process ID.

		mib[0] = CTL_KERN;
		mib[1] = KERN_PROC;
		mib[2] = KERN_PROC_PID;
		mib[3] = getpid();

		// Call sysctl.

		size = sizeof(info);
		junk = sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);
		assert(junk == 0);

		// We're being debugged if the P_TRACED flag is set.

		return ( (info.kp_proc.p_flag & P_TRACED) != 0 );
	}
#endif

	void initializeFilesystem(){
#ifdef __APPLE__
		//from http://stackoverflow.com/questions/516200/relative-paths-not-working-in-xcode-c
		//I am not entirely sure I need this yet.  May get rid of it in the future.
		//Editing schemes allows me to set the file system structure I want when debugging and I will likely
		//need to compile things into the bundle later anyway thereby making this unhelpful.
		if(!AmIBeingDebugged()){
			CFBundleRef mainBundle = CFBundleGetMainBundle();
			CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
			char path[PATH_MAX];
			if (!CFURLGetFileSystemRepresentation(resourcesURL, TRUE, (UInt8 *)path, PATH_MAX))
			{
				// error!
			}
			CFRelease(resourcesURL);

			chdir(path);
			std::cout << "Current Path: " << path << std::endl;
		}
#endif
	}

	std::string guid(std::string a_baseName){
		static std::map<std::string, int64_t> counters;
		return a_baseName+'_'+std::to_string(counters[a_baseName]++);
	}

	int roundUpPowerOfTwo(int num){
		if(num<1){return 0;}
		//make sure num isn't already a power of two
		if (isPowerOfTwo(num)){return num;}
		int counter = 0;
		while(num){
			num=num>>1;
			counter++;
		}
		num = 1;
		num=num<<counter;
		return num;
	}

	bool isPowerOfTwo(int num){
		return num>0 && ((num & (num-1))==0);
	}

	float distance(const float &x1, const float &y1, const float &x2, const float &y2) {
		float deltaX = x1 - x2, deltaY = y1 - y2;
		if (deltaX < 0) { deltaX *= -1; }
		if (deltaY < 0) { deltaY *= -1; }
		return sqrt(((deltaX)*(deltaX)) + ((deltaY)*(deltaY)));
	}

	double distance(const double &x1, const double &y1, const double &x2, const double &y2) {
		double deltaX = x1 - x2, deltaY = y1 - y2;
		if (deltaX < 0) { deltaX *= -1; }
		if (deltaY < 0) { deltaY *= -1; }
		return sqrt(((deltaX)*(deltaX)) + ((deltaY)*(deltaY)));
	}

	std::string fileNameFromPath(std::string a_path, bool a_includeExtension /*= false*/) {
		const size_t last_slash_idx = a_path.find_last_of("\\/");
		if (last_slash_idx != std::string::npos) {
			a_path.erase(0, last_slash_idx + 1);
		}

		if (!a_includeExtension) {
			const size_t period_idx = a_path.rfind('.');
			if (period_idx != std::string::npos) {
				a_path.erase(period_idx);
			}
		}
		return a_path;
	}

	const std::string& playerPreferencesPath() {
		static const std::string path = SDL_GetPrefPath(APPLICATION_ORG, APPLICATION_NAME);
		return path;
	}

	bool fileExistsAbsolute(const std::string& a_path) {
		if (a_path.empty()) {
			return false;
		}
#if defined(__ANDROID__) || defined(__APPLE__)
		SDL_RWops* sdlIO = SDL_RWFromFile(a_path.c_str(), "rb");
		if (sdlIO != NULL)
		{
			SDL_RWclose(sdlIO);
			return true;
		}
		return false;
#else
		return std::filesystem::exists(a_path);
#endif
	}

	bool fileExistsInSearchPaths(const std::string& a_path) {
		std::string userPrefPath = playerPreferencesPath() + a_path;
#if defined(__ANDROID__) || defined(__APPLE__)
		return fileExistsAbsolute(a_path) || fileExistsAbsolute(userPrefPath);
#else
		return fileExistsAbsolute(a_path) || fileExistsAbsolute(userPrefPath) || fileExistsAbsolute("Assets/" + a_path);
#endif
	}

	SDL_RWops* sdlFileHandle(const std::string& a_path) {
		if (a_path.empty()) {
			return {};
		}
		std::string userPrefPath = playerPreferencesPath() + a_path;
#if defined(__ANDROID__) || defined(__APPLE__)
		SDL_RWops* sdlIO = SDL_RWFromFile(fileExistsAbsolute(userPrefPath) ? userPrefPath.c_str() : a_path.c_str(), "rb");
#else
		//supports absolute paths too.
		SDL_RWops* sdlIO = SDL_RWFromFile(fileExistsAbsolute(a_path) ? a_path.c_str() : fileExistsAbsolute(userPrefPath) ? userPrefPath.c_str() : ("Assets/" + a_path).c_str(), "rb");
#endif
		return sdlIO;
	}

	std::string fileContents(const std::string& a_path, bool a_throwOnMissing) {
		SDL_RWops* sdlIO = sdlFileHandle(a_path);
		if (sdlIO == nullptr) {
			if (a_throwOnMissing) {
				throw MV::ResourceException("Missing file at path: "s + a_path);
			}
			return {};
		}
		SCOPE_EXIT{ SDL_RWclose(sdlIO); };
		auto totalSize = SDL_RWsize(sdlIO);

		std::string data;
		data.resize(totalSize);
		SDL_RWread(sdlIO, &data[0], totalSize, sizeof(data[0]));

		return data;
	}

	bool writeToFile(const std::string& a_path, const std::string& a_contents) {
		std::filesystem::path finalPath = std::filesystem::path(a_path).is_absolute() ? a_path : playerPreferencesPath() + a_path;
		std::filesystem::create_directories(finalPath.parent_path());
#if defined(__ANDROID__) || defined(__APPLE__)
		SDL_RWops* rw = SDL_RWFromFile(finalPath.c_str(), "wb");
		if (rw != NULL) {
			SCOPE_EXIT{ SDL_RWclose(rw); };
			if (SDL_RWwrite(rw, a_contents.c_str(), 1, a_contents.size()) == a_contents.size()) {
				MV::info("Saving File: ", finalPath);
				return true;
			}
		}
		MV::error("Failed to Save File: ", finalPath);
		return false;
#else
		std::ofstream ofs(finalPath, std::ios::binary);
		if (ofs.is_open()) {
			ofs.write(a_contents.c_str(), a_contents.size());
			ofs.close();
			MV::info("Saving File: ", finalPath);
			return ofs.good();
		} else {
			MV::error("Failed to Save File: ", finalPath);
			return false;
		}
#endif
	}

	bool deleteFile(const std::string& a_path) {
		return std::filesystem::remove(playerPreferencesPath() + a_path);
	}

	time_t lastFileWriteTime(const std::string& a_path) {
#if defined(__ANDROID__) || defined(__APPLE__)
		return fileExistsInSearchPaths(a_path) ? 1 : 0;
#else
		std::string userPrefPath = playerPreferencesPath() + a_path;
		std::error_code errorCode;
		auto writeTime = std::filesystem::last_write_time(a_path, errorCode);
		if (errorCode) {
			writeTime = std::filesystem::last_write_time(userPrefPath, errorCode);
			if (errorCode) {
				writeTime = std::filesystem::last_write_time("Assets/" + a_path, errorCode);
			}
		}
		return writeTime.time_since_epoch().count();
#endif
	}

	Random* Random::instance = nullptr;

	double randomNumber(double a_min, double a_max) {
		return Random::global()->number(a_min, a_max);
	}

	float randomNumber(float a_min, float a_max) {
		return Random::global()->number(a_min, a_max);
	}

	int64_t randomInteger(int64_t a_min, int64_t a_max) {
		return Random::global()->integer(a_min, a_max);
	}

	std::string randomString(std::string a_charset, size_t a_length) {
		return Random::global()->randomString(a_charset, a_length);
	}

	std::string randomString(size_t a_length) {
		return randomString("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_", a_length);
	}

}
