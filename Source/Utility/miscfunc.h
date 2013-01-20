#ifndef __MISFUNC_H__
#define __MISFUNC_H__

#include <sstream>
#include <iostream>
#include <cmath>

namespace M2Util {
	enum AngleType {DEGREES, RADIANS};
	const double PIE = 3.14159265358979323846;

	//Some general exceptions that can be used
	class DefaultException {};
	class RangeException {};
	class ResourceException {};

	//assert functions (named require to avoid potential name clashes with the common c macro)
	template <class exception_type, class condition_type>
	inline void require(condition_type condition){
		if(!condition){
			std::cerr << "ASSERTION FAILED!" << std::endl;
			throw exception_type();
		}
	}
	template <class exception_type, class condition_type>
	inline void require(condition_type condition, exception_type exception){
		if(!condition){
			std::cerr << "ASSERTION FAILED!" << std::endl;
			throw exception;
		}
	}
	template <class condition_type>
	inline void require(condition_type condition){
		if(!condition){
			std::cerr << "ASSERTION FAILED!" << std::endl;
			throw DefaultException();
		}
	}

	//rounds num up to the next largest power of two (or the current value) and returns that value
	int roundUpPowerOfTwo(int num);

	template <class variable_type>
	variable_type toDegrees(variable_type val){
		return val*(180.0/PIE);
	}

	template <class variable_type>
	variable_type toRadians(variable_type val){
		return val*(PIE/180.0);
	}

	template <class Type>
	Type calculateDistanceBetweenPoints(Type x1, Type y1, Type x2, Type y2){
		Type deltaX = x1-x2, deltaY = y1-y2;
		if(deltaX<0){deltaX*=-1;}
		if(deltaY<0){deltaY*=-1;}
		return sqrt(((deltaX)*(deltaX)) + ((deltaY)*(deltaY)));
	}

	template <class Type>
	long double calculateAngleBetweenPoints(Type x1, Type y1, Type x2, Type y2, AngleType returnAs = DEGREES){
		Type dx = x2-x1;
		Type dy = y2-y1;
		Type angle = 0.0;

		// Calculate angle
		if (dx == 0.0){
			if (dy == 0.0){
				angle = 0.0;
			}else if (dy > 0.0){
				angle = PIE / 2.0;
			}else{
				angle = PIE * 3.0 / 2.0;
			}
		}else if (dy == 0.0){
			if  (dx > 0.0){
				angle = 0.0;
			}else{
				angle = PIE;
			}
		}else{
			if(dx < 0.0){
				angle = atan(dy/dx) + PIE;
			}else if (dy < 0.0){
				angle = atan(dy/dx) + (2.0*PIE);
			}else{
				angle = atan(dy/dx);
			}
		}
		if(returnAs == DEGREES){
			return toDegrees(angle);
		}else{
			return angle;
		}
	}

	template <class Type>
	long double calculateAngleBetweenPoints2(Type x1, Type y1, Type x2, Type y2, AngleType returnAs = DEGREES){
		if(returnAs == DEGREES){
			return toDegrees(atan2(y2 -y1, x2 - x1));
		}else{
			return atan2(y2 - y1, x2 - x1);
		}
	}

	template <class variable_type>
	void rotatePoint2D(variable_type &x, variable_type &y, long double angle, AngleType angleUnitIs = DEGREES){
		if(angleUnitIs == DEGREES){
			angle = toRadians(angle);
		}
		variable_type tmpX, tmpY;
		tmpX = variable_type((x * cos(angle)) - (y * sin(angle)));
		tmpY = variable_type((y * cos(angle)) + (x * sin(angle)));
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

	template <class variable_type>
	variable_type capBetween(const variable_type &val, const variable_type &lowerBound, const variable_type &upperBound){
		require<RangeException>(upperBound >= lowerBound);
		variable_type resultVal = val;

		if(resultVal > upperBound){
			resultVal = upperBound;
		}else if(resultVal > lowerBound){
			resultVal = lowerBound;
		}
		while(resultVal > upperBound){resultVal-=boundDifference;}
		while(resultVal < lowerBound){resultVal+=boundDifference;}

		return resultVal;
	}

	template <class variable_type>
	variable_type boundBetween(const variable_type &val, const variable_type &lowerBound, const variable_type &upperBound){
		require<RangeException>(upperBound >= lowerBound);
		const variable_type boundDifference = upperBound-lowerBound;
		variable_type resultVal = val;

		while(resultVal > upperBound){
			resultVal-=boundDifference;
		}
		while(resultVal < lowerBound){
			resultVal+=boundDifference;
		}

		return resultVal;
	}

	//returns the shortest distance between two numbers within a given bounding set of values.  If the closest value is the
	//wraparound value and wrapDist is passed in then wrapDist is set to 1, if it is closer between the two numbers, wrapDist==0
	template <class variable_type>
	variable_type getWrappingDistance(variable_type val, variable_type val2, variable_type lowerBound, variable_type upperBound, bool *wrapDist=NULL){
		require<RangeException>(upperBound >= lowerBound);
		if(val==val2){return 0;}
		val = boundBetween(val, lowerBound, upperBound);
		val2 = boundBetween(val2, lowerBound, upperBound);
		variable_type dist1, dist2;
		if(val>val2){
			dist1 = val-val2;
			dist2 = (upperBound-val)+val2;
			if(dist1 <= dist2){
				if(wrapDist!=NULL){
					*wrapDist = 0;
				}
				return dist1;
			}else{
				if(wrapDist!=NULL){
					*wrapDist = 1;
				}
				return dist2;
			}
		}else{
			dist1 = val2-val;
			dist2 = (upperBound-val2)+val;
			if(dist1 <= dist2){
				if(wrapDist!=NULL){
					*wrapDist = 0;
				}
				return dist1;
			}else{
				if(wrapDist!=NULL){
					*wrapDist = 1;
				}
				return dist2;
			}
		}
	}
}
#endif
