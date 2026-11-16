#include "sceneToolbar.h"
#include "widgets.h"

namespace Workbench {

	namespace {
		const MV::Color groupFill(0.11f, 0.11f, 0.11f, 0.92f);
	}

	SceneToolbar::SceneToolbar(const std::shared_ptr<MV::Scene::Node>& a_host, MV::Services& a_services, const Theme& a_theme, Actions a_actions) :
		host(a_host),
		services(a_services),
		theme(a_theme),
		actions(std::move(a_actions)) {

		float x = 0.0f;
		const float buttonSize = IconButton::size(theme);
		const float inset = theme.px(2.0f);
		auto place = [&](const std::shared_ptr<MV::Scene::Node>& a_group, IconButton& a_button, size_t a_index) {
			(void)a_group;
			a_button.node()->position(MV::point(inset + static_cast<float>(a_index) * buttonSize, inset));
		};

		auto tools = addGroup(x, 4);
		const Icon toolIcons[] = { Icon::Select, Icon::Move, Icon::Rotate, Icon::Scale };
		for (int index = 0; index < 4; ++index) {
			toolButtons.push_back(std::make_unique<IconButton>(tools, services, theme, "tool" + std::to_string(index), toolIcons[index], [this, index]() {
				if (actions.onTool) {
					actions.onTool(index);
				}
			}));
			place(tools, *toolButtons.back(), static_cast<size_t>(index));
		}

		auto aids = addGroup(x, 2);
		snapButton = std::make_unique<IconButton>(aids, services, theme, "snap", Icon::Snap, [this]() {
			if (actions.onSnapToggle) {
				actions.onSnapToggle();
			}
		});
		place(aids, *snapButton, 0);
		gridButton = std::make_unique<IconButton>(aids, services, theme, "grid", Icon::Grid, [this]() {
			if (actions.onGridToggle) {
				actions.onGridToggle();
			}
		});
		place(aids, *gridButton, 1);

		auto view = addGroup(x, 1);
		frameButton = std::make_unique<IconButton>(view, services, theme, "frame", Icon::Frame, [this]() {
			if (actions.onFrame) {
				actions.onFrame();
			}
		});
		place(view, *frameButton, 0);

		barSize = MV::size(x - theme.px(6.0f), buttonSize + inset * 2.0f);
	}

	std::shared_ptr<MV::Scene::Node> SceneToolbar::addGroup(float& a_x, size_t a_buttonCount) {
		const float inset = theme.px(2.0f);
		MV::Size<> groupSize(static_cast<float>(a_buttonCount) * IconButton::size(theme) + inset * 2.0f, IconButton::size(theme) + inset * 2.0f);
		auto group = host->make(MV::guid("group_"));
		group->position(MV::point(a_x, 0.0f));
		attachOutlinedRect(group, services, theme, groupSize, groupFill, theme.fieldOutline());
		a_x += groupSize.width + theme.px(6.0f);
		return group;
	}

	void SceneToolbar::tool(int a_toolIndex) {
		for (int index = 0; index < static_cast<int>(toolButtons.size()); ++index) {
			toolButtons[static_cast<size_t>(index)]->active(index == a_toolIndex);
		}
	}

	void SceneToolbar::snap(bool a_on) {
		snapButton->active(a_on);
	}

	void SceneToolbar::grid(bool a_on) {
		gridButton->active(a_on);
	}

	void SceneToolbar::position(const MV::Point<>& a_topLeft) {
		host->position(a_topLeft);
	}

	MV::Point<int> SceneToolbar::buttonScreenPoint(int a_index) const {
		const IconButton* button = a_index < 4 ? toolButtons[static_cast<size_t>(a_index)].get() : a_index == 4 ? snapButton.get() : a_index == 5 ? gridButton.get() : frameButton.get();
		return button->node()->screenBounds().centerPoint();
	}

	bool SceneToolbar::contains(const MV::Point<>& a_point) const {
		auto origin = host->position();
		return a_point.x >= origin.x && a_point.y >= origin.y && a_point.x <= origin.x + barSize.width && a_point.y <= origin.y + barSize.height;
	}

}
