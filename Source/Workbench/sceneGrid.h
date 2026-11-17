#ifndef _WORKBENCH_SCENEGRID_H_
#define _WORKBENCH_SCENEGRID_H_

#include <memory>
#include <vector>
#include "MV/Render/package.h"
#include "theme.h"

namespace Workbench {

	// Grid lines behind the scene. The host is an unscaled node inside the viewport clip, so a line stays one pixel wide
	// at any zoom; the lines are laid out from the camera's position and zoom. The step widens by tens as the camera
	// zooms out, every tenth line is brighter, and the scene's two axes are tinted.
	class SceneGrid {
	public:
		SceneGrid(const std::shared_ptr<MV::Scene::Node>& a_host, const Theme& a_theme);

		bool visible() const { return isVisible; }
		void visible(bool a_visible);

		// World units between lines across and down, before the step widens for the zoom.
		MV::Size<> spacing() const { return worldSpacing; }
		void spacing(const MV::Size<>& a_worldUnits);

		void update(const MV::Point<>& a_cameraPosition, float a_zoom, const MV::BoxAABB<>& a_view);

	private:
		std::shared_ptr<MV::Scene::Sprite> line(size_t a_index);

		std::shared_ptr<MV::Scene::Node> host;
		const Theme& theme;
		std::vector<std::shared_ptr<MV::Scene::Sprite>> lines;
		bool isVisible = true;
		MV::Size<> worldSpacing { 10.0f, 10.0f };

		MV::Point<> laidOutPosition;
		float laidOutZoom = 0.0f;
		MV::BoxAABB<> laidOutView;
		MV::Size<> laidOutSpacing;
	};

}

#endif
