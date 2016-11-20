#ifndef _MV_INTERFACE_MANAGER_H_
#define _MV_INTERFACE_MANAGER_H_

#include "Render/package.h"
#include "Game/state.h"
#include "Utility/chaiscriptUtility.h"
#include "Interface/mouse.h"

namespace MV {
	class InterfaceManager;

	class Interface {
	public:
		Interface(const std::string &a_pageId, InterfaceManager& a_manager);

		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);

		void show();

		void hide();

		void update(double a_dt) {
			if (scriptUpdate) {
				scriptUpdate(*this, a_dt);
			}
		}

		std::string id() {
			return pageId;
		}

		std::shared_ptr<MV::Scene::Node> root() {
			return node;
		}

	private:
		void initialize();

		std::string pageId;
		InterfaceManager& manager;
		std::shared_ptr<MV::Scene::Node> node;

		std::function<void(Interface&)> scriptInitialize;
		std::function<void(Interface&, double)> scriptUpdate;
		std::function<void(Interface&)> scriptShow;
		std::function<void(Interface&)> scriptHide;
	};

	class InterfaceManager {
	public:
		InterfaceManager(std::shared_ptr<MV::Scene::Node> a_root, MouseState& a_mouse, Managers& a_managers, chaiscript::ChaiScript &a_script, std::string a_scriptName);

		MouseState& mouse() {
			return ourMouse;
		}

		Interface& make(const std::string &a_pageId) {
			pages.emplace_back(a_pageId, *this);
			return pages.back();
		}

		Managers& managers() {
			return ourManagers;
		}

		chaiscript::ChaiScript& script() {
			return ourScript;
		}

		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);

		std::shared_ptr<MV::Scene::Node> root() {
			return node;
		}

		Interface& page(const std::string &a_pageId) {
			for (auto&& p : pages) {
				if (p.id() == a_pageId) {
					return p;
				}
			}
			require<ResourceException>(false, "Failed to find page [", a_pageId, "]");
		}

	private:
		std::vector<Interface> pages;

		MouseState& ourMouse;
		Managers& ourManagers;
		chaiscript::ChaiScript& ourScript;

		std::shared_ptr<MV::Scene::Node> node;

		std::function<void(InterfaceManager&)> scriptInitialize;
	};
}

#endif
