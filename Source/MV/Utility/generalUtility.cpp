#include "generalUtility.h"
#include <iterator>
#include <vector>
#include <cmath>
#include <map>
#include <algorithm>
#include <fstream>

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

	std::string toString(wchar_t wc){
		std::vector<char> c(MB_CUR_MAX);
		mbstate_t ignore;
		memset (&ignore, '\0', sizeof (ignore));
		size_t totalBytes = wcrtomb(&c[0], wc, &ignore);
		std::string result;
		for(size_t i = 0;i < totalBytes;++i){
			result+=c[i];
		}
		return result;
	}

	wchar_t toWide(char c){
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

	bool fileExists(const std::string& name) {
		if (FILE *file = fopen(name.c_str(), "r")) {
			fclose(file);
			return true;
		} else {
			return false;
		}
	}

	std::string fileContents(const std::string& a_path) {
		std::ifstream stream(a_path);
		return stream ? std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>()) : "";
	}

	std::string toString(const std::wstring& ws) {
		std::string s;
		std::for_each(ws.begin(), ws.end(), [&](const wchar_t &wc){
			s+=toString(wc);
		});
		return s;
	}

	std::wstring toWide(const std::string& s){
		std::wstring ws;
		std::transform(s.begin(), s.end(), std::back_inserter(ws), [](char c){
			return toWide(c);
		});
		return ws;
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