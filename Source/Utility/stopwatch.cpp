#include "stopwatch.h"

namespace MV {
	Stopwatch::TimeType Stopwatch::check(){
		if(paused){
			return pausetime-prevtime + timeOffset;
		}
		return timerfunc(false);
	}

	Stopwatch::TimeType Stopwatch::pause(){
		pausetime = systemTime();
		paused = true;
		return timerfunc(false);
	}

	Stopwatch::TimeType Stopwatch::resume(){
		if(paused){
			prevtime+=systemTime()-pausetime;
		}
		paused = false;
		return timerfunc(false);
	}

	void Stopwatch::start(){
		timerfunc(true);
	}

	Stopwatch::TimeType Stopwatch::stop(){
		started = false;
		if(paused){
			return pausetime-prevtime + timeOffset;
		}else{
			return systemTime()-prevtime + timeOffset;
		}
	}
	
	Stopwatch::TimeType Stopwatch::systemTime() const{
		return static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency());
	}

	Stopwatch::TimeType Stopwatch::timerfunc(bool reset){
		TimeType curtime = systemTime();
		if(!started){
			prevtime=curtime;
			started = true;

			return timeOffset;
		}else{
			TimeType diff=curtime-prevtime;
			if(reset){
				prevtime=curtime;
				for (auto&& deltaValue : deltaVals) {
					deltaValue.second.deltaspot = 0;
				}
			}
			return diff+timeOffset;
		}
	}

	Stopwatch::TimeType Stopwatch::delta(const std::string &deltaName, bool resetDelta, TimeType addOverflow){
		if(deltaVals.find(deltaName) == deltaVals.end()){ //if no delta by that name is found
			deltaVals[deltaName].deltaspot = check();  //set the delta to the current time
			return 0;
		}
		TimeType PreviousTime = deltaVals[deltaName].deltaspot;
		Stopwatch::TimeType CurrentTime = check();
		if(resetDelta){
			deltaVals[deltaName].deltaspot = CurrentTime + addOverflow; //update delta
		}
		return CurrentTime - PreviousTime;
	}

	MV::Stopwatch::TimeType Stopwatch::delta(bool a_resetDelta /*= true*/) {
		return delta("_!_", a_resetDelta);
	}


	void Stopwatch::setTimeOffset( TimeType offsetMilliseconds ){
		timeOffset = offsetMilliseconds;
	}

	Stopwatch::TimeType Stopwatch::getTimeOffset(){
		return timeOffset;
	}

	bool Stopwatch::frame(TimeType timeToWait) {
		return frame("_!_", timeToWait);
	}

	bool Stopwatch::frame(const std::string &deltaName, TimeType timeToWait) {
		if(!isStarted()){
			start();
		}
		auto deltaTime = delta(deltaName, false);
		if(deltaTime >= timeToWait){
			delta(deltaName, true, timeToWait - deltaTime);
			return true;
		}
		return false;
	}
}
