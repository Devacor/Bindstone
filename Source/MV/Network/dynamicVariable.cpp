#include "DynamicVariable.h"
#include "chaiscript/chaiscript.hpp"
#include "chaiscript/chaiscript_stdlib.hpp"

namespace MV {
	void hookDynamicVariable(chaiscript::ChaiScript &a_script) {
		a_script.add(chaiscript::user_type<DynamicVariable>(), "DynamicVariable");

		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, bool a_value) -> decltype(auto) {
			return a_self = a_value;
		}), "=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, int64_t a_value) -> decltype(auto) {
			return a_self = a_value;
		}), "=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, size_t a_value) -> decltype(auto) {
			return a_self = a_value;
		}), "=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, int a_value) -> decltype(auto) {
			return a_self = a_value;
		}), "=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, double a_value) -> decltype(auto) {
			return a_self = a_value;
		}), "=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, std::string a_value) -> decltype(auto) {
			return a_self = a_value;
		}), "=");

		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, bool a_value) -> decltype(auto) {
			return a_self == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, int64_t a_value) -> decltype(auto) {
			return a_self == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, size_t a_value) -> decltype(auto) {
			return a_self == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, int a_value) -> decltype(auto) {
			return a_self == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, double a_value) -> decltype(auto) {
			return a_self == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, std::string a_value) -> decltype(auto) {
			return a_self == a_value;
		}), "==");

		a_script.add(chaiscript::fun([&](bool a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](int64_t a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](size_t a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](int a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](double a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](std::string a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self == a_value;
		}), "==");

		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, bool a_value) -> decltype(auto) {
			return a_self != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, int64_t a_value) -> decltype(auto) {
			return a_self != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, size_t a_value) -> decltype(auto) {
			return a_self != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, int a_value) -> decltype(auto) {
			return a_self != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, double a_value) -> decltype(auto) {
			return a_self != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, std::string a_value) -> decltype(auto) {
			return a_self != a_value;
		}), "!=");

		a_script.add(chaiscript::fun([&](bool a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](int64_t a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](size_t a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](int a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](double a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](std::string a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self != a_value;
		}), "!=");

		a_script.add(chaiscript::fun([&](DynamicVariable &a_self) {
			return a_self.getBool();
		}), "bool");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self) {
			return a_self.getInt();
		}), "int");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self) {
			return a_self.getDouble();
		}), "double");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self) {
			return a_self.getString();
		}), "string");

		a_script.add(chaiscript::fun([&](DynamicVariable& a_self) {
			return a_self.clear();
		}), "clear");

		a_script.add(chaiscript::bootstrap::standard_library::map_type<std::map<std::string, DynamicVariable>>("MapDynamicVariable"));
		a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<DynamicVariable>>("VectorDynamicVariable"));
	}
	/*//For Boost Variant Version
	void hookDynamicVariable(chaiscript::ChaiScript &a_script) {
		a_script.add(chaiscript::user_type<DynamicVariable>(), "DynamicVariable");

		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, bool a_value) -> decltype(auto) {
			return a_self = a_value;
		}), "=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, int64_t a_value) -> decltype(auto) {
			return a_self = a_value;
		}), "=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, size_t a_value) -> decltype(auto) {
			return a_self = static_cast<int64_t>(a_value);
		}), "=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, int a_value) -> decltype(auto) {
			return a_self = static_cast<int64_t>(a_value);
		}), "=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, double a_value) -> decltype(auto) {
			return a_self = a_value;
		}), "=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, std::string a_value) -> decltype(auto) {
			return a_self = a_value;
		}), "=");

		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, bool a_value) -> decltype(auto) {
			return !a_self.empty() && boost::get<bool>(a_self) == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, int64_t a_value) -> decltype(auto) {
			return !a_self.empty() && boost::get<int64_t>(a_self) == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, size_t a_value) -> decltype(auto) {
			return !a_self.empty() && boost::get<int64_t>(a_self) == static_cast<int64_t>(a_value);
		}), "==");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, int a_value) -> decltype(auto) {
			return !a_self.empty() && boost::get<int64_t>(a_self) == static_cast<int64_t>(a_value);
		}), "==");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, double a_value) -> decltype(auto) {
			return !a_self.empty() && boost::get<double>(a_self) == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, std::string a_value) -> decltype(auto) {
			return !a_self.empty() && boost::get<std::string>(a_self) == a_value;
		}), "==");

		a_script.add(chaiscript::fun([&](bool a_value, DynamicVariable &a_self) -> decltype(auto) {
			return !a_self.empty() && boost::get<bool>(a_self) == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](int64_t a_value, DynamicVariable &a_self) -> decltype(auto) {
			return !a_self.empty() && boost::get<int64_t>(a_self) == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](size_t a_value, DynamicVariable &a_self) -> decltype(auto) {
			return !a_self.empty() && boost::get<int64_t>(a_self) == static_cast<int64_t>(a_value);
		}), "==");
		a_script.add(chaiscript::fun([&](int a_value, DynamicVariable &a_self) -> decltype(auto) {
			return !a_self.empty() && boost::get<int64_t>(a_self) == static_cast<int64_t>(a_value);
		}), "==");
		a_script.add(chaiscript::fun([&](double a_value, DynamicVariable &a_self) -> decltype(auto) {
			return !a_self.empty() && boost::get<double>(a_self) == a_value;
		}), "==");
		a_script.add(chaiscript::fun([&](std::string a_value, DynamicVariable &a_self) -> decltype(auto) {
			return !a_self.empty() && boost::get<std::string>(a_self) == a_value;
		}), "==");

		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, bool a_value) -> decltype(auto) {
			return a_self.empty() || boost::get<bool>(a_self) != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, int64_t a_value) -> decltype(auto) {
			return a_self.empty() || boost::get<int64_t>(a_self) != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, size_t a_value) -> decltype(auto) {
			return a_self.empty() || boost::get<int64_t>(a_self) != static_cast<int64_t>(a_value);
		}), "!=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, int a_value) -> decltype(auto) {
			return a_self.empty() || boost::get<int64_t>(a_self) != static_cast<int64_t>(a_value);
		}), "!=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, double a_value) -> decltype(auto) {
			return a_self.empty() || boost::get<double>(a_self) != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self, std::string a_value) -> decltype(auto) {
			return a_self.empty() || boost::get<std::string>(a_self) != a_value;
		}), "!=");

		a_script.add(chaiscript::fun([&](bool a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self.empty() || boost::get<bool>(a_self) != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](int64_t a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self.empty() || boost::get<int64_t>(a_self) != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](size_t a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self.empty() || boost::get<int64_t>(a_self) != static_cast<int64_t>(a_value);
		}), "!=");
		a_script.add(chaiscript::fun([&](int a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self.empty() || boost::get<int64_t>(a_self) != static_cast<int64_t>(a_value);
		}), "!=");
		a_script.add(chaiscript::fun([&](double a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self.empty() || boost::get<double>(a_self) != a_value;
		}), "!=");
		a_script.add(chaiscript::fun([&](std::string a_value, DynamicVariable &a_self) -> decltype(auto) {
			return a_self.empty() || boost::get<std::string>(a_self) != a_value;
		}), "!=");

		a_script.add(chaiscript::fun([&](DynamicVariable &a_self) -> decltype(auto) {
			return a_self.empty() ? false : boost::get<bool>(a_self);
		}), "bool");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self) -> decltype(auto) {
			return a_self.empty() ? 0 : boost::get<int64_t>(a_self);
		}), "int");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self) -> decltype(auto) {
			return a_self.empty() ? 0.0 : boost::get<double>(a_self);
		}), "double");
		a_script.add(chaiscript::fun([&](DynamicVariable &a_self) -> decltype(auto) {
			return a_self.empty() ? "" : boost::get<std::string>(a_self);
		}), "string");

		a_script.add(chaiscript::bootstrap::standard_library::map_type<std::map<std::string, DynamicVariable>>("MapDynamicVariable"));
		a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<DynamicVariable>>("VectorDynamicVariable"));
	}
	*/
}