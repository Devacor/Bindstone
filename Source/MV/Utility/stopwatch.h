#ifndef __stopwatch_h__
#define __stopwatch_h__
#undef check
#include <iostream>
#include <string>
#include <unordered_map>
#include <chrono>

namespace MV {
	/*------------------------------*\
	| M2tM's Stopwatch:				  |
	| basically keeps track of time  |
	| and as many deltas as you like |
	\*------------------------------*/

	class Stopwatch{
	public:
		//starts the timer from 0, restarts the timer if already started
		void start();
		
		//returns the current time delta since it originally was started... OR starts the stopwatch
		//and theoretically returns 0... though it might not if it takes a long time to start.
		double check();
		double check() const;
		
		//returns the current time delta since it originally was started, and pauses the stopwatch
		//at that time
		double pause();
		
		//resumes the stopwatch if it was paused and returns the time much like check
		double resume();
		
		//Stop the timer and return the final time between the start (or the first check) and stop calls.
		double stop();

		//Stop and restart, return the final time between start and reset, or the last reset.
		double reset();
		
		//Checks the time between calls.
		double delta(bool a_resetDelta = true);

		//Ultimate convenience, returns true after a certain amount of time, and resets the delta.
		bool frame(double timeToWait);

		bool frame(const std::string &deltaName, double timeToWait);

		//These values affect the returned time on calls to the base check and stop functions.
		//does not affect delta, but you can always call getTimeOffset manually if you need this.
		//All returned times will have timeOffset added to them.
		void setTimeOffset(double offsetMilliseconds);
		double getTimeOffset();

		//checks the time between calls for delta of a given name while the stopwatch is running.
		//first call returns 0.  Subsequent calls return the time between the current call
		//and the previous call.  The timer should be running or the delta will not work.
		double delta(const std::string &deltaName, bool resetDelta = true, double addOverflow = 0.0);
		void clearDeltas(){deltaVals.clear();}
		
		bool isStarted(){return started;}
		bool isPaused(){return paused;}
		
		static inline double programTime() {
			static std::chrono::high_resolution_clock::time_point programStartTime = std::chrono::high_resolution_clock::now();
			return std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - programStartTime).count();
		}

		static inline int64_t systemTimeNano() {
			return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
		}

		static inline int64_t systemTimeMilli() {
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
		}

		static inline int64_t systemTimeSeconds() {
			return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
		}
	private:
		double timerfunc(bool reset);
		double prevtime = 0;
		double pausetime = 0;
		double timeOffset = 0;
		bool started = false;
		bool paused = false;
		bool firstCall = false;

		std::unordered_map<std::string, double> deltaVals;
	};
}
#endif
