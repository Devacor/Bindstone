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
		// Read each time the menu opens: a label that follows the editor's state, and whether the row can be chosen.
		std::function<std::string()> liveLabel;
		std::function<bool()> enabled;
		// Read each time the menu opens, in place of shortcut: the key bound to the row's action now.
		std::function<std::string()> liveShortcut;
	};

	// The menu titles in the title bar and the one dropdown open beneath them. Moving across the titles while a menu
	// is open switches menus; Escape or a press anywhere else closes it.
	class MenuBar {
	public:
		MenuBar(const std::shared_ptr<MV::Scene::Node>& a_barHost, const std::shared_ptr<MV::Scene::Node>& a_popupHost, MV::Services& a_services, const Theme& a_theme);

		void add(const std::string& a_title, std::vector<MenuItem> a_items);

		// A menu of its own at the pointer, for a right press: kept inside the window, closed the same ways.
		void openContext(const MV::Point<int>& a_screenPoint, const std::vector<MenuItem>& a_items);
		bool contextOpen() const { return contextShown; }
		// Whether the point is on the open menu's rows.
		bool popupContains(const MV::Point<int>& a_screenPoint) const;
		int rowCount() const { return static_cast<int>(rowLabels.size()); }

		float width() const { return nextTitleX; }
		bool isOpen() const { return openIndex >= 0 || contextShown; }
		void close();
		void handleInput(SDL_Event& a_event);

		// Centres on screen of a menu's title and of a row of the open menu.
		MV::Point<int> titleScreenPoint(int a_menu) const;
		MV::Point<int> rowScreenPoint(int a_row) const;
		// The open menu's rows as shown.
		const std::string& rowLabel(int a_row) const { return rowLabels[static_cast<size_t>(a_row)]; }
		bool rowEnabled(int a_row) const { return rowsEnabled[static_cast<size_t>(a_row)]; }

	private:
		struct Menu {
			std::string title;
			std::vector<MenuItem> items;
			std::shared_ptr<MV::Scene::Sprite> highlight;
			std::shared_ptr<MV::Scene::Node> titleNode;
			float x = 0.0f;
			float width = 0.0f;
		};

		void open(int a_index);
		void buildPopup(const std::vector<MenuItem>& a_items, const MV::Point<>& a_worldPosition);
		void hover(const MV::Point<int>& a_screenPoint);
		void hoverRow(int a_row);
		float textWidth(const std::string& a_text) const;
		float rowHeight() const { return theme.rowHeight + theme.px(2.0f); }
		MV::Point<> worldFromScreen(const MV::Point<int>& a_screenPoint) const;

		std::shared_ptr<MV::Scene::Node> barHost;
		std::shared_ptr<MV::Scene::Node> popupHost;
		MV::Services& services;
		const Theme& theme;

		std::vector<Menu> menus;
		float nextTitleX = 0.0f;
		int openIndex = -1;
		bool contextShown = false;

		std::shared_ptr<MV::Scene::Node> popup;
		MV::Size<> popupSize;
		std::vector<std::shared_ptr<MV::Scene::Sprite>> rowHighlights;
		std::vector<float> rowTops;
		std::vector<std::shared_ptr<MV::Scene::Node>> rowNodes;
		std::vector<std::string> rowLabels;
		std::vector<bool> rowsEnabled;
		int hoveredRow = -1;
		MV::TapDevice::SignalType hoverReceiver;
	};

}

#endif
