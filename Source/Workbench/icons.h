#ifndef _WORKBENCH_ICONS_H_
#define _WORKBENCH_ICONS_H_

#include <functional>
#include <memory>
#include <string>
#include "MV/Render/package.h"
#include "theme.h"

namespace Workbench {

	enum class Icon { Select, Move, Rotate, Scale, Snap, Grid, Frame, History, StepBack, StepForward, Close, Minimize, Maximize, Pause, Play, Step, Handles, Document };

	// A white icon drawn at the theme's scale, or at a_pixels across for one shown large; tint it through the Sprite's colour.
	// The surfaces iconHandle makes are named with this, for reloading them when the window's scale changes.
	inline const std::string ICON_SURFACE_PREFIX = "workbenchIcon_";
	std::shared_ptr<MV::TextureHandle> iconHandle(MV::Services& a_services, const Theme& a_theme, Icon a_icon, int a_pixels = 0);

	// A folder in its own colours, drawn open with its front leaning forward: for a Sprite of any square size.
	std::shared_ptr<MV::TextureHandle> folderHandle(MV::Services& a_services);

	// A square toolbar button showing an icon. An active button fills with the accent: a tool that is on, a toggle
	// that is set. The pointer over one that is not active lights it.
	class IconButton {
	public:
		IconButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, Icon a_icon, std::function<void()> a_onClick, const std::string& a_tooltip = "");

		static float size(const Theme& a_theme) { return a_theme.px(26.0f); }

		std::shared_ptr<MV::Scene::Node> node() const { return buttonNode; }
		bool active() const;
		void active(bool a_active);
		bool hovered() const;

	private:
		struct Look;

		std::shared_ptr<MV::Scene::Node> buttonNode;
		std::shared_ptr<Look> look;
	};

}

#endif
