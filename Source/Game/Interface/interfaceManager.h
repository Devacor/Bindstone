#ifndef _MV_INTERFACE_MANAGER_H_
#define _MV_INTERFACE_MANAGER_H_

#include "MV/Render/package.h"
#include "Game/state.h"
#include "MV/Utility/chaiscriptUtility.h"
#include "MV/Interface/mouse.h"

namespace MV {
	class InterfaceManager;

	class Interface {
	public:
		Interface() = delete;
		Interface(const Interface&) = delete;
		Interface& operator=(const Interface&) = delete;
		Interface(const std::string &a_pageId, InterfaceManager& a_manager);

		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);

		void show();
		void hide();
		bool visible() const;

		void update(double a_dt) {
			if (scriptUpdate) {
				scriptUpdate(*this, a_dt);
			}
		}

		std::string id() const {
			return pageId;
		}

		std::shared_ptr<MV::Scene::Node> root() {
			return node;
		}

		void focus(std::shared_ptr<MV::Scene::Text> a_textbox);

		void removeFocus();

		bool handleInput(SDL_Event &a_event) {
			if (auto lockedText = activeText.lock()) {
				lockedText->text(a_event);
			}
			return false;
		}

	private:
		void initialize();

		std::string pageId;
		InterfaceManager& manager;
		std::shared_ptr<MV::Scene::Node> node;

		std::weak_ptr<MV::Scene::Text> activeText;

		std::function<void(Interface&)> scriptInitialize;
		std::function<void(Interface&, double)> scriptUpdate;
		std::function<void(Interface&)> scriptShow;
		std::function<void(Interface&)> scriptHide;
	};

	class InterfaceManager {
	public:
		InterfaceManager(std::shared_ptr<MV::Scene::Node> a_root, TapDevice& a_mouse, Managers& a_managers, chaiscript::ChaiScript &a_script, std::string a_scriptName);
		InterfaceManager& initialize();
		TapDevice& mouse() {
			return ourMouse;
		}

		Interface& make(const std::string &a_pageId) {
			pages.push_back(std::make_unique<Interface>(a_pageId, *this));
			return *pages.back();
		}

		Managers& managers() {
			return ourManagers;
		}

		chaiscript::ChaiScript& script() {
			return ourScript;
		}

		void update(double a_dt) {
			for (auto&& p : pages) {
				if (p->visible()) {
					p->update(a_dt);
				}
			}
		}

		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);

		std::shared_ptr<MV::Scene::Node> root() {
			return node;
		}

		Interface& page(const std::string &a_pageId) {
			for (auto&& p : pages) {
				if (p->id() == a_pageId) {
					return *p;
				}
			}
			require<ResourceException>(false, "Failed to find page [", a_pageId, "]");
			return *pages[0]; //never hit in reality
		}

		bool handleInput(SDL_Event &a_event) {
			if (activeTextPage) {
				return activeTextPage->handleInput(a_event);
			}
			return false;
		}

		void setActiveText(Interface* a_current);
		void removeActiveText(Interface* a_current);
	private:
		std::vector<std::unique_ptr<Interface>> pages;

		TapDevice& ourMouse;
		Managers& ourManagers;
		chaiscript::ChaiScript& ourScript;

		std::shared_ptr<MV::Scene::Node> node;

		Interface* activeTextPage = nullptr;

		std::function<void(InterfaceManager&)> scriptInitialize;
	};
}

#endif
