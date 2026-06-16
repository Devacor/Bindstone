#include "InterfaceManager.h"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include "Game/state.h"

static jai::registrar<MV::Interface, MV::Services> _hookInterface("Interface",
	[](jai::dynamic_binder<MV::Interface>& builder, const MV::Services&) {
	builder.auto_bind();

	builder.method("show", &MV::Interface::show);
	builder.method("hide", &MV::Interface::hide);
	builder.method("visible", &MV::Interface::visible);
	builder.method("id", &MV::Interface::id);
	builder.method("root", &MV::Interface::root);
	builder.method("focus", &MV::Interface::focus);
	builder.method("removeFocus", &MV::Interface::removeFocus);
});

static jai::registrar<MV::InterfaceManager, MV::Services> _hookInterfaceManager("InterfaceManager",
	[](jai::dynamic_binder<MV::InterfaceManager>& builder, const MV::Services&) {
	builder.auto_bind();

	// Note: make() and page() return Interface& which is non-copyable (has reference member)
	// value_converter fallback tries to copy, which fails. These need proper registration or
	// should return shared_ptr instead.
	builder.method("root", &MV::InterfaceManager::root);
});

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

			if (scriptInitialize) {
				scriptInitialize(*this);
			}
		}
	}

	InterfaceManager::InterfaceManager(std::shared_ptr<MV::Scene::Node> a_root, TapDevice& a_mouse, Managers& a_managers, jai::engine& a_engine, std::string a_scriptName) :
		ourMouse(a_mouse),
		ourManagers(a_managers),
		jaiEngine_(a_engine),
		node(a_root->make(a_scriptName)->depth(1)) {
	}

	InterfaceManager& InterfaceManager::initialize() {
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

	void Interface::jai_auto_bind(jai::dynamic_binder<Interface>& builder) {
		// Note: manager() not bound - InterfaceManager contains non-copyable members (unique_ptr vector)
		// and value_converter would try to copy it if not registered. Access via scripts not needed.
		builder.property("initialize", &Interface::scriptInitialize);
		builder.property("onShow", &Interface::scriptShow);
		builder.property("onHide", &Interface::scriptHide);
		builder.property("update", &Interface::scriptUpdate);
	}

	void InterfaceManager::jai_auto_bind(jai::dynamic_binder<InterfaceManager>& builder) {
		builder.property("initialize", &InterfaceManager::scriptInitialize);
	}

}