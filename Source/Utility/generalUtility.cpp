#include "generalUtility.h"
#include <iterator>
#include <vector>
#include <cmath>
#include <map>
#include <boost/lexical_cast.hpp>
#include <algorithm>

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
		return a_baseName+std::to_string(counters[a_baseName]++);
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

	std::string wideToChar(UtfChar wc){
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

	UtfChar charToWide(char c){
		wchar_t wc;
		mbtowc(&wc, &c, 1);
		return wc;
	}

	std::string wideToString(const UtfString& ws){
		std::string s;
		std::for_each(ws.begin(), ws.end(), [&](const UtfChar &wc){
			s+=wideToChar(wc);
		});
		return s;
	}

	UtfString stringToWide(const std::string& s){
		UtfString ws;
		std::transform(s.begin(), s.end(), std::back_inserter(ws), charToWide);
		return ws;
	}


	int boundBetween(int val, int lowerBound, int upperBound){
		return static_cast<int>(boundBetween(static_cast<long>(val), static_cast<long>(lowerBound), static_cast<long>(upperBound)));
	}

	long boundBetween(long val, long lowerBound, long upperBound){
		if(lowerBound > upperBound){std::swap(lowerBound, upperBound);}
		long rangeSize = upperBound - lowerBound;

		if (val < lowerBound)
			val += rangeSize * ((lowerBound - val) / rangeSize + 1);

		return lowerBound + (val - lowerBound) % rangeSize;

	}

	float boundBetween(float val, float lowerBound, float upperBound){
		return static_cast<float>(boundBetween(static_cast<double>(val), static_cast<double>(lowerBound), static_cast<double>(upperBound)));
	}

	double boundBetween(double val, double lowerBound, double upperBound){
		if(lowerBound > upperBound){std::swap(lowerBound, upperBound);}
		val-=lowerBound; //adjust to 0
		double rangeSize = upperBound - lowerBound;
		if(rangeSize == 0){return upperBound;} //avoid dividing by 0
		return val - (rangeSize * std::floor(val/rangeSize)) + lowerBound;
	}

}
