#ifndef _MV_DYNAMIC_VARIABLE_H_
#define _MV_DYNAMIC_VARIABLE_H_

#include <string>
#include <tuple>
#include <variant>

namespace chaiscript { class ChaiScript; }

namespace MV {
	class DynamicVariable {
	public:
		DynamicVariable() {}
		DynamicVariable(const DynamicVariable& a_value) = default;
		explicit DynamicVariable(bool a_value):
			value(a_value) {
		}
		explicit DynamicVariable(int64_t a_value) :
			value(a_value) {
		}
		explicit DynamicVariable(size_t a_value) :
			value(static_cast<int64_t>(a_value)) {
		}
		explicit DynamicVariable(int a_value) :
			value(static_cast<int64_t>(a_value)) {
		}
		explicit DynamicVariable(double a_value) :
			value(a_value) {
		}
		explicit DynamicVariable(const std::string &a_value) :
			value(a_value) {
		}

		DynamicVariable& operator=(bool a_rhs) {
			value = a_rhs;
			return *this;
		}

		DynamicVariable& operator=(int64_t a_rhs) {
			value = a_rhs;
			return *this;
		}
		DynamicVariable& operator=(size_t a_rhs) {
			value = static_cast<int64_t>(a_rhs);
			return *this;
		}
		DynamicVariable& operator=(int a_rhs) {
			value = static_cast<int64_t>(a_rhs);
			return *this;
		}

		DynamicVariable& operator=(double a_rhs) {
			value = a_rhs;
			return *this;
		}

		DynamicVariable& operator=(const std::string &a_rhs) {
			value = a_rhs;
			return *this;
		}

		DynamicVariable& operator=(const DynamicVariable &a_rhs) {
			value = a_rhs.value;
			return *this;
		}

		bool operator<(const DynamicVariable &a_rhs) const {
			return value < a_rhs.value;
		}

		bool operator==(bool a_rhs) const {
			try { 
				return std::get<bool>(value) == a_rhs; 
			} catch (...) { 
				return false; 
			}
		}
		bool operator==(int64_t a_rhs) const {
			try {
				return std::get<int64_t>(value) == a_rhs;
			} catch (...) {
				return false;
			}
		}
		bool operator==(size_t a_rhs) const {
			return *this == static_cast<int64_t>(a_rhs);
		}
		bool operator==(int a_rhs) const {
			return *this == static_cast<int64_t>(a_rhs);
		}
		bool operator==(double a_rhs) const {
			try { 
				return std::get<double>(value) == a_rhs;
			} catch (...) { 
				return false; 
			}
		}
		bool operator==(const std::string &a_rhs) const {
			try { 
				return std::get<std::string>(value) == a_rhs;
			} catch (...) { 
				return false; 
			}
		}

		bool operator!=(bool a_rhs) const {
			return !(*this == a_rhs);
		}
		bool operator!=(int64_t a_rhs) const {
			return !(*this == a_rhs);
		}
		bool operator!=(size_t a_rhs) const {
			return !(*this == a_rhs);
		}
		bool operator!=(int a_rhs) const {
			return !(*this == a_rhs);
		}
		bool operator!=(double a_rhs) const {
			return !(*this == a_rhs);
		}
		bool operator!=(const std::string &a_rhs) const {
			return !(*this == a_rhs);
		}

		bool getBool() const {
			return std::get<bool>(value);
		}

		int64_t getInt() const {
			return std::get<int64_t>(value);
		}

		double getDouble() const {
			return std::get<double>(value);
		}

		const std::string& getString() const {
			return std::get<std::string>(value);
		}

		void clear() {
			value = std::monostate{};
		}

		bool empty() const {
			return value.index() == 0;
		}

		template <class Archive>
		void serialize(Archive & archive) {
			archive(CEREAL_NVP(value));
		}
	private:
		std::variant<std::monostate, bool, int64_t, double, std::string> value;
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

	//I needed more debuggability and std::variant was throwing a lot within Cereal. :<
	//typedef std::variant<bool, int64_t, double, std::string> DynamicVariable;
	void hookDynamicVariable(chaiscript::ChaiScript &a_script);
}

#endif
