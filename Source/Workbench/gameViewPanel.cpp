#include "gameViewPanel.h"
#include <jaiscript/signals/signal.hpp>

namespace Workbench {

	namespace {
		// Draws the game when the panel's content draws, after the panel's background (attached before it), so the
		// dock's clip and the panels drawn over it stay in order.
		class GameFrame : public MV::Scene::Component {
		public:
			GameFrame(const std::weak_ptr<MV::Scene::Node>& a_owner, GameViewPanel* a_panel) :
				Component(a_owner),
				panel(a_panel) {
			}

			bool draw() override {
				panel->drawGame();
				return true;
			}

		private:
			GameViewPanel* panel;
		};
	}

	void GameViewPanel::build() {
		frame = contentNode->attach<MV::Scene::Sprite>().self();
		frame->color(theme->windowBg());
		contentNode->attach<GameFrame>(this);
		windowStandIn = MV::Scene::Node::make(contentNode->renderer(), "gameWindow");
		windowStandInView = windowStandIn->attach<MV::Scene::View>().self();
	}

	void GameViewPanel::resized(const MV::Size<>& a_size) {
		if (frame) {
			frame->bounds(MV::BoxAABB<>(MV::Point<>(), MV::toPoint(a_size)));
		}
	}

	void GameViewPanel::drawGame() {
		auto shown = scene ? scene() : nullptr;
		auto camera = editorCamera ? editorCamera() : nullptr;
		lastViewsDrawn = 0;
		if (!shown || !camera || !frame) {
			return;
		}
		auto& renderer = shown->renderer();
		const auto rectangle = frame->screenBounds();
		if (rectangle.width() <= 0 || rectangle.height() <= 0) {
			return;
		}
		lastTargetRectangle = rectangle;
		const auto panPosition = camera->position();
		const auto panScale = camera->scale();
		{
			auto quiet = camera->silence();
			quiet->position(MV::Point<>());
			quiet->scale(MV::Scale(1.0f, 1.0f, 1.0f));
		}
		renderer.pushTarget(rectangle);
		auto views = shown->componentsInChildren<MV::Scene::View>(false, true);
		for (auto&& view : views) {
			std::get<0>(view)->authoring(false);
		}
		if (views.empty()) {
			windowStandInView->show(shown);
			windowStandIn->draw();
		} else {
			shown->draw();
		}
		for (auto&& view : views) {
			std::get<0>(view)->authoring(true);
		}
		lastViewsDrawn = views.size();
		renderer.popTarget();
		{
			auto quiet = camera->silence();
			quiet->position(panPosition);
			quiet->scale(panScale);
		}
		if (renderer.device()) {
			const auto pixels = renderer.targetPixelSize();
			renderer.device()->setScissor(MV::Render::Rect{ 0, 0, pixels.width, pixels.height });
		}
	}

}
