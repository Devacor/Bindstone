#ifndef _WORKBENCH_WIDGETS_H_
#define _WORKBENCH_WIDGETS_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "MV/Render/package.h"
#include "theme.h"
#include "focus.h"

namespace Workbench {

	// Shared per-radius rounded-rect texture handle (9-slice; POT SDF surface, straight alpha).
	// Attach to any Sprite-derived component via ->texture(handle) to round its corners.
	std::shared_ptr<MV::TextureHandle> roundedRectHandle(MV::Services& a_services, const Theme& a_theme);

	// Sprite with themed rounded corners; resizing via ->bounds() re-lays the slice out.
	std::shared_ptr<MV::Scene::Sprite> attachRoundedRect(const std::shared_ptr<MV::Scene::Node>& a_owner, MV::Services& a_services, const Theme& a_theme, const MV::Size<>& a_size, const MV::Color& a_color);

	std::shared_ptr<MV::Scene::Text> makeLabel(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, const MV::Size<>& a_size, const std::string& a_text);

	std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, const MV::Size<>& a_size, const std::string& a_text, std::function<void()> a_onClick);

	std::shared_ptr<MV::Scene::Text> makeTextField(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, const std::string& a_id, const MV::Size<>& a_size, const std::string& a_initial, std::function<void(const std::string&)> a_onCommit);

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
