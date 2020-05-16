#include "Game/Interface/interfaceManager.h"

namespace MV {
	template<>
	void MV::Script::Registrar<Interface>::privateAccess(chaiscript::ChaiScript& a_script, const MV::Services& a_services){
		a_script.add(chaiscript::user_type<Interface>(), "Interface");

		a_script.add(chaiscript::fun(&Interface::show), "show");
		a_script.add(chaiscript::fun(&Interface::hide), "hide");
		a_script.add(chaiscript::fun(&Interface::visible), "visible");

		a_script.add(chaiscript::fun([](Interface& a_self) -> auto& {return a_self.manager; }), "manager");

		a_script.add(chaiscript::fun(&Interface::node), "root");
		a_script.add(chaiscript::fun(&Interface::scriptInitialize), "initialize");
		a_script.add(chaiscript::fun(&Interface::scriptShow), "onShow");
		a_script.add(chaiscript::fun(&Interface::scriptHide), "onHide");
		a_script.add(chaiscript::fun(&Interface::scriptUpdate), "update");
		a_script.add(chaiscript::fun(&Interface::pageId), "id");

		a_script.add(chaiscript::fun(&Interface::focus), "focus");
		a_script.add(chaiscript::fun(&Interface::removeFocus), "removeFocus");
	}
	MV::Script::Registrar<Interface> _hookInterface {};

	template<>
	void MV::Script::Registrar<InterfaceManager>::privateAccess(chaiscript::ChaiScript& a_script, const MV::Services& a_services){
		a_script.add(chaiscript::user_type<InterfaceManager>(), "InterfaceManager");

		//a_script.add(chaiscript::fun([](Interface& a_self) {  }), "make");
		a_script.add(chaiscript::fun(&InterfaceManager::make), "make");
		a_script.add(chaiscript::fun(&InterfaceManager::mouse), "mouse");
		a_script.add(chaiscript::fun(&InterfaceManager::script), "script");

		a_script.add(chaiscript::fun(&InterfaceManager::page), "page");

		a_script.add(chaiscript::fun(&InterfaceManager::node), "root");

		a_script.add(chaiscript::fun(&InterfaceManager::scriptInitialize), "initialize");
	}
	MV::Script::Registrar<InterfaceManager> _hookInterfaceManager{};
}
