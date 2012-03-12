#include "stopwatch.h"

Int64 M2Util::Stopwatch::check(){
   if(paused){
      return (Int64)(pausetime-prevtime)+timeOffset;
   }
   return timerfunc(false);
}

Int64 M2Util::Stopwatch::pause(){
   Int64 curtime = SDL_GetTicks();
   pausetime = curtime;
   paused = true;
   return timerfunc(false);
}

Int64 M2Util::Stopwatch::resume(){
   Int64 curtime = SDL_GetTicks();
   if(paused){
      prevtime+=curtime-pausetime;
   }
   paused = false;
   return timerfunc(false);
}

void M2Util::Stopwatch::start(){
   timerfunc(true);
}

Int64 M2Util::Stopwatch::stop(){
   started = false;
   if(paused){
      return Int64(pausetime-prevtime)+timeOffset;
   }else{
      return Int64(SDL_GetTicks()-prevtime)+timeOffset;
   }
}

Int64 M2Util::Stopwatch::timerfunc(bool reset){
   Int64 curtime = SDL_GetTicks();
   if(!started){
      prevtime=curtime;
      started = true;
      return (0)+timeOffset;
   }else{
      Int64 diff=curtime-prevtime;
      if(reset){prevtime=curtime;}
      return Int64(diff)+timeOffset;
   }
}

Int64 M2Util::Stopwatch::delta(const std::string &deltaName, bool resetDelta){
   if(deltaVals.find(deltaName) == deltaVals.end()){ //if no delta by that name is found
      deltaVals[deltaName].deltaspot = check();  //set the delta to the current time
      return 0;
   }
   Int64 PreviousTime = deltaVals[deltaName].deltaspot;
   Int64 CurrentTime = check();
   if(resetDelta){
      deltaVals[deltaName].deltaspot = CurrentTime; //update delta
   }
   return CurrentTime - PreviousTime;
}

void M2Util::Stopwatch::setTimeOffset( Int64 offsetMilliseconds ){
   timeOffset = offsetMilliseconds;
}

Int64 M2Util::Stopwatch::getTimeOffset(){
   return timeOffset;
}
