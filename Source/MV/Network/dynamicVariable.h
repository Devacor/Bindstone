#ifndef _MV_DYNAMIC_VARIABLE_H_
#define _MV_DYNAMIC_VARIABLE_H_

#include <boost/variant.hpp>
#include <string>
#include <tuple>

namespace chaiscript { class ChaiScript; }

namespace MV {
	class DynamicVariable {
	public:
		DynamicVariable() {}
		DynamicVariable(const DynamicVariable& a_value) = default;
		explicit DynamicVariable(bool a_value):
			boolVal(a_value),
			isEmpty(false){
		}
		explicit DynamicVariable(int64_t a_value) :
			intVal(a_value),
			isEmpty(false) {
		}
		explicit DynamicVariable(size_t a_value) :
			intVal(static_cast<int64_t>(a_value)),
			isEmpty(false) {
		}
		explicit DynamicVariable(int a_value) :
			intVal(a_value),
			isEmpty(false) {
		}
		explicit DynamicVariable(double a_value) :
			doubleVal(a_value),
			isEmpty(false) {
		}
		explicit DynamicVariable(const std::string &a_value) :
			stringVal(a_value),
			isEmpty(false) {
		}

		DynamicVariable& operator=(bool a_rhs) {
			boolVal = a_rhs;
			intVal = 0;
			doubleVal = 0.0;
			stringVal.clear();
			isEmpty = false;
			return *this;
		}

		DynamicVariable& operator=(int64_t a_rhs) {
			boolVal = false;
			intVal = a_rhs;
			doubleVal = 0.0;
			stringVal.clear();
			isEmpty = false;
			return *this;
		}
		DynamicVariable& operator=(size_t a_rhs) {
			boolVal = false;
			intVal = static_cast<int64_t>(a_rhs);
			doubleVal = 0.0;
			stringVal.clear();
			isEmpty = false;
			return *this;
		}
		DynamicVariable& operator=(int a_rhs) {
			boolVal = false;
			intVal = a_rhs;
			doubleVal = 0.0;
			stringVal.clear();
			isEmpty = false;
			return *this;
		}

		DynamicVariable& operator=(double a_rhs) {
			boolVal = false;
			intVal = 0;
			doubleVal = a_rhs;
			stringVal.clear();
			isEmpty = false;
			return *this;
		}

		DynamicVariable& operator=(const std::string &a_rhs) {
			boolVal = false;
			intVal = 0;
			doubleVal = 0.0;
			stringVal = a_rhs;
			isEmpty = false;
			return *this;
		}

		DynamicVariable& operator=(const DynamicVariable &a_rhs) {
			boolVal = a_rhs.boolVal;
			intVal = a_rhs.intVal;
			doubleVal = a_rhs.doubleVal;
			stringVal = a_rhs.stringVal;
			isEmpty = a_rhs.isEmpty;
			return *this;
		}

		bool operator<(const DynamicVariable &a_rhs) const {
			return std::tie(isEmpty, boolVal, intVal, doubleVal, stringVal) < std::tie(a_rhs.isEmpty, a_rhs.boolVal, a_rhs.intVal, a_rhs.doubleVal, a_rhs.stringVal);
		}

		bool operator==(bool a_rhs) const {
			return !isEmpty && boolVal == a_rhs;
		}
		bool operator==(int64_t a_rhs) const {
			return !isEmpty && intVal == a_rhs;
		}
		bool operator==(size_t a_rhs) const {
			return !isEmpty && intVal == static_cast<int64_t>(a_rhs);
		}
		bool operator==(int a_rhs) const {
			return !isEmpty && intVal == a_rhs;
		}
		bool operator==(double a_rhs) const {
			return !isEmpty && doubleVal == a_rhs;
		}
		bool operator==(const std::string &a_rhs) const {
			return !isEmpty && stringVal == a_rhs;
		}

		bool operator!=(bool a_rhs) const {
			return isEmpty || boolVal != a_rhs;
		}
		bool operator!=(int64_t a_rhs) const {
			return isEmpty || intVal != a_rhs;
		}
		bool operator!=(size_t a_rhs) const {
			return isEmpty || intVal != static_cast<int64_t>(a_rhs);
		}
		bool operator!=(int a_rhs) const {
			return isEmpty || intVal != a_rhs;
		}
		bool operator!=(double a_rhs) const {
			return isEmpty || doubleVal != a_rhs;
		}
		bool operator!=(const std::string &a_rhs) const {
			return isEmpty || stringVal != a_rhs;
		}

		bool getBool() const {
			return boolVal;
		}

		int64_t getInt() const {
			return intVal;
		}

		double getDouble() const {
			return doubleVal;
		}

		std::string getString() const {
			return stringVal;
		}

		void clear() {
			boolVal = false;
			intVal = 0;
			doubleVal = 0.0;
			stringVal.clear();
			isEmpty = true;
		}

		bool empty() const {
			return isEmpty;
		}

		template <class Archive>
		void serialize(Archive & archive) {
			archive(CEREAL_NVP(isEmpty), CEREAL_NVP(boolVal), CEREAL_NVP(intVal), CEREAL_NVP(doubleVal), CEREAL_NVP(stringVal));
		}
	private:
		bool boolVal = false;
		int64_t intVal = 0;
		double doubleVal = 0.0;
		std::string stringVal;
		bool isEmpty = true;
	};

	inline bool operator==(bool a_lhs, const DynamicVariable &a_rhs) {
		return a_rhs == a_lhs;
	}
	inline bool operator==(int64_t a_lhs, const DynamicVariable &a_rhs) {
		return a_rhs == a_lhs;
	}
	inline bool operator==(size_t a_lhs, const DynamicVariable &a_rhs) {
		return a_rhs == a_lhs;
	}
	inline bool operator==(int a_lhs, const DynamicVariable &a_rhs) {
		return a_rhs == a_lhs;
	}
	inline bool operator==(double a_lhs, const DynamicVariable &a_rhs) {
		return a_rhs == a_lhs;
	}
	inline bool operator==(const std::string &a_lhs, const DynamicVariable &a_rhs) {
		return a_rhs == a_lhs;
	}

	inline bool operator!=(bool a_lhs, const DynamicVariable &a_rhs) {
		return a_rhs != a_lhs;
	}
	inline bool operator!=(int64_t a_lhs, const DynamicVariable &a_rhs) {
		return a_rhs != a_lhs;
	}
	inline bool operator!=(size_t a_lhs, const DynamicVariable &a_rhs) {
		return a_rhs != a_lhs;
	}
	inline bool operator!=(int a_lhs, const DynamicVariable &a_rhs) {
		return a_rhs != a_lhs;
	}
	inline bool operator!=(double a_lhs, const DynamicVariable &a_rhs) {
		return a_rhs != a_lhs;
	}
	inline bool operator!=(const std::string &a_lhs, const DynamicVariable &a_rhs) {
		return a_rhs != a_lhs;
	}

	//I needed more debuggability and boost was throwing a lot within Cereal. :<
	//typedef boost::variant<bool, int64_t, double, std::string> DynamicVariable;
	void hookDynamicVariable(chaiscript::ChaiScript &a_script);
}

#endif
