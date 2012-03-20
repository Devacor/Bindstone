#ifndef __stopwatch_h__
#define __stopwatch_h__
#include "SDL/SDL.h"
#include <iostream>
#include <string>
#include <map>

typedef int64_t Int64;

namespace MV {
   /*------------------------------*\
   | M2tM's Stopwatch:              |
   | basically keeps track of time  |
   | and as many deltas as you like |
   \*------------------------------*/

   class Stopwatch{
   public:
      Stopwatch(){timeOffset = 0; prevtime = 0; pausetime = 0; firstCall = true; started = false; paused = false;}
      ~Stopwatch(){}
      
      //starts the timer without returning a value, restarts the timer if already started
      void start();
      
      //returns the current time delta since it originally was started... OR starts the stopwatch
      //and theoretically returns 0... though it might not if it takes a long time to start.
      Int64 check();
      
      //returns the current time delta since it originally was started, and pauses the stopwatch
      //at that time
      Int64 pause();
      
      //resumes the stopwatch if it was paused and returns the time much like check
      Int64 resume();
      
      //Stop the timer and return the final time between the start (or the first check) and stop calls.
      Int64 stop();
      
      //These values affect the returned time on calls to the base check and stop functions.
      //does not affect delta, but you can always call getTimeOffset manually if you need this.
      //All returned times will have timeOffset added to them.
      void setTimeOffset(Int64 offsetMilliseconds);
      Int64 getTimeOffset();

      //checks the time between calls for delta of a given name while the stopwatch is running.
      //first call returns 0.  Subsequent calls return the time between the current call
      //and the previous call.  The timer should be running or the delta will not work.
      Int64 delta(const std::string &deltaName, bool resetDelta = true);
      void clearDeltas(){deltaVals.clear();}
      
      bool isStarted(){return started;}
      bool isPaused(){return paused;}
   private:
      Int64 timerfunc(bool reset);
      Int64 prevtime;
      Int64 pausetime;
      Int64 timeOffset;
      bool started, paused, firstCall;
      class TimeDelta{
      public:
         TimeDelta(){deltaspot = 0;}
         Int64 deltaspot;
      };
      std::map<std::string, TimeDelta> deltaVals;
   };
}
#endif
