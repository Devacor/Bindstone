#ifndef _WORKBENCH_WIDGETS_H_
#define _WORKBENCH_WIDGETS_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "MV/Render/package.h"
#include <optional>
#include "theme.h"
#include "focus.h"
#include "icons.h"

namespace Workbench {

	// Fixed notation, trailing zeros dropped: 993.646, 340, 0 (never -0 or 1e-05).
	std::string formatNumberCompact(float a_value, int a_maximumDecimals = 3);

	// Shared per-radius rounded-rect texture handle (9-slice; POT SDF surface, straight alpha).
	// Attach to any Sprite-derived component via ->texture(handle) to round its corners.
	std::shared_ptr<MV::TextureHandle> roundedRectHandle(MV::Services& a_services, const Theme& a_theme);

	// Sprite with themed rounded corners; resizing via ->bounds() re-lays the slice out.
	std::shared_ptr<MV::Scene::Sprite> attachRoundedRect(const std::shared_ptr<MV::Scene::Node>& a_owner, MV::Services& a_services, const Theme& a_theme, const MV::Size<>& a_size, const MV::Color& a_color);

	// A control's body: an outline ring (the first Sprite on the node) and a fill inset by one logical pixel, id "fill".
	// A shaded fill darkens slightly towards its foot.
	std::shared_ptr<MV::Scene::Sprite> attachOutlinedRect(const std::shared_ptr<MV::Scene::Node>& a_owner, MV::Services& a_services, const Theme& a_theme, const MV::Size<>& a_size, const MV::Color& a_fill, const MV::Color& a_outline, bool a_shaded = false);
	MV::BoxAABB<> outlinedFillBounds(const Theme& a_theme, const MV::Size<>& a_size);

	// White-to-grey vertical ramp: texture it onto a flat Sprite to shade it towards its foot.
	std::shared_ptr<MV::TextureHandle> shadeHandle(MV::Services& a_services);

	// Solid expander arrow, centred in a_box: pointing right when collapsed, down when expanded.
	std::shared_ptr<MV::Scene::Sprite> makeExpanderArrow(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const MV::Size<>& a_box, bool a_expanded);

	// The width a_text takes in the label font.
	float labelTextWidth(MV::Services& a_services, const Theme& a_theme, const std::string& a_text);
	// a_text, or its start and an ellipsis when it is wider than a_width. Text that carries markup is left as it is.
	std::string fitLabelText(MV::Services& a_services, const Theme& a_theme, const std::string& a_text, float a_width);

	// A label never draws past the width it is made with: text too long for it ends in an ellipsis.
	std::shared_ptr<MV::Scene::Text> makeLabel(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, const MV::Size<>& a_size, const std::string& a_text);
	// New text for a label, fitted to the width it was made with.
	void setLabelText(const std::shared_ptr<MV::Scene::Text>& a_label, MV::Services& a_services, const Theme& a_theme, const std::string& a_text);

	std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, const MV::Size<>& a_size, const std::string& a_text, std::function<void()> a_onClick);
	// The same button showing an icon in place of text. a_idle and a_hover replace the theme's fills: an accent idle
	// for a button that is switched on, a red hover for one that closes.
	std::shared_ptr<MV::Scene::Button> makeIconButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, const MV::Size<>& a_size, Icon a_icon, std::function<void()> a_onClick, const std::string& a_tooltip = "", std::optional<MV::Color> a_idle = {}, std::optional<MV::Color> a_hover = {});
	void setButtonIcon(const std::shared_ptr<MV::Scene::Button>& a_button, MV::Services& a_services, const Theme& a_theme, Icon a_icon);

	// The sprite takes a_hover while the Clickable is highlighted (under the pointer or holding the focus) and a_idle
	// again after.
	void hoverTint(const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const std::shared_ptr<MV::Scene::Sprite>& a_sprite, const MV::Color& a_idle, const MV::Color& a_hover);
	// The sprite shows only while the Clickable is highlighted.
	void hoverShow(const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const std::shared_ptr<MV::Scene::Sprite>& a_sprite);
	// Shows a_text beside the pointer once it has rested on the Clickable.
	void tooltip(MV::Services& a_services, const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const std::string& a_text);

	// a_enterOnly: the field commits on Enter alone; left any other way it shows again what it showed when focused.
	std::shared_ptr<MV::Scene::Text> makeTextField(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, const std::string& a_id, const MV::Size<>& a_size, const std::string& a_initial, std::function<void(const std::string&)> a_onCommit, bool a_enterOnly = false);

	std::shared_ptr<MV::Scene::Node> makeToggle(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, bool a_initial, std::function<void(bool)> a_onChange);

	std::shared_ptr<MV::Scene::Button> makeCycleButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, const MV::Size<>& a_size, std::vector<std::string> a_labels, int a_initialIndex, std::function<void(int)> a_onChange);

	// Numeric field with Unity-style drag-to-scrub: horizontal drag changes the value live
	// (magnitude-scaled step); a plain click focuses it for typing. a_onChange fires per live
	// tick; a_onHistory fires ONCE per gesture (drag end / typed commit) with old and new.
	std::shared_ptr<MV::Scene::Text> makeScrubField(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, const std::string& a_id, const MV::Size<>& a_size, float a_value, std::function<void(float)> a_onChange, std::function<void(float, float)> a_onHistory = nullptr);

	// A row of labeled scrub fields (X/Y/Z, R/G/B/A...) splitting a_totalWidth; per-axis commits.
	void makeVectorRow(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, float a_totalWidth, const std::vector<std::pair<std::string, float>>& a_axes, std::function<void(size_t, float)> a_onChange, std::function<void(size_t, float, float)> a_onHistory = nullptr);

}

#endif
