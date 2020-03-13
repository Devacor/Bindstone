#ifndef _MV_DYNAMIC_VARIABLE_H_
#define _MV_DYNAMIC_VARIABLE_H_

#include <string>
#include <tuple>
#include <variant>
#include "cereal/cereal.hpp"
#include "MV/Utility/exactType.hpp"
#include "chaiscript/chaiscript.hpp"

namespace MV {
	//I needed more debuggability and std::variant was throwing a lot within Cereal. This is a wrapper around a variant which plays better in scripts.
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

		static void hook(chaiscript::ChaiScript& a_script);

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

		template <typename ArchiveType,
			typename std::enable_if_t<std::is_base_of<cereal::detail::OutputArchiveBase, ArchiveType>::value>* = nullptr>
			ArchiveType & serialize(ArchiveType & a_archive, const std::string & a_archivedName, bool a_force = false) {
			bool hasValue = modified || a_force;
			a_archive(cereal::make_nvp(a_archivedName + "_has_value", hasValue));
			if (hasValue) {
				a_archive(cereal::make_nvp(a_archivedName, value));
			}
			modified = false;
			return a_archive;
		}

		template <typename ArchiveType,
			typename std::enable_if_t<std::is_base_of<cereal::detail::InputArchiveBase, ArchiveType>::value>* = nullptr>
			ArchiveType & serialize(ArchiveType & a_archive, const std::string & a_archivedName, bool a_doNothing = false) {
			a_archive(cereal::make_nvp(a_archivedName + "_has_value", modified));
			if (modified) {
				a_archive(cereal::make_nvp(a_archivedName, value));
			}
			return a_archive;
		}

		template <typename ArchiveType,
			typename std::enable_if_t<std::is_base_of<cereal::detail::OutputArchiveBase, ArchiveType>::value>* = nullptr>
			ArchiveType & serialize(ArchiveType & a_archive, MV::ExactType<bool> a_force = false) {
			bool hasValue = modified || a_force;
			a_archive(hasValue);
			if (hasValue) {
				a_archive(value);
			}
			modified = false;
			return a_archive;
		}

		template <typename ArchiveType,
			typename std::enable_if_t<std::is_base_of<cereal::detail::InputArchiveBase, ArchiveType>::value>* = nullptr>
			ArchiveType & serialize(ArchiveType & a_archive, MV::ExactType<bool> a_doNothing = false) {
			a_archive(modified);
			if (modified) {
				a_archive(value);
			}
			return a_archive;
		}

		static void hook(chaiscript::ChaiScript& a_script);
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

	template<typename T>
	void DeltaVariable<T>::hook(chaiscript::ChaiScript& a_script) {
		if (!scriptHookedUp[reinterpret_cast<size_t>(&a_script)]) {
			a_script.add(chaiscript::fun([&](DeltaVariable<T>& a_self, const T &a_value) -> decltype(auto) {
				return a_self = a_value;
			}), "=");
			a_script.add(chaiscript::fun([&](T& a_self, const DeltaVariable<T> &a_value) -> decltype(auto) {
				return a_self = a_value.view();
			}), "=");

			a_script.add(chaiscript::fun([&](DeltaVariable<T>& a_self, const T& a_value) -> decltype(auto) {
				return a_self == a_value;
			}), "==");
			a_script.add(chaiscript::fun([&](T& a_self, const DeltaVariable<T>& a_value) -> decltype(auto) {
				return a_self == a_value.view();
			}), "==");

			a_script.add(chaiscript::fun([&](DeltaVariable<T>& a_self, const T& a_value) -> decltype(auto) {
				return a_self != a_value;
			}), "!=");
			a_script.add(chaiscript::fun([&](T& a_self, const DeltaVariable<T>& a_value) -> decltype(auto) {
				return a_self != a_value.view();
			}), "!=");

			a_script.add(chaiscript::fun([&](DeltaVariable<T>& a_self) -> decltype(auto) {
				return a_self.view();
			}), "view");
			a_script.add(chaiscript::fun([&](DeltaVariable<T>& a_self) -> decltype(auto) {
				return a_self.modify();
			}), "modify");
		}
	}
}

#endif
