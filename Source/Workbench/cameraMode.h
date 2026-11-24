#ifndef _WORKBENCH_CAMERAMODE_H_
#define _WORKBENCH_CAMERAMODE_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "MV/Render/package.h"
#include "commands.h"
#include "dock.h"
#include "focus.h"
#include "theme.h"

namespace Workbench {

	// The Scene panel looking through one of the scene's cameras as the game would: that camera's view of the game's
	// window fitted into the panel, every Parallax layer placed for it, and its CameraConstraint's boxes drawn over it.
	// The mode looks through a camera of its own made to the scene camera's measure, so panning and zooming here move
	// nothing in the scene; a CameraConstraint naming a camera another scene holds (map.scene's WorldCamera lives in
	// game.scene) is looked through the same way. The editor's own pan and zoom come back when the mode ends.
	class CameraMode {
	public:
		CameraMode(const std::shared_ptr<MV::Scene::Node>& a_viewCamera, const std::shared_ptr<MV::Scene::Node>& a_overlay, const std::shared_ptr<MV::Scene::Node>& a_barHost, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, CommandHistory& a_history);
		~CameraMode();

		// What a scene can be looked through, by node id: its Cameras, then each camera a CameraConstraint in it names
		// that is not one of them.
		static std::vector<std::string> camerasIn(const std::shared_ptr<MV::Scene::Node>& a_scene);
		// Starts looking at a_scene through the camera of that id, the first there is when empty; false when it has none.
		bool begin(const std::shared_ptr<MV::Scene::Node>& a_scene, const std::string& a_camera = "");
		void end();
		bool active() const { return eye != nullptr; }
		const std::string& lookingThrough() const { return subjectId; }
		std::shared_ptr<MV::Scene::Camera> camera() const { return eye; }
		std::shared_ptr<MV::Scene::CameraConstraint> constraint() const { return limits.lock(); }
		// The window the game looks through: the shape of this monitor, 1080 high, as the game sizes its world.
		MV::Size<> gameView() const;

		// Panning and zooming the mode's camera, held inside the constraint's hard limits as a player's drag is.
		void pan(const MV::Point<int>& a_fromScreen, const MV::Point<int>& a_toScreen);
		void panBy(const MV::Point<>& a_worldDelta);
		void zoomBy(float a_factor, const MV::Point<int>& a_aroundScreen);
		void zoom(float a_zoom);
		std::pair<float, float> zoomRange();
		// The view put against one edge of the hard or soft bounds, or at one end of their zoom range.
		enum class Edge { LEFT, RIGHT, TOP, BOTTOM };
		void toEdge(bool a_soft, Edge a_edge);
		void toZoomLimit(bool a_soft, bool a_maximum);
		// Every Parallax layer in the scene takes this view as its rest view, as one undo step.
		void restParallaxHere();
		// Parallax placed for the mode's camera, or put at rest for a write.
		void layersLive(bool a_live);

		// What the scene's grid draws at while the mode is on: a screen point is origin + scale * scene point.
		MV::Point<> sceneOrigin();
		float sceneScale();

		void layout(const DockRect& a_view);
		// Each frame before the scene draws: the panel's projection through the camera, the overlay and the bar.
		void update();
		bool barContains(const MV::Point<>& a_worldPoint) const;
		std::optional<MV::Point<int>> barButtonScreenPoint(const std::string& a_label) const;

		// The bar's Done: the app ends the mode, as its own toggle does.
		std::function<void()> onExit;

	private:
		std::vector<std::shared_ptr<MV::Scene::Parallax>> layers() const;
		MV::BoxAABB<> frame() const;
		MV::Point<> viewFromScreen(const MV::Point<int>& a_screen) const;
		void hold(const MV::Point<>& a_around);
		void buildBar();
		void drawOverlay();

		std::shared_ptr<MV::Scene::Node> viewCamera;
		std::shared_ptr<MV::Scene::Node> overlayHost;
		std::shared_ptr<MV::Scene::Node> barHost;
		MV::Services& services;
		MV::Draw2D& renderer;
		const Theme& theme;
		FocusRouter& focus;
		CommandHistory& history;

		std::shared_ptr<MV::Scene::Node> scene;
		std::shared_ptr<MV::Scene::Camera> eye;
		std::weak_ptr<MV::Scene::CameraConstraint> limits;
		std::string subjectId;
		std::vector<std::string> choices;
		int32_t slot = -1;
		MV::Point<> editorPosition;
		MV::Scale editorScale;
		DockRect view;

		std::shared_ptr<MV::Scene::Node> overlay;
		std::shared_ptr<MV::Scene::Node> bar;
		std::vector<std::pair<std::string, std::weak_ptr<MV::Scene::Node>>> barButtons;
		std::weak_ptr<MV::Scene::Text> zoomField;
		float shownZoom = -1.0f;
		MV::BoxAABB<> drawnFrame;
		MV::BoxAABB<> drawnHard;
		MV::BoxAABB<> drawnSoft;
	};

}

#endif
