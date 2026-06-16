#ifndef _MV_DYNAMIC_VARIABLE_H_
#define _MV_DYNAMIC_VARIABLE_H_

#include <string>
#include <tuple>
#include <variant>
#include <map>
#include <jaiscript/serialization/archive.hpp>
#include "MV/Utility/exactType.hpp"

namespace MV {
	//I needed more debuggability and std::variant was throwing a lot during serialization. This is a wrapper around a variant which plays better in scripts.
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

		template<class Archive>
		void serialize(Archive& archive) {
			if constexpr (jai::serialization::is_save<Archive>) {
				int typeIndex = static_cast<int>(value.index());
				archive.serialize("type", typeIndex);
				std::visit([&archive](const auto& v) {
					using T = std::decay_t<decltype(v)>;
					if constexpr (!std::is_same_v<T, std::monostate>) {
						archive.serialize("value", v);
					}
				}, value);
			} else {
				int typeIndex = 0;
				archive.serialize("type", typeIndex);
				switch (typeIndex) {
					case 0: value = std::monostate{}; break;
					case 1: { bool v; archive.serialize("value", v); value = v; break; }
					case 2: { int64_t v; archive.serialize("value", v); value = v; break; }
					case 3: { double v; archive.serialize("value", v); value = v; break; }
					case 4: { std::string v; archive.serialize("value", v); value = v; break; }
				}
			}
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

	//Used similarly to optional during network serialization.
	template <typename T>
	struct DeltaVariable {
		DeltaVariable() = default;
		DeltaVariable(const T& a_value) :value(a_value) {}
		DeltaVariable(T&& a_value) :value(std::move(a_value)) {}
		DeltaVariable(const DeltaVariable<T>& a_value) :value(a_value.value), modified(true) {}
		DeltaVariable(DeltaVariable<T>&& a_value) : value(std::move(a_value.value)), modified(true) {}
		DeltaVariable& operator=(const T& a_rhs) {
			value = a_rhs;
			modified = true;
			return *this;
		}
		DeltaVariable<T>& operator=(const DeltaVariable<T>& a_rhs) {
			if (a_rhs.modified) {
				value = a_rhs.value;
				modified = true;
			}
			return *this;
		}

		const T& operator*() {
			return value;
		}
		const T* operator->() {
			return &value;
		}
		const T& view() const {
			return value;
		}
		T& modify() {
			modified = true;
			return value;
		}

		template <typename ArchiveType>
		auto& serialize(ArchiveType& a_archive, const std::string& a_archivedName, bool a_force = false) {
			if constexpr (jai::serialization::is_save<ArchiveType>) {
				bool hasValue = modified || a_force;
				a_archive.serialize((a_archivedName + "_has_value").c_str(), hasValue);
				if (hasValue) {
					a_archive.serialize(a_archivedName.c_str(), value);
				}
				modified = false;
			} else {
				a_archive.serialize((a_archivedName + "_has_value").c_str(), modified);
				if (modified) {
					a_archive.serialize(a_archivedName.c_str(), value);
				}
			}
			return a_archive;
		}

		template <typename ArchiveType>
		auto& serialize(ArchiveType& a_archive, MV::ExactType<bool> a_force = false) {
			if constexpr (jai::serialization::is_save<ArchiveType>) {
				bool hasValue = modified || static_cast<bool>(a_force);
				a_archive.serialize("has_value", hasValue);
				if (hasValue) {
					a_archive.serialize("value", value);
				}
				modified = false;
			} else {
				a_archive.serialize("has_value", modified);
				if (modified) {
					a_archive.serialize("value", value);
				}
			}
			return a_archive;
		}

	private:
		static inline std::map<size_t, bool> scriptHookedUp = std::map<size_t, bool>();
		bool modified = true;
		T value = {};
	};

	template <typename T>
	std::ostream& operator<<(std::ostream& a_os, const DeltaVariable<T>& a_dt) {
		a_os << a_dt.view();
		return a_os;
	}

	template <typename T>
	std::istream& operator>>(std::istream& a_is, const DeltaVariable<T>& a_dt) {
		a_is >> a_dt.view();
		return a_is;
	}

	template <typename T>
	bool operator==(const DeltaVariable<T>& a_rhs, const DeltaVariable<T>& a_lhs) { return a_rhs.view() == a_lhs.view(); }

	template <typename T>
	bool operator!=(const DeltaVariable<T>& a_rhs, const DeltaVariable<T>& a_lhs) { return a_rhs.view() != a_lhs.view(); }

	template <typename T>
	bool operator<(const DeltaVariable<T>& a_rhs, const DeltaVariable<T>& a_lhs) { return a_rhs.view() < a_lhs.view(); }

	template <typename T>
	bool operator>(const DeltaVariable<T>& a_rhs, const DeltaVariable<T>& a_lhs) { return a_rhs.view() > a_lhs.view(); }

	template <typename T>
	bool operator==(const T& a_rhs, const DeltaVariable<T>& a_lhs) { return a_rhs == a_lhs.view(); }

	template <typename T>
	bool operator!=(const T& a_rhs, const DeltaVariable<T>& a_lhs) { return a_rhs != a_lhs.view(); }

	template <typename T>
	bool operator<(const T& a_rhs, const DeltaVariable<T>& a_lhs) { return a_rhs < a_lhs.view(); }

	template <typename T>
	bool operator>(const T& a_rhs, const DeltaVariable<T>& a_lhs) { return a_rhs > a_lhs.view(); }

	template <typename T>
	bool operator==(const DeltaVariable<T>& a_rhs, const T& a_lhs) { return a_rhs.view() == a_lhs; }

	template <typename T>
	bool operator!=(const DeltaVariable<T>& a_rhs, const T& a_lhs) { return a_rhs.view() != a_lhs; }

	template <typename T>
	bool operator<(const DeltaVariable<T>& a_rhs, const T& a_lhs) { return a_rhs.view() < a_lhs; }

	template <typename T>
	bool operator>(const DeltaVariable<T>& a_rhs, const T& a_lhs) { return a_rhs.view() > a_lhs; }
}

#endif
