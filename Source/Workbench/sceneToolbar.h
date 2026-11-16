#ifndef _WORKBENCH_SCENETOOLBAR_H_
#define _WORKBENCH_SCENETOOLBAR_H_

#include <functional>
#include <memory>
#include <vector>
#include "MV/Render/package.h"
#include "icons.h"
#include "theme.h"

namespace Workbench {

	// The strip along the top of the Scene view: the transform tools, then snap, grid and frame-selected. It only
	// reports presses; the app owns the state and pushes it back through tool(), snap() and grid(), so hotkeys and
	// the buttons always agree.
	class SceneToolbar {
	public:
		struct Actions {
			std::function<void(int a_toolIndex)> onTool;   // 0 select, 1 move, 2 rotate, 3 scale
			std::function<void()> onSnapToggle;
			std::function<void()> onGridToggle;
			std::function<void()> onFrame;
		};

		SceneToolbar(const std::shared_ptr<MV::Scene::Node>& a_host, MV::Services& a_services, const Theme& a_theme, Actions a_actions);

		void tool(int a_toolIndex);
		void snap(bool a_on);
		void grid(bool a_on);

		void position(const MV::Point<>& a_topLeft);
		// In the host's parent space: presses inside belong to the toolbar, not to the viewport's pan and pick.
		bool contains(const MV::Point<>& a_point) const;
		// Centre of a button on screen: 0 to 3 the tools, then snap, grid, frame.
		MV::Point<int> buttonScreenPoint(int a_index) const;

	private:
		std::shared_ptr<MV::Scene::Node> addGroup(float& a_x, size_t a_buttonCount);

		std::shared_ptr<MV::Scene::Node> host;
		MV::Services& services;
		const Theme& theme;
		Actions actions;

		std::vector<std::unique_ptr<IconButton>> toolButtons;
		std::unique_ptr<IconButton> snapButton;
		std::unique_ptr<IconButton> gridButton;
		std::unique_ptr<IconButton> frameButton;
		MV::Size<> barSize;
	};

}

#endif
