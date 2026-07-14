#ifndef _WORKBENCH_THEME_H_
#define _WORKBENCH_THEME_H_

#include <string>
#include "MV/Render/points.h"

namespace Workbench {

	// Dark editor palette; hex literals feed MV::Color(uint32_t).
	namespace ThemeColor {
		enum : uint32_t {
			WINDOW_BG = 0x1b1d20,
			PANEL_BG = 0x232629,
			PANEL_ALT = 0x2a2e32,
			HEADER = 0x17191b,
			TAB_ACTIVE = 0x2f3439,
			TAB_IDLE = 0x1f2224,
			SPLITTER = 0x101214,
			ACCENT = 0x3d7bd9,
			SELECTION = 0x2f4f75,
			FIELD_BG = 0x141618,
			BUTTON_IDLE = 0x33383d,
			BUTTON_ACTIVE = 0x1d5fb8,
			ROW_HOVER = 0x30353a,
			DROP_HINT = 0x3d7bd9,
			VIEWPORT_EDGE = 0x0e0f11
		};
	}

	struct Theme {
		Theme() = default;
		// One knob scales every metric: pass 2 * window uiScale for the doubled, DPI-aware UI.
		explicit Theme(float a_scale) {
			for (auto&& metric : { &Theme::titleBarHeight, &Theme::titleButtonWidth, &Theme::tabHeight, &Theme::tabWidth,
					&Theme::rowHeight, &Theme::fieldHeight, &Theme::splitterSize, &Theme::pad, &Theme::treeIndent, &Theme::labelWidth,
					&Theme::cornerRadius }) {
				this->*metric *= a_scale;
			}
		}

		// Derived multiplier for sizes not stored as metrics (tiles, panel-local widths).
		float scaleFactor() const { return rowHeight / 18.0f; }

		// Base design: original 2x metrics tuned down — heights/text x0.8, widths x0.65.
		float titleBarHeight = 27.0f;
		float titleButtonWidth = 30.0f;
		float tabHeight = 19.0f;
		float tabWidth = 70.0f;
		float rowHeight = 18.0f;
		float fieldHeight = 16.0f;
		float splitterSize = 5.0f;
		float pad = 4.0f;
		float treeIndent = 9.0f;
		float labelWidth = 62.0f;
		float cornerRadius = 3.0f;
		std::string labelFont = "wb-small";
		std::string buttonFont = "wb-small";
		std::string titleFont = "wb";

		MV::Color windowBg() const { return MV::Color(uint32_t(ThemeColor::WINDOW_BG)); }
		MV::Color panelBg() const { return MV::Color(uint32_t(ThemeColor::PANEL_BG)); }
		MV::Color panelAlt() const { return MV::Color(uint32_t(ThemeColor::PANEL_ALT)); }
		MV::Color header() const { return MV::Color(uint32_t(ThemeColor::HEADER)); }
		MV::Color tabActive() const { return MV::Color(uint32_t(ThemeColor::TAB_ACTIVE)); }
		MV::Color tabIdle() const { return MV::Color(uint32_t(ThemeColor::TAB_IDLE)); }
		MV::Color splitter() const { return MV::Color(uint32_t(ThemeColor::SPLITTER)); }
		MV::Color accent() const { return MV::Color(uint32_t(ThemeColor::ACCENT)); }
		MV::Color selection() const { return MV::Color(uint32_t(ThemeColor::SELECTION)); }
		MV::Color fieldBg() const { return MV::Color(uint32_t(ThemeColor::FIELD_BG)); }
		MV::Color buttonIdle() const { return MV::Color(uint32_t(ThemeColor::BUTTON_IDLE)); }
		MV::Color buttonActive() const { return MV::Color(uint32_t(ThemeColor::BUTTON_ACTIVE)); }
		MV::Color rowHover() const { return MV::Color(uint32_t(ThemeColor::ROW_HOVER)); }
		MV::Color dropHint() const {
			MV::Color c{ uint32_t(ThemeColor::DROP_HINT) };
			c.A = 0.35f;
			return c;
		}
		MV::Color tabStrip() const {
			MV::Color c{ uint32_t(ThemeColor::HEADER) };
			c.A = 0.3f;
			return c;
		}
	};

}

#endif
