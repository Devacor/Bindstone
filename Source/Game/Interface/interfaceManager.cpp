#include "interfaceManager.h"

namespace MV {

	Interface::Interface(const std::string &a_pageId, InterfaceManager& a_manager) :
		pageId(a_pageId),
		manager(a_manager) {

		initialize();
	}

	chaiscript::ChaiScript& Interface::hook(chaiscript::ChaiScript &a_script) {
		a_script.add(chaiscript::user_type<Interface>(), "Interface");

		a_script.add(chaiscript::fun(&Interface::show), "show");
		a_script.add(chaiscript::fun(&Interface::hide), "hide");

		a_script.add(chaiscript::fun([](Interface& a_self) {return a_self.manager;}), "manager");

		a_script.add(chaiscript::fun(&Interface::node), "root");
		a_script.add(chaiscript::fun(&Interface::scriptInitialize), "initialize");
		a_script.add(chaiscript::fun(&Interface::scriptShow), "onShow");
		a_script.add(chaiscript::fun(&Interface::scriptHide), "onHide");
		a_script.add(chaiscript::fun(&Interface::scriptUpdate), "update");
		a_script.add(chaiscript::fun(&Interface::pageId), "id");

		a_script.add(chaiscript::fun(&Interface::focus), "focus");
		a_script.add(chaiscript::fun(&Interface::removeFocus), "removeFocus");

		return a_script;
	}

	void Interface::show() {
		if (!node->active()) {
			initialize();
			node->active(true);
			if (scriptShow) {
				scriptShow(*this);
			}
		}
	}

	void Interface::hide() {
		if (node->active()) {
			removeFocus();
			if (scriptHide) {
				scriptHide(*this);
			}
			node->active(false);
		}
	}

	void Interface::focus(std::shared_ptr<MV::Scene::Text> a_textbox) {
		if (!activeText.expired()) {
			activeText.lock()->disableCursor();
		}
		manager.setActiveText(this);
		a_textbox->enableCursor();
		activeText = a_textbox;
	}

	void Interface::removeFocus() {
		manager.removeActiveText(this);
		if (!activeText.expired()) {
			activeText.lock()->disableCursor();
		}
		activeText.reset();
	}

	void Interface::initialize() {
		if (!node) {
			node = manager.root()->loadChild("Assets/Interface/" + pageId + "/view.scene", JsonNodeLoadBinder(manager.managers(), manager.mouse(), manager.script()), pageId);

			scriptFileEval("Assets/Interface/" + pageId + "/initialize.script", manager.script(), {
				{ "self", chaiscript::Boxed_Value(this) }
			});

			if (scriptInitialize) {
				scriptInitialize(*this);
			}

			manager.root()->add(node);
			node->active(false);
		}
	}

	InterfaceManager::InterfaceManager(std::shared_ptr<MV::Scene::Node> a_root, MouseState& a_mouse, Managers& a_managers, chaiscript::ChaiScript &a_script, std::string a_scriptName) :
		ourMouse(a_mouse),
		ourManagers(a_managers),
		ourScript(a_script),
		node(a_root->make(a_scriptName)->depth(1)) {

		scriptFileEval(a_scriptName, ourScript, {
			{ "self", chaiscript::Boxed_Value(this) }
		});

		if (scriptInitialize) {
			scriptInitialize(*this);
		}
	}

	chaiscript::ChaiScript& InterfaceManager::hook(chaiscript::ChaiScript &a_script) {
		Interface::hook(a_script);

		a_script.add(chaiscript::user_type<InterfaceManager>(), "InterfaceManager");

		//a_script.add(chaiscript::fun([](Interface& a_self) {  }), "make");
		a_script.add(chaiscript::fun(&InterfaceManager::make), "make");
		a_script.add(chaiscript::fun(&InterfaceManager::mouse), "mouse");
		a_script.add(chaiscript::fun(&InterfaceManager::script), "script");

		a_script.add(chaiscript::fun(&InterfaceManager::page), "page");

		a_script.add(chaiscript::fun(&InterfaceManager::node), "root");

		a_script.add(chaiscript::fun(&InterfaceManager::scriptInitialize), "initialize");

		//a_script.add(chaiscript::fun(&InterfaceManager::scriptUpdate), "update");

		return a_script;
	}

	void InterfaceManager::setActiveText(Interface* a_current) {
		if (activeTextPage && activeTextPage != a_current) {
			activeTextPage->removeFocus();
		}
		activeTextPage = a_current;
		SDL_StartTextInput();
	}

	void InterfaceManager::removeActiveText(Interface* a_current) {
		if (activeTextPage && activeTextPage == a_current) {
			activeTextPage = nullptr;
			SDL_StopTextInput();
		}
	}

}