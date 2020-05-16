#include "InterfaceManager.h"

namespace MV {

	Interface::Interface(const std::string &a_pageId, InterfaceManager& a_manager) :
		pageId(a_pageId),
		manager(a_manager) {

		initialize();
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
			std::string nodePath = "Interface/" + pageId + "/view.scene";
			node = manager.root()->loadChild(nodePath, manager.managers().services, pageId);
			MV::require<MV::ResourceException>(node, "Node failed to load from: ", nodePath);
			manager.root()->add(node);
			node->active(false);

			manager.script().fileEval("InterfaceManager::initialize", "Interface/" + pageId + "/initialize.script", { { "self", chaiscript::Boxed_Value(this) } });
			if (scriptInitialize) {
				scriptInitialize(*this);
			}
		}
	}

	InterfaceManager::InterfaceManager(std::shared_ptr<MV::Scene::Node> a_root, TapDevice& a_mouse, Managers& a_managers, MV::Script &a_script, std::string a_scriptName) :
		ourMouse(a_mouse),
		ourManagers(a_managers),
		ourScript(a_script),
		node(a_root->make(a_scriptName)->depth(1)) {
	}

	InterfaceManager& InterfaceManager::initialize() {
		ourScript.fileEval("InterfaceManager::initialize", node->id(), { { "self", chaiscript::Boxed_Value(this) } });
		if (scriptInitialize) {
			scriptInitialize(*this); 
		}
		return *this;
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