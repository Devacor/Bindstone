#ifndef __MV_GENERALUTILITY_H__
#define __MV_GENERALUTILITY_H__

#undef require

#include <string>
#include <sstream>
#include <iostream>
#include <cmath>
#include <iostream>
#include <functional>
#include <numeric>
#include <stdint.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

#define UTF_CHAR_STR(stringVal) L##stringVal

namespace MV {
	typedef wchar_t UtfChar;
	typedef std::wstring UtfString;

	enum AngleType {DEGREES, RADIANS};
	const double PIE = 3.14159265358979323846;

	std::string guid(std::string a_baseName = "guid_");

	void initializeFilesystem();

	void systemSleep(int time);

	//Some general exceptions that can be used
	class MVExceptionBase {
	public:
		MVExceptionBase(){}

		void setExceptionMessage(std::string a_exceptionPrefix, std::string a_exceptionMessage){
			exceptionMessage = "[" + a_exceptionPrefix + "] = (" + a_exceptionMessage + ")";
		}
		void setExceptionMessage(std::string a_exceptionPrefix){
			exceptionMessage = "[" + a_exceptionPrefix + "]";
		}
		const std::string getExceptionMessage(){
			return exceptionMessage;
		}
	protected:
		std::string exceptionMessage;
	};

	class DefaultException : public MVExceptionBase {
	public:
		DefaultException(){setExceptionMessage("Default Exception");}
		DefaultException(std::string a_exceptionMessage){setExceptionMessage("Default Exception", a_exceptionMessage);}
	};
	class RangeException : public MVExceptionBase {
	public:
		RangeException(){setExceptionMessage("Range Exception");}
		RangeException(std::string a_exceptionMessage){setExceptionMessage("Range Exception", a_exceptionMessage);}
	};
	class ResourceException : public MVExceptionBase {
	public:
		ResourceException(){setExceptionMessage("Resource Exception");}
		ResourceException(std::string a_exceptionMessage){setExceptionMessage("Resource Exception", a_exceptionMessage);}
	};
	class PointerException : public MVExceptionBase {
	public:
		PointerException(){setExceptionMessage("Pointer Exception");}
		PointerException(std::string a_exceptionMessage){setExceptionMessage("Pointer Exception", a_exceptionMessage);}
	};

	template <class exception_type>
	inline exception_type outputAssertionFailed(exception_type exception) {
		std::cerr << "ASSERTION FAILED! " << exception.getExceptionMessage() << std::endl;
		return exception;
	}

	//assert functions (named require to avoid potential name clashes with the common c macro "assert")
	template <class exception_type, class condition_type>
	inline void require(condition_type condition){
		if(!condition){
			throw outputAssertionFailed<exception_type>(exception_type());
		}
	}
	template <class exception_type, class condition_type>
	inline void require(condition_type condition, const exception_type &exception){
		if(!condition){
			throw outputAssertionFailed<exception_type>(exception);
		}
	}
	template <class condition_type>
	inline void require(condition_type condition){
		if(!condition){
			throw outputAssertionFailed<DefaultException>(DefaultException());
		}
	}

	//rounds num up to the next largest power of two (or the current value) and returns that value
	int roundUpPowerOfTwo(int num);

	bool isPowerOfTwo(int num);

	std::string wideToChar(UtfChar wc);
	UtfChar charToWide(char c);

	std::string wideToString(const UtfString& ws);
	UtfString stringToWide(const std::string& s);

	template <class Type>
	Type toDegrees(const Type &val){
		return static_cast<Type>(val*(180.0 / PIE));
	}

	template <class Type>
	Type toRadians(const Type &val){
		return static_cast<Type>(val*(PIE / 180.0));
	}

	template <class Type>
	Type calculateDistanceBetweenPoints(const Type &x1, const Type &y1, const Type &x2, const Type &y2){
		Type deltaX = x1-x2, deltaY = y1-y2;
		if(deltaX<0){deltaX*=-1;}
		if(deltaY<0){deltaY*=-1;}
		return sqrt(((deltaX)*(deltaX)) + ((deltaY)*(deltaY)));
	}

	template <class Type>
	long double calculateAngleBetweenPoints(const Type &x1, const Type &y1, const Type &x2, const Type &y2, AngleType returnAs = DEGREES){
		if(returnAs == DEGREES){
			return boundBetween(toDegrees(atan2(y2 -y1, x2 - x1)), static_cast<Type>(0.0), static_cast<Type>(360.0));
		}else{
			return atan2(y2 - y1, x2 - x1);
		}
	}

	template <class Type>
	void rotatePoint2D(Type &x, Type &y, long double angle, AngleType angleUnitIs = DEGREES){
		if(angleUnitIs == DEGREES){
			angle = toRadians(angle);
		}
		Type tmpX, tmpY;
		tmpX = Type((x * cos(angle)) - (y * sin(angle)));
		tmpY = Type((y * cos(angle)) + (x * sin(angle)));
		x = tmpX; y = tmpY;
	}

	template <class Type>
	void rotatePoint3D(Type &x, Type &y, Type &z, long double aX, long double aY, long double aZ, long double angle = 1.0, AngleType angleUnitIs = DEGREES){
		if(angleUnitIs == DEGREES){
			aY = toRadians(aY); aX = toRadians(aX); aZ = toRadians(aZ);
		}

		long double tmpy = y;
		y = (y * cos(aX)) - (z * sin(aX));
		z = (tmpy * sin(aX)) + (z * cos(aX));

		long double tmpx = x;
		x = (z * sin(aY)) + (x * cos(aY));
		z = (z * cos(aY)) - (tmpx * sin(aY));

		tmpx = x;
		x = (y * sin(aZ)) + (x * cos(aZ));
		y = (y * cos(aZ)) - (tmpx * sin(aZ));
	}

	template <class Type>
	Type capBetween(const Type &val, const Type & lowerBound, const Type & upperBound){
		return std::min(std::max(val, lowerBound), val);
	}

	int boundBetween(int val, int lowerBound, int upperBound);
	long boundBetween(long val, long lowerBound, long upperBound);
	float boundBetween(float val, float lowerBound, float upperBound);
	double boundBetween(double val, double lowerBound, double upperBound);

	//returns the shortest distance between two numbers within a given bounding set of values.  If the closest value is the
	//wraparound value and wrapDist is passed in then wrapDist is set to 1, if it is closer between the two numbers, wrapDist==0
	template <class Type>
	Type getWrappingDistance(Type val, Type val2, Type lowerBound, Type upperBound, bool *wrapDist = nullptr){
		require<RangeException>(upperBound >= lowerBound);
		if(val==val2){
			if(wrapDist != nullptr){*wrapDist = false;}
			return 0;
		}
		val = boundBetween(val, lowerBound, upperBound);
		val2 = boundBetween(val2, lowerBound, upperBound);
		Type dist1, dist2;
		if(val>val2){
			dist1 = val-val2;
			dist2 = (upperBound-val)+val2;
		}else{
			dist1 = val2-val;
			dist2 = (upperBound-val2)+val;
		}

		if(wrapDist!=nullptr){
			*wrapDist = !(dist1 <= dist2);
		}
		return (dist1 <= dist2)?dist1:dist2;
	}

	//expects the same type for both parameters.
	template<typename Type>
	bool floatingPointRangeCompareCheck(Type value, Type delta){
		return delta == 0. || (value - delta != value);
	}

	template<typename FT, typename IT>
	bool floatEqualImplementation(FT lhs, FT rhs, IT maxUlps){
		require(sizeof(FT) == sizeof(IT), RangeException("Function 'floatEqualImplementation' had an issue!"));
		IT intDiff = std::abs(*(IT*)&lhs - *(IT*)&rhs);
		return intDiff <= maxUlps;
	}

	//inspired by: http://www.cygnus-software.com/papers/comparingfloats/Comparing%20floating%20point%20numbers.htm
	//good up to 64 bit floating point types.
	template<typename T>
	bool equals(T lhs, T rhs){
		if(std::numeric_limits<T>::epsilon() == 0){ //integral type
			return lhs == rhs;
		} else if(lhs == rhs){ //handle simple case equality
			return true;
		} else if(!floatingPointRangeCompareCheck(lhs, rhs)){
			return false;
		} else{
			int maxUlps = 1; //precision required
			if(sizeof(T) == sizeof(int8_t)){
				return floatEqualImplementation(lhs, rhs, static_cast<int8_t>(maxUlps));
			} else if(sizeof(T) == sizeof(int16_t)){
				return floatEqualImplementation(lhs, rhs, static_cast<int16_t>(maxUlps));
			} else if(sizeof(T) == sizeof(int32_t)){
				return floatEqualImplementation(lhs, rhs, static_cast<int32_t>(maxUlps));
			} else if(sizeof(T) == sizeof(int64_t)){
				return floatEqualImplementation(lhs, rhs, static_cast<int64_t>(maxUlps));
			} else{
				require(0, RangeException("Function 'equals' had too big a type to check!"));
			}
		}
	}
}
#endif
