#ifndef __MV_GENERALUTILITY_H__
#define __MV_GENERALUTILITY_H__

#include <string>
#include <sstream>
#include <iostream>
#include <cmath>
#include <iostream>
#include <functional>
#include <numeric>
#include <stdint.h>
#include <random>
#include <array>
#include <type_traits>

#include "Utility/require.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

#define UTF_CHAR_STR(stringVal) L##stringVal

namespace MV {
	typedef wchar_t UtfChar;
	typedef std::wstring UtfString;

	enum AngleType {DEGREES, RADIANS};
	const double PIE = 3.14159265358979323846;

	const float MV_EPSILONF = .0001f;
	const double MV_EPSILON = .00000001f;

	std::string guid(std::string a_baseName = "guid_");

	void initializeFilesystem();

	void systemSleep(int time);

	template< typename T >
	typename std::vector<std::shared_ptr<T>>::iterator insertSorted(std::vector<std::shared_ptr<T>> & a_vec, const std::shared_ptr<T>& a_item){
		return a_vec.insert(std::lower_bound(a_vec.begin(), a_vec.end(), a_item, [](const std::shared_ptr<T>& a_lhs, const std::shared_ptr<T> &a_rhs){return *a_lhs < *a_rhs; }), a_item);
	}

	template< typename T >
	typename std::vector<T>::iterator insertSorted(std::vector<T> & a_vec, const T& a_item) {
		return a_vec.insert(std::lower_bound(a_vec.begin(), a_vec.end(), a_item, [](const T& a_lhs, const T &a_rhs) {return a_lhs < a_rhs; }), a_item);
	}

	template< typename T >
	typename std::vector<std::shared_ptr<T>>::iterator insertReverseSorted(std::vector<std::shared_ptr<T>> & a_vec, const std::shared_ptr<T>& a_item) {
		return a_vec.insert(std::lower_bound(a_vec.begin(), a_vec.end(), a_item, [](const std::shared_ptr<T>& a_lhs, const std::shared_ptr<T> &a_rhs) {return !(*a_lhs < *a_rhs); }), a_item);
	}

	template< typename T >
	typename std::vector<T>::iterator insertReverseSorted(std::vector<T> & a_vec, const T& a_item) {
		return a_vec.insert(std::lower_bound(a_vec.begin(), a_vec.end(), a_item, [](const T& a_lhs, const T &a_rhs) {return !(a_lhs < a_rhs); }), a_item);
	}

	template <class T, size_t I, size_t... J>
	struct MultiArray {
		typedef typename MultiArray<T, J...>::type Nested;
		typedef std::array<Nested, I> type;
	};

	template <class T, size_t I>
	struct MultiArray<T, I> {
		typedef std::array<T, I> type;
	};

	template<typename T>
	T mix(const T &a_start, const T &a_end, float a_percent, float a_strength = 1.0f) {
		return T{pow(a_percent, a_strength) * (a_end - a_start) + a_start};
	}

	template<typename T>
	T mixIn(const T &a_start, const T &a_end, float a_percent, float a_strength = 1.0f) {
		return T{pow(a_percent, a_strength) * (a_end - a_start) + a_start};
	}

	template<typename T>
	T mixOut(const T &a_start, const T &a_end, float a_percent, float a_strength = 1.0f) {
		return T{(1.0f - pow(a_percent, a_strength)) * (a_end - a_start) + a_start};
	}

	template<typename T>
	T mixInOut(const T &a_start, const T &a_end, float a_percent, float a_strength = 1.0f) {
		auto halfRange = ((a_end - a_start) / 2.0f);
		if(a_percent < .5f){
			return T{pow(a_percent * 2.0f, a_strength) * halfRange + a_start};
		} else{
			return T{a_end - ((1.0f - pow((a_percent - .5f) * 2.0f, a_strength)) * halfRange)};
		}
	}

	template <typename T>
	T clamp(T val, T lowerBound, T upperBound) {
		using std::swap;
		if (lowerBound > upperBound) { swap(lowerBound, upperBound); }
		return std::max(std::min(val, upperBound), lowerBound);
	}

	static inline float percentOfRange(float a_start, float a_end, float a_value) {
		if (a_start > a_end) {
			std::swap(a_start, a_end);
		}
		a_value = clamp(a_value, a_start, a_end);

		if (a_start < 0.0f && a_end > 0.0f) {
			a_value += -a_start;
			a_end += -a_start;
			a_start = 0.0f;
		}

		return (a_value - a_start) / (a_end - a_start);
	}

	static inline float unmix(float a_start, float a_end, float a_value, float a_strength = 1.0f) {
		return mix(0.0f, 1.0f, percentOfRange(a_start, a_end, a_value), a_strength);
	}

	static inline float unmixIn(float a_start, float a_end, float a_value, float a_strength = 1.0f) {
		return mixIn(0.0f, 1.0f, percentOfRange(a_start, a_end, a_value), a_strength);
	}

	static inline float unmixOut(float a_start, float a_end, float a_value, float a_strength = 1.0f) {
		return mixOut(0.0f, 1.0f, percentOfRange(a_start, a_end, a_value), a_strength);
	}

	static inline float unmixInOut(float a_start, float a_end, float a_value, float a_strength = 1.0f) {
		return mixInOut(0.0f, 1.0f, percentOfRange(a_start, a_end, a_value), a_strength);
	}

	//rounds num up to the next largest power of two (or the current value) and returns that value
	int roundUpPowerOfTwo(int num);

	bool isPowerOfTwo(int num);

	std::string toString(UtfChar wc);
	UtfChar toWide(char c);

	std::string toString(const UtfString& ws);
	UtfString toWide(const std::string& s);

	template <class Type>
	Type toDegrees(const Type &val){
		return static_cast<Type>(val*(180.0 / PIE));
	}

	template <class Type>
	Type toRadians(const Type &val){
		return static_cast<Type>(val*(PIE / 180.0));
	}

	template <class Type>
	double distance(const Type &x1, const Type &y1, const Type &x2, const Type &y2){
		Type deltaX = x1-x2, deltaY = y1-y2;
		if(deltaX<0){deltaX*=-1;}
		if(deltaY<0){deltaY*=-1;}
		return sqrt(static_cast<double>(((deltaX)*(deltaX)) + ((deltaY)*(deltaY))));
	}

	template <class Type>
	double distance(const Type &a_lhs, const Type &a_rhs) {
		return distance(a_lhs.x, a_lhs.y, a_rhs.x, a_rhs.y);
	}

	template <class Type>
	double angle(const Type &x1, const Type &y1, const Type &x2, const Type &y2, AngleType returnAs = DEGREES){
		if(returnAs == DEGREES){
			return wrap(static_cast<double>(toDegrees(atan2(y2 -y1, x2 - x1))), 0.0, 360.0);
		}else{
			return static_cast<double>(atan2(y2 - y1, x2 - x1));
		}
	}

	template <class Type>
	double angle(const Type &a_lhs, const Type &a_rhs, AngleType returnAs = DEGREES) {
		return angle(a_lhs.x, a_lhs.y, a_rhs.x, a_rhs.y, returnAs);
	}

	template <class Type>
	void rotatePoint2D(Type &x, Type &y, Type angle, AngleType angleUnitIs = DEGREES){
		if(angleUnitIs == DEGREES){
			angle = toRadians(angle);
		}
		Type tmpX, tmpY;
		tmpX = Type((x * cos(angle)) - (y * sin(angle)));
		tmpY = Type((y * cos(angle)) + (x * sin(angle)));
		x = tmpX; y = tmpY;
	}

	template <class Type>
	void rotatePoint3D(Type &x, Type &y, Type &z, Type aX, Type aY, Type aZ, AngleType angleUnitIs = DEGREES){
		if(angleUnitIs == DEGREES){
			aY = toRadians(aY); aX = toRadians(aX); aZ = toRadians(aZ);
		}

		Type tmpy = y;
		y = (y * cos(aX)) - (z * sin(aX));
		z = (tmpy * sin(aX)) + (z * cos(aX));

		Type tmpx = x;
		x = (z * sin(aY)) + (x * cos(aY));
		z = (z * cos(aY)) - (tmpx * sin(aY));

		tmpx = x;
		x = (y * sin(aZ)) + (x * cos(aZ));
		y = (y * cos(aZ)) - (tmpx * sin(aZ));
	}

	template <class Type>
	void rotatePoint(Type &a_point, Type a_angle, AngleType angleUnitIs = DEGREES){
		if(equals(a_angle.x, 0.0f) && equals(a_angle.y, 0.0f)){
			rotatePoint2D(a_point.x, a_point.y, a_angle.z, angleUnitIs);
		} else{
			rotatePoint3D(a_point.x, a_point.y, a_point.z, a_angle.x, a_angle.y, a_angle.z, angleUnitIs);
		}
	}

	int wrap(int lowerBound, int upperBound, int val);
	long wrap(long lowerBound, long upperBound, long val);
	float wrap(float lowerBound, float upperBound, float val);
	double wrap(double lowerBound, double upperBound, double val);

	//returns the shortest distance between two numbers within a given bounding set of values.  If the closest value is the
	//wraparound value and wrapDist is passed in then wrapDist is set to 1, if it is closer between the two numbers, wrapDist==0
	template <class Type>
	Type wrappingDistance(Type lowerBound, Type upperBound, Type val, Type val2, bool *wrapDist = nullptr){
		using std::swap;
		if (lowerBound > upperBound) { swap(lowerBound, upperBound); }
		if(val==val2){
			if(wrapDist != nullptr){*wrapDist = false;}
			return 0;
		}
		val = wrap(val, lowerBound, upperBound);
		val2 = wrap(val2, lowerBound, upperBound);
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
		auto ftSize = sizeof(FT);
		auto itSize = sizeof(IT);
		require<RangeException>(ftSize == itSize, "Function 'floatEqualImplementation' had an issue! Inequal type sizes: ", ftSize, " != ", itSize);
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
		} else if(!floatingPointRangeCompareCheck(lhs, rhs) || (lhs < 0 && rhs > 0) || (lhs > 0 && rhs < 0)){
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
				require<RangeException>(0, "Function 'equals' had too big a type to check!");
			}
		}
	}

	class Random {
	public:
		Random():
			Random(std::random_device{}()){
		}

		Random(uint64_t a_seed):
			generatorSeed(a_seed),
			generator(static_cast<unsigned long>(a_seed)){
		}
		uint64_t seed() const{
			return generatorSeed;
		}
		Random& seed(uint64_t a_seed){
			generator.seed(static_cast<unsigned long>(a_seed));
		}

		int number(int a_min, int a_max){
			return std::uniform_int_distribution<int>{a_min, a_max}(generator);
		}

		size_t number(size_t a_min, size_t a_max){
			return std::uniform_int_distribution<size_t>{a_min, a_max}(generator);
		}

		double number(double a_min, double a_max){
			return std::uniform_real_distribution<double>{a_min, std::nextafter(a_max, a_max + 1.0)}(generator);
		}

		float number(float a_min, float a_max){
			return std::uniform_real_distribution<float>{a_min, std::nextafter(a_max, a_max + 1.0f)}(generator);
		}
	private:
		uint64_t generatorSeed;
		std::mt19937 generator;

		static Random* instance;
		friend double randomNumber(double, double);
		friend float randomNumber(float, float);
		friend int randomNumber(int, int);
		friend size_t randomNumber(size_t, size_t);
	};

	double randomNumber(double a_min, double a_max);
	float randomNumber(float a_min, float a_max);
	int randomNumber(int a_min, int a_max);

	//Generic constructor for automatic type deduction: MV::make<std::pair>(1, 2); and so on (instead of std::pair<int>(1, 2);)
	template <template <typename...> class TemplateClass, typename... Args>
	TemplateClass<Args...> make(Args&&... a_args){
		return TemplateClass<Args...>(std::forward<Args>(a_args)...);
	}

	template <typename T>
	void moveAppend(std::vector<T>& a_dst, std::vector<T>& a_src){
		a_dst.insert(a_dst.end(), make_move_iterator(a_src.begin()), make_move_iterator(a_src.end()));
		a_src.clear();
	}

	template <typename T>
	void moveCopy(std::vector<T>& a_dst, std::vector<T>& a_src, size_t a_dstOffset){
		std::copy(make_move_iterator(a_src.begin()), make_move_iterator(a_src.end()), a_dst.begin() + a_dstOffset);
		a_src.clear();
	}

	class atomic_cout {
		std::ostringstream stream;
	public:
		template <typename T>
		atomic_cout& operator<<(T const& t) {
			stream << t;
			return *this;
		}
		atomic_cout& operator<<(std::ostream& (*f)(std::ostream& o)) {
			stream << f;
			return *this;
		}
		~atomic_cout() {
			std::cout << stream.str();
		}
	};
}
#endif
