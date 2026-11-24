#ifndef _WORKBENCH_GAMEVIEWPANEL_H_
#define _WORKBENCH_GAMEVIEWPANEL_H_

#include <functional>
#include <memory>
#include "dock.h"
#include "MV/Render/Scene/view.h"

namespace Workbench {

	// The scene as the game draws it: its Views take part, laid out in this panel as they would be in the window,
	// with the editor's pan and zoom left out. A scene with no View draws as the window shows one.
	class GameViewPanel : public Panel {
	public:
		GameViewPanel(std::function<std::shared_ptr<MV::Scene::Node>()> a_scene, std::function<std::shared_ptr<MV::Scene::Node>()> a_editorCamera) :
			Panel("Game"),
			scene(std::move(a_scene)),
			editorCamera(std::move(a_editorCamera)) {
		}

		void resized(const MV::Size<>& a_size) override;

		// Draws the game into the panel's rectangle; its frame calls it while the panel draws.
		void drawGame();

		// How many of the scene's Views the last picture drew through.
		size_t viewsDrawn() const { return lastViewsDrawn; }
		MV::BoxAABB<int> lastTarget() const { return lastTargetRectangle; }

	protected:
		void build() override;

	private:
		std::function<std::shared_ptr<MV::Scene::Node>()> scene;
		std::function<std::shared_ptr<MV::Scene::Node>()> editorCamera;
		std::shared_ptr<MV::Scene::Sprite> frame;
		// a scene with no View is drawn through this, standing in for the window
		std::shared_ptr<MV::Scene::Node> windowStandIn;
		std::shared_ptr<MV::Scene::View> windowStandInView;
		size_t lastViewsDrawn = 0;
		MV::BoxAABB<int> lastTargetRectangle;
	};

}

#endif
