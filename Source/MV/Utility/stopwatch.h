#ifndef __stopwatch_h__
#define __stopwatch_h__
#undef check
#include "SDL.h"
#include <iostream>
#include <string>
#include <map>

namespace MV {
	/*------------------------------*\
	| M2tM's Stopwatch:				  |
	| basically keeps track of time  |
	| and as many deltas as you like |
	\*------------------------------*/

	class Stopwatch{
	public:
		typedef double TimeType;
        
		//starts the timer from 0, restarts the timer if already started
		void start();
		
		//returns the current time delta since it originally was started... OR starts the stopwatch
		//and theoretically returns 0... though it might not if it takes a long time to start.
		TimeType check();
		TimeType check() const;
		
		//returns the current time delta since it originally was started, and pauses the stopwatch
		//at that time
		TimeType pause();
		
		//resumes the stopwatch if it was paused and returns the time much like check
		TimeType resume();
		
		//Stop the timer and return the final time between the start (or the first check) and stop calls.
		TimeType stop();

		//Stop and restart, return the final time between start and reset, or the last reset.
		TimeType reset();
		
		//Checks the time between calls.
		TimeType delta(bool a_resetDelta = true);

		//Ultimate convenience, returns true after a certain amount of time, and resets the delta.
		bool frame(TimeType timeToWait);

		bool frame(const std::string &deltaName, TimeType timeToWait);

		//These values affect the returned time on calls to the base check and stop functions.
		//does not affect delta, but you can always call getTimeOffset manually if you need this.
		//All returned times will have timeOffset added to them.
		void setTimeOffset(TimeType offsetMilliseconds);
		TimeType getTimeOffset();

		//checks the time between calls for delta of a given name while the stopwatch is running.
		//first call returns 0.  Subsequent calls return the time between the current call
		//and the previous call.  The timer should be running or the delta will not work.
		TimeType delta(const std::string &deltaName, bool resetDelta = true, TimeType addOverflow = 0.0);
		void clearDeltas(){deltaVals.clear();}
		
		bool isStarted(){return started;}
		bool isPaused(){return paused;}
		
		static TimeType systemTime();
	private:
		TimeType timerfunc(bool reset);
		TimeType prevtime = 0;
		TimeType pausetime = 0;
		TimeType timeOffset = 0;
        bool started = false;
        bool paused = false;
        bool firstCall = false;
		class TimeDelta{
		public:
			TimeDelta():
				deltaspot(0){
			}
			TimeType deltaspot;
		};
		std::map<std::string, TimeDelta> deltaVals;
	};
}
#endif