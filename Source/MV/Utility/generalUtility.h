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
#include <mutex>
#include <cctype>
#include <algorithm>
#include <set>
#include <deque>

#include "MV/Utility/require.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

#define U8_STR(stringVal) reinterpret_cast<const char *>(u8##stringVal)
using namespace std::string_literals; //for convenience to enable ""s
struct SDL_RWops;
namespace MV {
	typedef char UtfChar;
	typedef std::string UtfString;

	const double PIE = 3.141592653589793115997963468544185161590576171875;
	const float PIEf = 3.1415927410125732421875f;

	std::string guid(std::string a_baseName = "guid_");

	void initializeFilesystem();

	inline bool isLittleEndian() {
		static std::int32_t test = 1;
		return *reinterpret_cast<std::int8_t*>(&test) == 1;
	}

	template <std::size_t DataSize>
	std::uint8_t* swapBytes(std::uint8_t * data) {
		for (std::size_t i = 0, end = DataSize / 2; i < end; ++i) {
			std::swap(data[i], data[DataSize - i - 1]);
		}
		return data;
	}

	template <std::size_t DataSize>
	std::uint8_t* swapBytesForNetwork(std::uint8_t * data) {
		if (isLittleEndian()) {
			swapBytes<DataSize>(data);
		}
		return data;
	}

	template< typename T >
	typename std::vector<std::shared_ptr<T>>::iterator insertSorted(std::vector<std::shared_ptr<T>> & a_vec, const std::shared_ptr<T>& a_item){
		return a_vec.insert(std::upper_bound(a_vec.begin(), a_vec.end(), a_item, [](const std::shared_ptr<T>& a_lhs, const std::shared_ptr<T> &a_rhs){return *a_lhs < *a_rhs; }), a_item);
	}

	template< typename T >
	typename std::vector<std::weak_ptr<T>>::iterator insertSorted(std::vector<std::weak_ptr<T>> & a_vec, const std::weak_ptr<T>& a_item) {
		return a_vec.insert(std::upper_bound(a_vec.begin(), a_vec.end(), a_item, [](const std::weak_ptr<T>& a_lhs, const std::weak_ptr<T> &a_rhs) {
			if (auto lhsLocked = a_lhs.lock()) {
				if (auto rhsLocked = a_rhs.lock()) {
					return *lhsLocked < *rhsLocked;
				} else {
					return false;
				}
			} else {
				return !a_rhs.expired();
			}
		}), a_item);
	}

	template< typename T >
	typename std::vector<T>::iterator insertSorted(std::vector<T> & a_vec, const T& a_item) {
		return a_vec.insert(std::upper_bound(a_vec.begin(), a_vec.end(), a_item, [](const T& a_lhs, const T &a_rhs) {return a_lhs < a_rhs; }), a_item);
	}

	template< typename T >
	typename std::vector<std::shared_ptr<T>>::iterator insertReverseSorted(std::vector<std::shared_ptr<T>> & a_vec, const std::shared_ptr<T>& a_item) {
		return a_vec.insert(std::upper_bound(a_vec.begin(), a_vec.end(), a_item, [](const std::shared_ptr<T>& a_lhs, const std::shared_ptr<T> &a_rhs) {return !(*a_lhs < *a_rhs); }), a_item);
	}

	template< typename T >
	typename std::vector<T>::iterator insertReverseSorted(std::vector<T> & a_vec, const T& a_item) {
		return a_vec.insert(std::upper_bound(a_vec.begin(), a_vec.end(), a_item, [](const T& a_lhs, const T &a_rhs) {return !(a_lhs < a_rhs); }), a_item);
	}

	template< typename T >
	typename std::vector<std::weak_ptr<T>>::iterator insertReverseSorted(std::vector<std::weak_ptr<T>> & a_vec, const std::weak_ptr<T>& a_item) {
		return a_vec.insert(std::upper_bound(a_vec.begin(), a_vec.end(), a_item, [](const std::weak_ptr<T>& a_lhs, const std::weak_ptr<T> &a_rhs) {
			if (auto lhsLocked = a_lhs.lock()) {
				if (auto rhsLocked = a_rhs.lock()) {
					return !(*lhsLocked < *rhsLocked);
				} else {
					return true;
				}
			} else {
				return a_rhs.expired();
			}
		}), a_item);
	}

	template< typename T, typename Pred >
	typename std::vector<T>::iterator insertSorted(std::vector<T> & a_vec, T const& a_item, Pred a_pred) {
		return a_vec.insert(std::upper_bound(a_vec.begin(), a_vec.end(), a_item, a_pred), a_item);
	}

	template<typename InputIterator, typename OutputIterator, typename Predicate, typename TransformFunc>
	OutputIterator transform_if(
		InputIterator&& begin,
		InputIterator&& end,
		OutputIterator&& out,
		Predicate&& predicate,
		TransformFunc&& transformer
	) {
		for (; begin != end; ++begin, ++out) {
			if (predicate(*begin))
				*out = transformer(*begin);
		}
		return out;
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

	inline std::string simpleFilter(std::string a_original, const std::string &a_allowed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789~!@#$%^&*()-=+/><\\[]{}?") {
		a_original.erase(std::remove_if(a_original.begin(), a_original.end(), [&](char c) {return a_allowed.find(c) == std::string::npos; }), a_original.end());
		return a_original;
	}
    
	inline int wrap(int lowerBound, int upperBound, int val) {
		using std::swap;
		if (lowerBound > upperBound) { swap(lowerBound, upperBound); }
		int rangeSize = upperBound - lowerBound;

		if (val < lowerBound)
			val += rangeSize * ((lowerBound - val) / rangeSize + 1);

		return lowerBound + (val - lowerBound) % rangeSize;
	}

	inline long wrap(long lowerBound, long upperBound, long val) {
		using std::swap;
		if (lowerBound > upperBound) { swap(lowerBound, upperBound); }
		long rangeSize = upperBound - lowerBound;

		if (val < lowerBound)
			val += rangeSize * ((lowerBound - val) / rangeSize + 1);

		return lowerBound + (val - lowerBound) % rangeSize;
	}

	inline float wrap(float lowerBound, float upperBound, float val) {
		using std::swap;
		if (lowerBound > upperBound) { swap(lowerBound, upperBound); }
		val -= lowerBound; //adjust to 0
		float rangeSize = upperBound - lowerBound;
		if (rangeSize == 0) { return upperBound; } //avoid dividing by 0
		return val - (rangeSize * std::floor(val / rangeSize)) + lowerBound;
	}

	inline double wrap(double lowerBound, double upperBound, double val) {
		using std::swap;
		if (lowerBound > upperBound) { swap(lowerBound, upperBound); }
		val -= lowerBound; //adjust to 0
		double rangeSize = upperBound - lowerBound;
		if (rangeSize == 0) { return upperBound; } //avoid dividing by 0
		return val - (rangeSize * std::floor(val / rangeSize)) + lowerBound;
	}

	inline float mix(float a_start, float a_end, float a_percent) {
		return (a_percent * (a_end - a_start)) + a_start;
	}

	inline float unmix(float a_start, float a_end, float a_value) {
		return a_end == a_start ? a_end : (a_value - a_start) / (a_end - a_start);
	}

	inline float mixIn(float a_start, float a_end, float a_percent, float a_strength) {
		return pow(a_percent, a_strength)*(a_end - a_start) + a_start;
	}

	inline float mix(float a_start, float a_end, float a_percent, float a_strength) {
		return mixIn(a_start, a_end, a_percent, a_strength);
	}

	inline float mixOut(float a_start, float a_end, float a_percent, float a_strength) {
		return (1.0f - pow(1.0f - a_percent, a_strength)) * (a_end - a_start) + a_start;
	}

	inline float mixInOut(float a_start, float a_end, float a_percent, float a_strength) {
		auto halfRange = (a_end - a_start) / 2.0f + a_start;
		if (a_percent < .5f) {
			return mixIn(a_start, halfRange, a_percent*2.0f, a_strength);
		}
		return mixOut(halfRange, a_end, (a_percent - .5f) * 2.0f, a_strength);
	}

	inline float mixOutIn(float a_start, float a_end, float a_percent, float a_strength) {
		auto halfRange = (a_end - a_start) / 2.0f + a_start;
		if (a_percent < .5f) {
			return mixOut(a_start, halfRange, a_percent * 2.0f, a_strength);
		}
		return mixIn(halfRange, a_end, (a_percent - .5f) * 2.0f, a_strength);
	}

	inline float unmixIn(float a_start, float a_end, float a_value, float a_strength) {
		return a_end == a_start ? a_end : pow((a_value - a_start) / (a_end - a_start), 1.0f / a_strength);
	}
	inline float unmix(float a_start, float a_end, float a_value, float a_strength) {
		return unmixIn(a_start, a_end, a_value, a_strength);
	}

	inline float unmixOut(float a_start, float a_end, float a_value, float a_strength) {
		return 1.0f - pow(1.0f - ((a_value - a_start) / (a_end - a_start)), 1.0f / a_strength);
	}

	inline float unmixInOut(float a_start, float a_end, float a_value, float a_strength) {
		auto halfRange = (a_end - a_start) / 2.0f + a_start;
		if (a_value < halfRange) {
			return unmixIn(a_start, halfRange, a_value, a_strength) / 2.0f;
		}
		return (unmixOut(halfRange, a_end, a_value, a_strength) / 2.0f) + .5f;
	}

	inline float unmixOutIn(float a_start, float a_end, float a_value, float a_strength) {
		auto halfRange = (a_end - a_start) / 2.0f + a_start;
		if (a_value < halfRange) {
			return unmixOut(a_start, halfRange, a_value, a_strength) / 2.0f;
		}
		return (unmixIn(halfRange, a_end, a_value, a_strength) / 2.0f) + .5f;
	}

	template <typename T>
	T clamp(T val, T lowerBound, T upperBound) {
		using std::swap;
		if (lowerBound > upperBound) { swap(lowerBound, upperBound); }
		return std::max(std::min(val, upperBound), lowerBound);
	}

	static inline float percentOfRange(float a_value, float a_start, float a_end) {
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

	template <typename T>
	int indexOf(const std::vector<T> &a_vector, const T& a_toFind){
		auto foundIterator = std::find(a_vector.cbegin(), a_vector.cend(), a_toFind);
		if(foundIterator == a_vector.cend()){
			return -1;
		}else{
			return static_cast<int>(std::distance(a_vector.cbegin(), foundIterator));
		}
	}

	//rounds num up to the next largest power of two (or the current value) and returns that value
	int roundUpPowerOfTwo(int num);

	bool isPowerOfTwo(int num);

	inline float toDegrees(float val){
		return val*(180.0f / float(PIE));
	}
	inline double toDegrees(double val) {
		return val*(180.0 / PIE);
	}

	inline float toRadians(float val){
		return val*(float(PIE) / 180.0f);
	}

	inline double toRadians(double val) {
		return val*(PIE / 180.0);
	}

	double distance(const double &x1, const double &y1, const double &x2, const double &y2);

	float distance(const float &x1, const float &y1, const float &x2, const float &y2);

	std::string fileNameFromPath(std::string a_path, bool a_includeExtension = false);
	const std::string& playerPreferencesPath();

	bool fileExistsAbsolute(const std::string& a_path);
	bool fileExistsInSearchPaths(const std::string& a_path);
	bool writeToFile(const std::string& a_path, const std::string& a_contents);
	bool deleteFile(const std::string& a_path);
	time_t lastFileWriteTime(const std::string& a_path);

	SDL_RWops* sdlFileHandle(const std::string& a_path);
	//returns a blank string if a_throwOnMissing is false.
	std::string fileContents(const std::string& a_path, bool a_throwOnMissing = false);

	inline float angle2D(int x1, int y1, int x2, int y2) {
		return wrap(toDegrees(atan2(static_cast<float>(y2 - y1), static_cast<float>(x2 - y2))), 0.0f, 360.0f);
	}

	inline float angle2D(float x1, float y1, float x2, float y2){
		return wrap(toDegrees(atan2(y2 -y1, x2 - x1)), 0.0f, 360.0f);
	}

	inline double angle2D(double x1, double y1, double x2, double y2) {
		return wrap(toDegrees(atan2(y2 - y1, x2 - x1)), 0.0, 360.0);
	}

	inline float angle2DRad(int x1, int y1, int x2, int y2) {
		return atan2(static_cast<float>(y2 - y1), static_cast<float>(x2 - y2));
	}

	inline float angle2DRad(float x1, float y1, float x2, float y2) {
		return atan2(y2 - y1, x2 - x1);
	}

	inline double angle2DRad(double x1, double y1, double x2, double y2) {
		return atan2(y2 - y1, x2 - x1);
	}

	inline void rotatePoint2DRad(float &x, float &y, float c, float s) {
		auto tmpX = ((x * c) - (y * s));
		auto tmpY = ((y * c) + (x * s));
		x = tmpX; y = tmpY;
	}

	inline void rotatePoint2DRad(float &x, float &y, float angle) {
		rotatePoint2DRad(x, y, cos(angle), sin(angle));
	}

	inline void rotatePoint2D(float &x, float &y, float angle){
		rotatePoint2DRad(x, y, toRadians(angle));
	}

	inline void rotatePoint2DRad(double &x, double &y, double c, double s) {
		auto tmpX = ((x * c) - (y * s));
		auto tmpY = ((y * c) + (x * s));
		x = tmpX; y = tmpY;
	}

	inline void rotatePoint2DRad(double &x, double &y, double angle) {
		rotatePoint2DRad(x, y, cos(angle), sin(angle));
	}

	inline void rotatePoint2D(double &x, double &y, double angle) {
		rotatePoint2DRad(x, y, toRadians(angle));
	}

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
		if constexpr (std::is_integral<T>::value){
			return lhs == rhs;
		} else {
			if(lhs == rhs){ //handle simple case equality
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
            return *this;
		}

		int64_t integer(int64_t a_min, int64_t a_max){
			return std::uniform_int_distribution<int64_t>{a_min, a_max}(generator);
		}

		double number(double a_min, double a_max){
			return std::uniform_real_distribution<double>{a_min, std::nextafter(a_max, a_max + 1.0)}(generator);
		}

		float number(float a_min, float a_max){
			return std::uniform_real_distribution<float>{a_min, std::nextafter(a_max, a_max + 1.0f)}(generator);
		}

		std::string randomString(std::string a_charset, size_t a_length) {
			if (a_length == 0 || a_charset.empty()) { return ""; }

			std::string result;
			while (a_length--) {
				result += a_charset[integer(0, a_charset.size() - 1)];
			}
			return result;
		}

		template<typename T>
		void shuffle(T collection) {
			std::shuffle(std::begin(collection), std::end(collection), generator);
		}

		static Random* global() {
			if (!instance) {
				instance = new Random();
			}
			return instance;
		}
	private:
		uint64_t generatorSeed;
		std::mt19937 generator;

		static Random* instance;
		friend double randomNumber(double, double);
		friend float randomNumber(float, float);
		friend int64_t randomInteger(int64_t, int64_t);
	};

	template<typename T>
	void randomShuffle(T collection) {
		return Random::global()->shuffle(collection);
	}
	double randomNumber(double a_min, double a_max);
	float randomNumber(float a_min, float a_max);
	int64_t randomInteger(int64_t a_min, int64_t a_max);
	std::string randomString(std::string a_charset, size_t a_length);
	std::string randomString(size_t a_length);

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

	template<typename T>
	class InstanceCounter {
	public:
		InstanceCounter() {
			++internalActiveCount; 
			++internalTotalCount;
		}
		InstanceCounter(const InstanceCounter&) {
			++internalActiveCount; 
			++internalTotalCount;
		}
		~InstanceCounter() { --internalActiveCount; }

		static size_t activeInstanceCount() { return internalActiveCount; }
		static size_t totalInstanceCount() { return internalTotalCount; }
	private:
		static size_t internalActiveCount;
		static size_t internalTotalCount;
	};

	template<typename T> size_t InstanceCounter<T>::internalActiveCount = 0;
	template<typename T> size_t InstanceCounter<T>::internalTotalCount = 0;

	//Primarily meant to queue actions for post construction in cereal
	class CallbackQueue {
	public:
		CallbackQueue(){}
		CallbackQueue(std::function<void(std::exception &e)> a_onException):onException(a_onException){}

		~CallbackQueue() {
			execute();
		}

		void execute() {
			std::lock_guard<std::mutex> guard(lock);
			for (auto&& action : actions) {
				try {
					action();
				} catch (std::exception &e) {
					onException(e);
				}
			}
			actions.clear();
		}

		CallbackQueue& then(std::function<void()> a_action) {
			std::lock_guard<std::mutex> guard(lock);
			actions.push_back(a_action);
			return *this;
		}

	private:
		std::mutex lock;
		std::function<void(std::exception &e)> onException;
		std::vector<std::function<void()>> actions;
	};

	class MainThreadCallback {
	public:
		MainThreadCallback(std::shared_ptr<CallbackQueue> a_queue = std::shared_ptr<CallbackQueue>(), std::function<void()> a_callback = std::function<void()>()) :
			queue(a_queue),
			boundCallback(a_callback){
		}

		void operator()(std::function<void()> a_callback) {
			if (queue) {
				queue->then(a_callback);
			}
		}

		void operator()() {
			if (queue && boundCallback) {
				queue->then(boundCallback);
			}
		}
	private:
		std::shared_ptr<CallbackQueue> queue;
		std::function<void()> boundCallback;
	};

	class TimeDeltaAggregate {
	public:
		inline void add(double a_timeStamp) {
			frames.push_back(a_timeStamp);
			if (frames.size() > MAX_TIMESTAMPS) {
				frames.pop_front();
			}
			dirty = true;
		}

		inline double delta() {
			if (dirty && frames.size() > 1) {
				auto first = frames.front();
				currentDelta = 0;
				for (auto frameTime : frames) {
					currentDelta += frameTime - first;
				}
				currentDelta /= frames.size() - 1;
				dirty = false;
			}
			return currentDelta;
		}
	private:
		size_t MAX_TIMESTAMPS = 4;
		bool dirty = false;
		double currentDelta = 1;
		std::deque<double> frames;
	};
}
#endif
