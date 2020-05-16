#include "dynamicVariable.h"
#include "network.h"

namespace MV {
	Script::Registrar<DynamicVariable> _hookDynamicVariable([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<DynamicVariable>(), "DynamicVariable");
		a_script.add(chaiscript::constructor<DynamicVariable()>(), "DynamicVariable");
		a_script.add(chaiscript::constructor<DynamicVariable(bool)>(), "DynamicVariable");
		a_script.add(chaiscript::constructor<DynamicVariable(int64_t)>(), "DynamicVariable");
		a_script.add(chaiscript::constructor<DynamicVariable(int)>(), "DynamicVariable");
		a_script.add(chaiscript::constructor<DynamicVariable(size_t)>(), "DynamicVariable");
		a_script.add(chaiscript::constructor<DynamicVariable(double)>(), "DynamicVariable");
		a_script.add(chaiscript::constructor<DynamicVariable(const std::string &)>(), "DynamicVariable");

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

		a_script.add(chaiscript::bootstrap::standard_library::map_type<std::map<std::string, DynamicVariable>>("DynamicVariableMap"));
		a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<DynamicVariable>>("DynamicVariableVector"));
	});


	template<typename T>
	void hookDeltaVariable(chaiscript::ChaiScript& a_script) {
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

	Script::Registrar<DeltaVariable<int32_t>> _hookDeltaVariable([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		hookDeltaVariable<int32_t>(a_script);
		hookDeltaVariable<std::string>(a_script);
		hookDeltaVariable<MV::Point<MV::PointPrecision>>(a_script);
		hookDeltaVariable<bool>(a_script);
		hookDeltaVariable<double>(a_script);
	});

	Script::Registrar<Client> _hookClient([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<Client>(), "Client");

		a_script.add(chaiscript::fun(&Client::send), "send");
		a_script.add(chaiscript::fun(&Client::connected), "connected");
		a_script.add(chaiscript::fun([](Client& a_self) { a_self.disconnect(); }), "disconnect");
		a_script.add(chaiscript::fun([](Client& a_self) { a_self.reconnect(); }), "reconnect");
	});
}