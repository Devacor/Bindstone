#include "generalUtility.h"
#include <iterator>
#include <vector>
#include <cmath>
#include <boost/lexical_cast.hpp>
#include <algorithm>

std::string M2Util::getNewStringId(){
   static unsigned int counter = 0;
   return "__M2_SID_"+boost::lexical_cast<std::string>(counter++);
}

int M2Util::roundUpPowerOfTwo(int num){
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

bool M2Util::isPowerOfTwo(int num){
   return num>0 && ((num & (num-1))==0);
}

std::string M2Util::wideToChar(M2Util::UtfChar wc){
   std::vector<char> c(MB_CUR_MAX);
   int totalBytes = wctomb(&c[0], wc);
   std::string result;
   for(int i = 0;i < totalBytes;++i){
      result+=c[i];
   }
   return result;
}

M2Util::UtfChar M2Util::charToWide(char c){
   wchar_t wc;
   mbtowc(&wc, &c, 1);
   return wc;
}

std::string M2Util::wideToString(const M2Util::UtfString& ws){
   std::string s;
   std::for_each(ws.begin(), ws.end(), [&](const M2Util::UtfChar &wc){
      s+=M2Util::wideToChar(wc);
   });
   return s;
}

M2Util::UtfString M2Util::stringToWide(const std::string& s){
   M2Util::UtfString ws;
   std::transform(s.begin(), s.end(), std::back_inserter(ws), M2Util::charToWide);
   return ws;
}


int M2Util::boundBetween(int val, int lowerBound, int upperBound){
   return static_cast<int>(boundBetween(static_cast<long>(val), static_cast<long>(lowerBound), static_cast<long>(upperBound)));
}

long M2Util::boundBetween(long val, long lowerBound, long upperBound){
   if(lowerBound > upperBound){std::swap(lowerBound, upperBound);}
   int rangeSize = upperBound - lowerBound;

   if (val < lowerBound)
      val += rangeSize * ((lowerBound - val) / rangeSize + 1);

   return lowerBound + (val - lowerBound) % rangeSize;

}

float M2Util::boundBetween(float val, float lowerBound, float upperBound){
   return static_cast<float>(boundBetween(static_cast<double>(val), static_cast<double>(lowerBound), static_cast<double>(upperBound)));
}

double M2Util::boundBetween(double val, double lowerBound, double upperBound){
   if(lowerBound > upperBound){std::swap(lowerBound, upperBound);}
   val-=lowerBound; //adjust to 0
   double rangeSize = upperBound - lowerBound;
   if(rangeSize == 0){return upperBound;} //avoid dividing by 0
   return val - (rangeSize * std::floor(val/rangeSize)) + lowerBound;
}

