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
#else
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


	void systemSleep(int time){
#ifdef __APPLE__
		sleep(time);
#else
		Sleep(time);
#endif
	}

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

	std::string toString(UtfChar wc){
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

	UtfChar toWide(char c){
		wchar_t wc;
		mbtowc(&wc, &c, 1);
		return wc;
	}

	std::string to_string(UtfChar wc) {
		return toString(wc);
	}
	UtfChar to_wide(char c) {
		return toWide(c);
	}

	std::string to_string(const UtfString& ws) {
		return toString(ws);
	}
	UtfString to_wide(const std::string& s) {
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

	std::string toString(const UtfString& ws) {
		std::string s;
		std::for_each(ws.begin(), ws.end(), [&](const UtfChar &wc){
			s+=toString(wc);
		});
		return s;
	}

	UtfString toWide(const std::string& s){
		UtfString ws;
		std::transform(s.begin(), s.end(), std::back_inserter(ws), [](char c){
			return toWide(c);
		});
		return ws;
	}


	int wrap(int lowerBound, int upperBound, int val){
		return static_cast<int>(wrap(static_cast<long>(lowerBound), static_cast<long>(upperBound), static_cast<long>(val)));
	}

	long wrap(long lowerBound, long upperBound, long val){
		using std::swap;
		if(lowerBound > upperBound){swap(lowerBound, upperBound);}
		long rangeSize = upperBound - lowerBound;

		if (val < lowerBound)
			val += rangeSize * ((lowerBound - val) / rangeSize + 1);

		return lowerBound + (val - lowerBound) % rangeSize;
	}

	float wrap(float lowerBound, float upperBound, float val){
		return static_cast<float>(wrap(static_cast<double>(lowerBound), static_cast<double>(upperBound), static_cast<double>(val)));
	}

	double wrap(double lowerBound, double upperBound, double val){
		using std::swap;
		if(lowerBound > upperBound){swap(lowerBound, upperBound);}
		val-=lowerBound; //adjust to 0
		double rangeSize = upperBound - lowerBound;
		if(rangeSize == 0){return upperBound;} //avoid dividing by 0
		return val - (rangeSize * std::floor(val/rangeSize)) + lowerBound;
	}

	Random* Random::instance = nullptr;

	double randomNumber(double a_min, double a_max) {
		if(!Random::instance){
			Random::instance = new Random();
		}
		return Random::instance->number(a_min, a_max);
	}

	float randomNumber(float a_min, float a_max) {
		if(!Random::instance){
			Random::instance = new Random();
		}
		return Random::instance->number(a_min, a_max);
	}

	int64_t randomInteger(int64_t a_min, int64_t a_max) {
		if(!Random::instance){
			Random::instance = new Random();
		}
		return Random::instance->integer(a_min, a_max);
	}

}
