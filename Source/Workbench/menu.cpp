#include "menu.h"
#include "widgets.h"
#include <algorithm>

namespace Workbench {

	namespace {
		// above every panel's Clickables, so a row takes the press that lands on it
		constexpr int64_t menuPriority = 10000;
		const MV::Color menuFill(0x1b1b1b);
		const MV::Color menuShadow(0.0f, 0.0f, 0.0f, 0.45f);
		// text markup: a row that cannot be chosen is drawn in grey
		const std::string disabledText = "[[c|0.42:0.42:0.42]]";
	}

	MenuBar::MenuBar(const std::shared_ptr<MV::Scene::Node>& a_barHost, const std::shared_ptr<MV::Scene::Node>& a_popupHost, MV::Services& a_services, const Theme& a_theme) :
		barHost(a_barHost),
		popupHost(a_popupHost),
		services(a_services),
		theme(a_theme) {

		hoverReceiver = services.get<MV::TapDevice>()->onMove.connect("menuHover", [this](MV::TapDevice& a_mouse) {
			hover(a_mouse.position());
		});
	}

	float MenuBar::textWidth(const std::string& a_text) const {
		return labelTextWidth(services, theme, a_text);
	}

	MV::Point<> MenuBar::worldFromScreen(const MV::Point<int>& a_screenPoint) const {
		return barHost->renderer().worldFromScreen(a_screenPoint);
	}

	void MenuBar::add(const std::string& a_title, std::vector<MenuItem> a_items) {
		auto& mouse = *services.get<MV::TapDevice>();
		Menu menu;
		menu.title = a_title;
		menu.items = std::move(a_items);
		menu.x = nextTitleX;
		menu.width = textWidth(a_title) + theme.px(18.0f);

		MV::Size<> titleSize(menu.width, theme.titleBarHeight - theme.pad * 2.0f);
		auto titleNode = barHost->make(MV::guid("menuTitle_"));
		titleNode->position(MV::point(menu.x, theme.pad));
		menu.titleNode = titleNode;
		menu.highlight = attachRoundedRect(titleNode, services, theme, titleSize, theme.buttonIdle());
		menu.highlight->hide();
		auto label = makeLabel(titleNode, services, theme, "label", titleSize, a_title);
		label->justification(MV::TextJustification::CENTER);

		int index = static_cast<int>(menus.size());
		titleNode->attach<MV::Scene::Clickable>(mouse)->bounds(titleSize)->onAccept.connect("toggle", [this, index](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (openIndex == index) {
				close();
			} else {
				open(index);
			}
		});

		nextTitleX += menu.width;
		menus.push_back(std::move(menu));
	}

	void MenuBar::close() {
		if (openIndex >= 0) {
			menus[static_cast<size_t>(openIndex)].highlight->hide();
		}
		openIndex = -1;
		contextShown = false;
		hoveredRow = -1;
		rowHighlights.clear();
		rowTops.clear();
		rowNodes.clear();
		rowLabels.clear();
		rowsEnabled.clear();
		if (popup) {
			popup->removeFromParent();
			popup.reset();
		}
	}

	void MenuBar::open(int a_index) {
		close();
		openIndex = a_index;
		auto& menu = menus[static_cast<size_t>(a_index)];
		menu.highlight->show();
		buildPopup(menu.items, MV::point(barHost->worldPosition().x + menu.x, theme.titleBarHeight - theme.px(1.0f)));
	}

	void MenuBar::openContext(const MV::Point<int>& a_screenPoint, const std::vector<MenuItem>& a_items) {
		close();
		if (a_items.empty()) {
			return;
		}
		contextShown = true;
		buildPopup(a_items, worldFromScreen(a_screenPoint));
		auto worldSize = barHost->renderer().world().size();
		auto at = popup->position();
		popup->position(MV::point(std::max(0.0f, std::min(at.x, worldSize.width - popupSize.width)), std::max(0.0f, std::min(at.y, worldSize.height - popupSize.height))));
		hover(services.get<MV::TapDevice>()->position());
	}

	bool MenuBar::popupContains(const MV::Point<int>& a_screenPoint) const {
		if (!popup) {
			return false;
		}
		auto local = worldFromScreen(a_screenPoint) - popup->position();
		return local.x >= 0.0f && local.y >= 0.0f && local.x < popupSize.width && local.y < popupSize.height;
	}

	void MenuBar::buildPopup(const std::vector<MenuItem>& a_items, const MV::Point<>& a_worldPosition) {
		auto& mouse = *services.get<MV::TapDevice>();

		float labelColumn = 0.0f;
		float shortcutColumn = 0.0f;
		for (auto&& item : a_items) {
			rowLabels.push_back(item.liveLabel ? item.liveLabel() : item.label);
			rowsEnabled.push_back(!item.enabled || item.enabled());
			labelColumn = std::max(labelColumn, textWidth(rowLabels.back()));
			shortcutColumn = std::max(shortcutColumn, textWidth(item.shortcut));
		}
		const float inset = theme.px(4.0f);
		const float textInset = theme.px(12.0f);
		const float separatorHeight = theme.px(7.0f);
		float rowWidth = std::max(theme.px(180.0f), labelColumn + shortcutColumn + textInset * 2.0f + theme.px(32.0f));

		float height = inset * 2.0f;
		for (auto&& item : a_items) {
			height += rowHeight() + (item.separatorAbove ? separatorHeight : 0.0f);
		}
		popupSize = MV::size(rowWidth + inset * 2.0f, height);

		popup = popupHost->make("menuPopup");
		popup->position(a_worldPosition);

		auto shadow = popup->make("shadow");
		shadow->position(MV::point(theme.px(2.0f), theme.px(3.0f)));
		attachRoundedRect(shadow, services, theme, popupSize, menuShadow);
		attachOutlinedRect(popup->make("body"), services, theme, popupSize, menuFill, theme.fieldOutline());

		float y = inset;
		int rowIndex = 0;
		for (auto&& item : a_items) {
			if (item.separatorAbove) {
				auto separator = popup->make(MV::guid("separator_"));
				separator->position(MV::point(inset, y + std::floor(separatorHeight * 0.5f)));
				separator->attach<MV::Scene::Sprite>()->bounds(MV::size(rowWidth, theme.px(1.0f)))->color(theme.fieldOutline());
				y += separatorHeight;
			}
			auto row = popup->make(MV::guid("row_"));
			row->position(MV::point(inset, y));
			MV::Size<> rowSize(rowWidth, rowHeight());
			auto highlight = attachRoundedRect(row, services, theme, rowSize, theme.accent());
			highlight->hide();
			rowHighlights.push_back(highlight);
			rowTops.push_back(y);
			rowNodes.push_back(row);

			bool enabled = rowsEnabled[static_cast<size_t>(rowIndex)];
			std::string tint { enabled ? std::string() : disabledText };
			auto label = makeLabel(row, services, theme, "label", MV::size(rowWidth - textInset * 2.0f, rowSize.height), tint + rowLabels[static_cast<size_t>(rowIndex)]);
			label->owner()->position(MV::point(textInset, 0.0f));
			if (!item.shortcut.empty()) {
				auto shortcut = makeLabel(row, services, theme, "shortcut", MV::size(rowWidth - textInset * 2.0f, rowSize.height), tint + item.shortcut);
				shortcut->owner()->position(MV::point(textInset, 0.0f));
				shortcut->justification(MV::TextJustification::RIGHT);
			}

			// a row that cannot be chosen still takes its press, so it does not reach the panel beneath
			auto action = enabled ? item.action : std::function<void()>();
			row->attach<MV::Scene::Clickable>(mouse)->bounds(rowSize)->globalPriority(menuPriority + rowIndex)->onAccept.connect("choose", [this, action, enabled](const std::shared_ptr<MV::Scene::Clickable>&) {
				if (!enabled) {
					return;
				}
				auto chosen = action;   // close() destroys this row and the closure with it
				close();
				if (chosen) {
					chosen();
				}
			});
			y += rowSize.height;
			++rowIndex;
		}
		hover(mouse.position());
	}

	void MenuBar::hoverRow(int a_row) {
		if (a_row == hoveredRow) {
			return;
		}
		if (hoveredRow >= 0 && hoveredRow < static_cast<int>(rowHighlights.size())) {
			rowHighlights[static_cast<size_t>(hoveredRow)]->hide();
		}
		hoveredRow = a_row;
		if (hoveredRow >= 0) {
			rowHighlights[static_cast<size_t>(hoveredRow)]->show();
		}
	}

	void MenuBar::hover(const MV::Point<int>& a_screenPoint) {
		if (!isOpen() || !popup) {
			return;
		}
		auto world = worldFromScreen(a_screenPoint);
		auto barOrigin = barHost->worldPosition();
		if (!contextShown && world.y >= 0.0f && world.y < theme.titleBarHeight) {
			for (int index = 0; index < static_cast<int>(menus.size()); ++index) {
				auto& menu = menus[static_cast<size_t>(index)];
				float left = barOrigin.x + menu.x;
				if (index != openIndex && world.x >= left && world.x < left + menu.width) {
					open(index);
					return;
				}
			}
		}
		auto local = world - popup->position();
		int row = -1;
		if (local.x >= 0.0f && local.x < popupSize.width) {
			for (int index = 0; index < static_cast<int>(rowTops.size()); ++index) {
				if (local.y >= rowTops[static_cast<size_t>(index)] && local.y < rowTops[static_cast<size_t>(index)] + rowHeight()) {
					row = rowsEnabled[static_cast<size_t>(index)] ? index : -1;
					break;
				}
			}
		}
		hoverRow(row);
	}

	MV::Point<int> MenuBar::titleScreenPoint(int a_menu) const {
		return menus[static_cast<size_t>(a_menu)].titleNode->screenBounds().centerPoint();
	}

	MV::Point<int> MenuBar::rowScreenPoint(int a_row) const {
		return rowNodes[static_cast<size_t>(a_row)]->screenBounds().centerPoint();
	}

	void MenuBar::handleInput(SDL_Event& a_event) {
		if (!isOpen()) {
			return;
		}
		if (a_event.type == SDL_KEYDOWN && a_event.key.keysym.sym == SDLK_ESCAPE) {
			close();
		} else if (a_event.type == SDL_MOUSEBUTTONDOWN) {
			MV::Point<int> pressed(a_event.button.x, a_event.button.y);
			auto world = worldFromScreen(pressed);
			bool insidePopup = popupContains(pressed);
			auto barOrigin = barHost->worldPosition();
			bool onTitles = !contextShown && world.y >= 0.0f && world.y < theme.titleBarHeight && world.x >= barOrigin.x && world.x < barOrigin.x + nextTitleX;
			if (!insidePopup && !onTitles) {
				close();
			}
		}
	}

}
