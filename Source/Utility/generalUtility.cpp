#include "generalUtility.h"
#include <iterator>
#include <vector>
#include <cmath>
#include <map>
#include <boost/lexical_cast.hpp>
#include <algorithm>

namespace MV {

   std::string getNewStringId(std::string a_baseName){
      static std::map<std::string, int> counters;
      return a_baseName+boost::lexical_cast<std::string>(counters[a_baseName]++);
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
      int totalBytes = wcrtomb(&c[0], wc, &ignore);
      std::string result;
      for(int i = 0;i < totalBytes;++i){
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
      int rangeSize = upperBound - lowerBound;

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