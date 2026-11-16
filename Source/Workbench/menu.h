#ifndef _WORKBENCH_MENU_H_
#define _WORKBENCH_MENU_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "MV/Render/package.h"
#include "MV/Interface/tapDevice.h"
#include "theme.h"

namespace Workbench {

	struct MenuItem {
		std::string label;
		std::string shortcut;
		std::function<void()> action;
		bool separatorAbove = false;
	};

	// The menu titles in the title bar and the one dropdown open beneath them. Moving across the titles while a menu
	// is open switches menus; Escape or a press anywhere else closes it.
	class MenuBar {
	public:
		MenuBar(const std::shared_ptr<MV::Scene::Node>& a_barHost, const std::shared_ptr<MV::Scene::Node>& a_popupHost, MV::Services& a_services, const Theme& a_theme);

		void add(const std::string& a_title, std::vector<MenuItem> a_items);

		float width() const { return nextTitleX; }
		bool isOpen() const { return openIndex >= 0; }
		void close();
		void handleInput(SDL_Event& a_event);

	private:
		struct Menu {
			std::string title;
			std::vector<MenuItem> items;
			std::shared_ptr<MV::Scene::Sprite> highlight;
			float x = 0.0f;
			float width = 0.0f;
		};

		void open(int a_index);
		void hover(const MV::Point<int>& a_screenPoint);
		void hoverRow(int a_row);
		float textWidth(const std::string& a_text) const { return theme.px(7.4f) * static_cast<float>(a_text.size()); }
		float rowHeight() const { return theme.rowHeight + theme.px(2.0f); }
		MV::Point<> worldFromScreen(const MV::Point<int>& a_screenPoint) const;

		std::shared_ptr<MV::Scene::Node> barHost;
		std::shared_ptr<MV::Scene::Node> popupHost;
		MV::Services& services;
		const Theme& theme;

		std::vector<Menu> menus;
		float nextTitleX = 0.0f;
		int openIndex = -1;

		std::shared_ptr<MV::Scene::Node> popup;
		MV::Size<> popupSize;
		std::vector<std::shared_ptr<MV::Scene::Sprite>> rowHighlights;
		std::vector<float> rowTops;
		int hoveredRow = -1;
		MV::TapDevice::SignalType hoverReceiver;
	};

}

#endif
