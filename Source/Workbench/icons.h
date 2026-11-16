#ifndef _WORKBENCH_ICONS_H_
#define _WORKBENCH_ICONS_H_

#include <functional>
#include <memory>
#include <string>
#include "MV/Render/package.h"
#include "theme.h"

namespace Workbench {

	enum class Icon { Select, Move, Rotate, Scale, Snap, Grid, Frame };

	// A white icon drawn at the theme's scale; tint it through the Sprite's colour.
	std::shared_ptr<MV::TextureHandle> iconHandle(MV::Services& a_services, const Theme& a_theme, Icon a_icon);

	// A square toolbar button showing an icon. An active button fills with the accent: a tool that is on, a toggle that is set.
	class IconButton {
	public:
		IconButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, Icon a_icon, std::function<void()> a_onClick);

		static float size(const Theme& a_theme) { return a_theme.px(26.0f); }

		std::shared_ptr<MV::Scene::Node> node() const { return buttonNode; }
		bool active() const { return isActive; }
		void active(bool a_active);

	private:
		const Theme& theme;
		std::shared_ptr<MV::Scene::Node> buttonNode;
		std::shared_ptr<MV::Scene::Sprite> fill;
		std::shared_ptr<MV::Scene::Sprite> glyph;
		bool isActive = false;
	};

}

#endif
