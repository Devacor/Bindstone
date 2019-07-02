#include "InterfaceManager.h"

namespace MV {

	Interface::Interface(const std::string &a_pageId, InterfaceManager& a_manager) :
		pageId(a_pageId),
		manager(a_manager) {

		initialize();
	}

	chaiscript::ChaiScript& Interface::hook(chaiscript::ChaiScript &a_script) {
		a_script.add(chaiscript::user_type<Interface>(), "MV/Interface");

		a_script.add(chaiscript::fun(&Interface::show), "show");
		a_script.add(chaiscript::fun(&Interface::hide), "hide");
		a_script.add(chaiscript::fun(&Interface::visible), "visible");

		a_script.add(chaiscript::fun([](Interface& a_self) -> auto& {return a_self.manager;}), "manager");

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
			node->active(true);
			node->depth(static_cast<MV::PointPrecision>(node->parent()->size())+1.0f);
			node->parent()->normalizeDepth();
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

	bool Interface::visible() const {
		return node && node->active();
	}

	void Interface::focus(std::shared_ptr<MV::Scene::Text> a_textbox) {
		if (auto lockedText = activeText.lock()) {
			lockedText->disableCursor();
		}
		manager.setActiveText(this);
		a_textbox->enableCursor();
		activeText = a_textbox;
	}

	void Interface::removeFocus() {
		manager.removeActiveText(this);
		if (auto lockedText = activeText.lock()) {
			lockedText->disableCursor();
		}
		activeText.reset();
	}

	void Interface::initialize() {
		if (!node) {
			std::string nodePath = "Assets/Interface/" + pageId + "/view.scene";
			node = manager.root()->loadChild(nodePath, manager.managers().services, pageId);
			MV::require<MV::ResourceException>(node, "Node failed to load from: ", nodePath);
			manager.root()->add(node);
			node->active(false);

			scriptExceptionWrapper("InterfaceManager::initialize", [&] {
				scriptFileEval("Assets/Interface/" + pageId + "/initialize.script", manager.script(), {
					{ "self", chaiscript::Boxed_Value(this) }
				});
			});
			if (scriptInitialize) {
				scriptExceptionWrapper("InterfaceManager::initialize", [&] {
					scriptInitialize(*this);
				});
			}
		}
	}

	InterfaceManager::InterfaceManager(std::shared_ptr<MV::Scene::Node> a_root, TapDevice& a_mouse, Managers& a_managers, chaiscript::ChaiScript &a_script, std::string a_scriptName) :
		ourMouse(a_mouse),
		ourManagers(a_managers),
		ourScript(a_script),
		node(a_root->make(a_scriptName)->depth(1)) {
	}

	InterfaceManager& InterfaceManager::initialize() {
		scriptExceptionWrapper("InterfaceManager::initialize", [&]{
			scriptFileEval(node->id(), ourScript, {
				{ "self", chaiscript::Boxed_Value(this) }
			});
		});
		if (scriptInitialize) {
			scriptExceptionWrapper("InterfaceManager::initialize", [&] {
				scriptInitialize(*this);
			}); 
		}
		return *this;
	}

	chaiscript::ChaiScript& InterfaceManager::hook(chaiscript::ChaiScript &a_script) {
		Interface::hook(a_script);

		a_script.add(chaiscript::user_type<InterfaceManager>(), "MV/InterfaceManager");

		//a_script.add(chaiscript::fun([](Interface& a_self) {  }), "make");
		a_script.add(chaiscript::fun(&InterfaceManager::make), "make");
		a_script.add(chaiscript::fun(&InterfaceManager::mouse), "mouse");
		a_script.add(chaiscript::fun(&InterfaceManager::script), "script");

		a_script.add(chaiscript::fun(&InterfaceManager::page), "page");

		a_script.add(chaiscript::fun(&InterfaceManager::node), "root");

		a_script.add(chaiscript::fun(&InterfaceManager::scriptInitialize), "initialize");

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