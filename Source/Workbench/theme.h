#ifndef _WORKBENCH_THEME_H_
#define _WORKBENCH_THEME_H_

#include <cmath>
#include <string>
#include "MV/Render/points.h"

namespace Workbench {

	// Dark editor palette; hex literals feed MV::Color(uint32_t).
	namespace ThemeColor {
		enum : uint32_t {
			WINDOW_BG = 0x151515,
			PANEL_BG = 0x242424,
			PANEL_ALT = 0x272727,
			HEADER = 0x2f2f2f,
			SPLITTER = 0x151515,
			ACCENT = 0x0070e0,
			SELECTION = 0x0f5fb5,
			FIELD_BG = 0x0f0f0f,
			FIELD_OUTLINE = 0x383838,
			BUTTON_OUTLINE = 0x161616,
			GLYPH = 0xb4b4b4,
			SCROLL_THUMB = 0x4c4c4c,
			BUTTON_IDLE = 0x3c3c3c,
			BUTTON_ACTIVE = 0x0070e0,
			ROW_HOVER = 0x333333,
			DROP_HINT = 0x0070e0
		};
	}

	struct Theme {
		Theme() = default;
		// The metrics below are logical pixels; the window's uiScale turns them into physical ones.
		explicit Theme(float a_scale) : scale(a_scale) {
			for (auto&& metric : { &Theme::titleBarHeight, &Theme::titleButtonWidth, &Theme::tabHeight, &Theme::tabWidth,
					&Theme::rowHeight, &Theme::fieldHeight, &Theme::splitterSize, &Theme::pad, &Theme::treeIndent, &Theme::labelWidth,
					&Theme::cornerRadius }) {
				this->*metric = px(this->*metric);
			}
		}

		// A size given in logical pixels, in physical ones.
		float px(float a_logical) const { return std::round(a_logical * scale); }
		float scaleFactor() const { return scale; }

		// Sized against Unreal's editor: 22 px controls, 26 px tabs and property rows, 4 px gaps and corners.
		float scale = 1.0f;
		float titleBarHeight = 32.0f;
		float titleButtonWidth = 46.0f;
		float tabHeight = 26.0f;
		float tabWidth = 104.0f;
		float rowHeight = 22.0f;
		float fieldHeight = 22.0f;
		float splitterSize = 4.0f;
		float pad = 4.0f;
		float treeIndent = 12.0f;
		float labelWidth = 110.0f;
		float cornerRadius = 4.0f;
		std::string labelFont = "wb-small";
		std::string buttonFont = "wb-small";
		std::string titleFont = "wb-small";

		MV::Color windowBg() const { return MV::Color(uint32_t(ThemeColor::WINDOW_BG)); }
		MV::Color panelBg() const { return MV::Color(uint32_t(ThemeColor::PANEL_BG)); }
		MV::Color panelAlt() const { return MV::Color(uint32_t(ThemeColor::PANEL_ALT)); }
		MV::Color header() const { return MV::Color(uint32_t(ThemeColor::HEADER)); }
		MV::Color splitter() const { return MV::Color(uint32_t(ThemeColor::SPLITTER)); }
		MV::Color accent() const { return MV::Color(uint32_t(ThemeColor::ACCENT)); }
		MV::Color selection() const { return MV::Color(uint32_t(ThemeColor::SELECTION)); }
		MV::Color fieldBg() const { return MV::Color(uint32_t(ThemeColor::FIELD_BG)); }
		MV::Color fieldOutline() const { return MV::Color(uint32_t(ThemeColor::FIELD_OUTLINE)); }
		MV::Color buttonOutline() const { return MV::Color(uint32_t(ThemeColor::BUTTON_OUTLINE)); }
		MV::Color glyph() const { return MV::Color(uint32_t(ThemeColor::GLYPH)); }
		MV::Color scrollThumb() const { return MV::Color(uint32_t(ThemeColor::SCROLL_THUMB)); }
		MV::Color buttonIdle() const { return MV::Color(uint32_t(ThemeColor::BUTTON_IDLE)); }
		MV::Color buttonActive() const { return MV::Color(uint32_t(ThemeColor::BUTTON_ACTIVE)); }
		MV::Color rowHover() const { return MV::Color(uint32_t(ThemeColor::ROW_HOVER)); }
		MV::Color dropHint() const {
			MV::Color c{ uint32_t(ThemeColor::DROP_HINT) };
			c.A = 0.35f;
			return c;
		}
	};

}

#endif
