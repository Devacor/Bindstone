#include "stopwatch.h"

namespace MV {
	double Stopwatch::check(){
		if(paused){
			return pausetime-prevtime + timeOffset;
		}
		return timerfunc(false);
	}

	double Stopwatch::check() const {
		if (paused) {
			return pausetime - prevtime + timeOffset;
		} else if(started) {
			return programTime() - prevtime + timeOffset;
		} else {
			return 0.0;
		}
	}

	double Stopwatch::pause(){
		pausetime = programTime();
		paused = true;
		return timerfunc(false);
	}

	double Stopwatch::resume(){
		if(paused){
			prevtime+=programTime()-pausetime;
		}
		paused = false;
		return timerfunc(false);
	}

	void Stopwatch::start(){
		timerfunc(true);
	}

	double Stopwatch::stop(){
		started = false;
		if(paused) {
			return pausetime-prevtime + timeOffset;
		} else {
			return programTime()-prevtime + timeOffset;
		}
	}

	double Stopwatch::reset() {
		auto result = stop();
		start();
		return result;
	}

	double Stopwatch::timerfunc(bool reset){
		double curtime = programTime();
		if(!started) {
			prevtime=curtime;
			started = true;

			return timeOffset;
		} else {
			double diff=curtime-prevtime;
			if(reset){
				prevtime=curtime;
				for (auto&& deltaValue : deltaVals) {
					deltaValue.second = 0.0;
				}
			}
			return diff+timeOffset;
		}
	}

	double Stopwatch::delta(const std::string &deltaName, bool resetDelta, double addOverflow){
		if(deltaVals.find(deltaName) == deltaVals.end()){ //if no delta by that name is found
			deltaVals[deltaName] = check();  //set the delta to the current time
			return 0;
		}
		double PreviousTime = deltaVals[deltaName];
		double CurrentTime = check();
		if(resetDelta){
			deltaVals[deltaName] = CurrentTime + addOverflow; //update delta
		}
		return CurrentTime - PreviousTime;
	}

	double Stopwatch::delta(bool a_resetDelta /*= true*/) {
		return delta("_!_", a_resetDelta);
	}


	void Stopwatch::setTimeOffset(double offsetMilliseconds ){
		timeOffset = offsetMilliseconds;
	}

	double Stopwatch::getTimeOffset(){
		return timeOffset;
	}

	bool Stopwatch::frame(double timeToWait) {
		return frame("_!_", timeToWait);
	}

	bool Stopwatch::frame(const std::string &deltaName, double timeToWait) {
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
