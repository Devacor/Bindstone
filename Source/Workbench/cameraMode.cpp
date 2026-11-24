#include "cameraMode.h"
#include "widgets.h"
#include <algorithm>
#include <cmath>

namespace Workbench {

	namespace {
		const MV::Color frameColor(0xccf6e532);
		const MV::Color outsideFill(0x99000000);
		const MV::Color hardBoundsColor(0xddd0d0d0);
		const MV::Color softBoundsColor(0xeef6d532);

		void eachNode(const std::shared_ptr<MV::Scene::Node>& a_node, const std::function<void(const std::shared_ptr<MV::Scene::Node>&)>& a_visit) {
			a_visit(a_node);
			for (auto&& child : *a_node) {
				eachNode(child, a_visit);
			}
		}

		bool sameBox(const MV::BoxAABB<>& a_first, const MV::BoxAABB<>& a_second) {
			return a_first.minPoint == a_second.minPoint && a_first.maxPoint == a_second.maxPoint;
		}
	}

	CameraMode::CameraMode(const std::shared_ptr<MV::Scene::Node>& a_viewCamera, const std::shared_ptr<MV::Scene::Node>& a_overlay, const std::shared_ptr<MV::Scene::Node>& a_barHost, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, CommandHistory& a_history) :
		viewCamera(a_viewCamera),
		overlayHost(a_overlay),
		barHost(a_barHost),
		services(a_services),
		renderer(*a_services.get<MV::Draw2D>()),
		theme(a_theme),
		focus(a_focus),
		history(a_history) {
	}

	CameraMode::~CameraMode() {
		end();
	}

	std::vector<std::string> CameraMode::camerasIn(const std::shared_ptr<MV::Scene::Node>& a_scene) {
		std::vector<std::string> result;
		std::vector<std::string> constrained;
		if (!a_scene) {
			return result;
		}
		eachNode(a_scene, [&](const std::shared_ptr<MV::Scene::Node>& a_node) {
			if (a_node->component<MV::Scene::Camera>(true, false)) {
				result.push_back(a_node->id());
			}
			if (auto limits = a_node->component<MV::Scene::CameraConstraint>(true, false); limits && !limits->cameraNode().empty()) {
				constrained.push_back(limits->cameraNode());
			}
		});
		for (auto&& id : constrained) {
			if (std::find(result.begin(), result.end(), id) == result.end()) {
				result.push_back(id);
			}
		}
		return result;
	}

	MV::Size<> CameraMode::gameView() const {
		const auto monitor = renderer.monitorSize();
		const float aspect = monitor.width > 0 && monitor.height > 0 ? static_cast<float>(monitor.width) / monitor.height : 16.0f / 9.0f;
		return MV::Size<>(1080.0f * aspect, 1080.0f);
	}

	bool CameraMode::begin(const std::shared_ptr<MV::Scene::Node>& a_scene, const std::string& a_camera) {
		end();
		auto names = camerasIn(a_scene);
		if (names.empty()) {
			return false;
		}
		scene = a_scene;
		choices = names;
		subjectId = std::find(names.begin(), names.end(), a_camera) == names.end() ? names.front() : a_camera;

		editorPosition = viewCamera->position();
		editorScale = viewCamera->scale();
		viewCamera->position(MV::Point<>());
		viewCamera->scale(MV::Scale(1.0f, 1.0f, 1.0f));
		slot = renderer.reserveViewSlot();
		viewCamera->cameraId(slot);

		std::shared_ptr<MV::Scene::Camera> subject;
		std::shared_ptr<MV::Scene::CameraConstraint> found;
		eachNode(scene, [&](const std::shared_ptr<MV::Scene::Node>& a_node) {
			if (!subject && a_node->id() == subjectId) {
				if (auto camera = a_node->component<MV::Scene::Camera>(true, false)) {
					subject = camera.self();
				}
			}
			if (auto candidate = a_node->component<MV::Scene::CameraConstraint>(true, false); !found && candidate && candidate->cameraNode() == subjectId) {
				found = candidate.self();
			}
		});
		auto eyeNode = viewCamera->make("cameraModeEye");
		eye = eyeNode->attach<MV::Scene::Camera>().self();
		if (subject) {
			eye->projection(subject->projection())->units(subject->units())->origin(subject->origin())->width(subject->width())->height(subject->height())->match(subject->match())->userScaled(subject->userScaled())
				->zoom(subject->zoom())->fieldOfView(subject->fieldOfView())->planes(subject->nearPlane(), subject->farPlane())->sees(subject->sees());
			eyeNode->position(subject->owner()->worldTransform().position());
		}
		eye->viewSize(gameView(), renderer.window().contentScale(), renderer.window().userScale());
		limits = found;
		if (found) {
			found->authoringCamera(eye);
			if (!subject) {
				// a camera from another scene starts squared up on the soft bounds, zoomed out as far as they let it
				eye->zoom(found->minimumZoom(true));
				found->moveCamera(found->soft().centerPoint() - found->visible().centerPoint());
				found->clamp();
			}
		}
		layersLive(true);
		overlay = overlayHost->make("cameraModeOverlay");
		buildBar();
		update();
		return true;
	}

	void CameraMode::end() {
		if (!eye) {
			return;
		}
		layersLive(false);
		if (auto found = limits.lock()) {
			found->authoringCamera(nullptr);
		}
		limits.reset();
		auto eyeNode = eye->owner();
		eye.reset();
		eyeNode->removeFromParent();
		viewCamera->cameraId(viewCamera->parent() ? viewCamera->parent()->cameraId() : 0);
		renderer.releaseViewSlot(slot);
		slot = -1;
		viewCamera->position(editorPosition);
		viewCamera->scale(editorScale);
		barHost->clear();
		bar.reset();
		barButtons.clear();
		zoomField.reset();
		if (overlay) {
			overlay->removeFromParent();
			overlay.reset();
		}
		scene.reset();
		subjectId.clear();
		choices.clear();
		shownZoom = -1.0f;
		drawnFrame = drawnHard = drawnSoft = MV::BoxAABB<>();
	}

	std::vector<std::shared_ptr<MV::Scene::Parallax>> CameraMode::layers() const {
		std::vector<std::shared_ptr<MV::Scene::Parallax>> result;
		if (scene) {
			eachNode(scene, [&](const std::shared_ptr<MV::Scene::Node>& a_node) {
				if (auto layer = a_node->component<MV::Scene::Parallax>(true, false); layer && layer->enabled()) {
					result.push_back(layer.self());
				}
			});
		}
		return result;
	}

	void CameraMode::layersLive(bool a_live) {
		if (!eye) {
			return;
		}
		for (auto&& layer : layers()) {
			layer->authoringPreview(a_live ? eye : nullptr);
		}
	}

	MV::BoxAABB<> CameraMode::frame() const {
		const auto game = gameView();
		const float fit = std::min(view.size.width / game.width, view.size.height / game.height);
		const MV::Size<> size(game.width * fit, game.height * fit);
		const MV::Point<> topLeft(std::round(view.position.x + (view.size.width - size.width) * 0.5f), std::round(view.position.y + (view.size.height - size.height) * 0.5f));
		return MV::BoxAABB<>(topLeft, MV::point(topLeft.x + size.width, topLeft.y + size.height));
	}

	MV::Point<> CameraMode::viewFromScreen(const MV::Point<int>& a_screen) const {
		const auto box = frame();
		const auto world = renderer.worldFromScreen(a_screen);
		return MV::Point<>((world.x - box.minPoint.x) / std::max(box.width(), 1.0f), (world.y - box.minPoint.y) / std::max(box.height(), 1.0f));
	}

	MV::Point<> CameraMode::sceneOrigin() {
		const auto topLeft = eye->sceneFromView(MV::Point<>());
		const auto scale = sceneScale();
		return MV::Point<>(frame().minPoint.x - topLeft.x * scale, frame().minPoint.y - topLeft.y * scale);
	}

	float CameraMode::sceneScale() {
		return frame().width() / std::max(eye->seen().width, 0.0001f);
	}

	void CameraMode::hold(const MV::Point<>& a_around) {
		if (auto found = limits.lock()) {
			found->clamp(a_around);
		}
	}

	void CameraMode::pan(const MV::Point<int>& a_fromScreen, const MV::Point<int>& a_toScreen) {
		if (!eye) {
			return;
		}
		const auto to = viewFromScreen(a_toScreen);
		eye->sceneTranslate(eye->sceneFromView(viewFromScreen(a_fromScreen)) - eye->sceneFromView(to));
		hold(to);
	}

	void CameraMode::panBy(const MV::Point<>& a_worldDelta) {
		if (!eye) {
			return;
		}
		eye->sceneTranslate(a_worldDelta * (-1.0f / sceneScale()));
		hold(MV::Point<>(0.5f, 0.5f));
	}

	std::pair<float, float> CameraMode::zoomRange() {
		if (auto found = limits.lock()) {
			return { found->minimumZoom(false), std::max(found->maximumZoom(false), found->minimumZoom(false)) };
		}
		return { 0.01f, 100.0f };
	}

	void CameraMode::zoomBy(float a_factor, const MV::Point<int>& a_aroundScreen) {
		if (!eye) {
			return;
		}
		const auto [lowest, highest] = zoomRange();
		const auto around = viewFromScreen(a_aroundScreen);
		eye->zoomAround(std::clamp(eye->zoom() * a_factor, lowest, highest), around);
		hold(around);
	}

	void CameraMode::zoom(float a_zoom) {
		if (!eye) {
			return;
		}
		const auto [lowest, highest] = zoomRange();
		eye->zoomAround(std::clamp(a_zoom, lowest, highest), MV::Point<>(0.5f, 0.5f));
		hold(MV::Point<>(0.5f, 0.5f));
	}

	void CameraMode::toEdge(bool a_soft, Edge a_edge) {
		auto found = limits.lock();
		if (!eye || !found) {
			return;
		}
		const auto shown = found->visible();
		const auto bounds = found->bounds(a_soft);
		MV::Point<> move;
		switch (a_edge) {
			case Edge::LEFT:
				move.x = bounds.minPoint.x - shown.minPoint.x;
			break;
			case Edge::RIGHT:
				move.x = bounds.maxPoint.x - shown.maxPoint.x;
			break;
			case Edge::TOP:
				move.y = bounds.minPoint.y - shown.minPoint.y;
			break;
			case Edge::BOTTOM:
				move.y = bounds.maxPoint.y - shown.maxPoint.y;
			break;
		}
		found->moveCamera(move);
		hold(MV::Point<>(0.5f, 0.5f));
	}

	void CameraMode::toZoomLimit(bool a_soft, bool a_maximum) {
		auto found = limits.lock();
		if (!eye || !found) {
			return;
		}
		eye->zoomAround(a_maximum ? found->maximumZoom(a_soft) : found->minimumZoom(a_soft), MV::Point<>(0.5f, 0.5f));
		hold(MV::Point<>(0.5f, 0.5f));
	}

	void CameraMode::restParallaxHere() {
		if (!eye) {
			return;
		}
		struct Rest {
			std::weak_ptr<MV::Scene::Parallax> layer;
			MV::Point<> center;
			float zoom;
		};
		std::vector<Rest> before;
		std::vector<Rest> after;
		for (auto&& layer : layers()) {
			before.push_back({ layer, layer->restCenter(), layer->restZoom() });
			layer->restViewFrom(eye);
			after.push_back({ layer, layer->restCenter(), layer->restZoom() });
		}
		if (after.empty()) {
			return;
		}
		auto apply = [](const std::vector<Rest>& a_rests) {
			for (auto&& rest : a_rests) {
				if (auto layer = rest.layer.lock()) {
					layer->restView(rest.center, rest.zoom);
				}
			}
		};
		history.record("parallax rest view", [apply, after]() { apply(after); }, [apply, before]() { apply(before); });
	}

	void CameraMode::layout(const DockRect& a_view) {
		view = a_view;
		if (bar) {
			const auto size = bar->bounds(true).size();
			bar->position(MV::point(std::round(view.position.x + theme.pad), std::round(view.position.y + view.size.height - size.height - theme.pad)));
		}
	}

	bool CameraMode::barContains(const MV::Point<>& a_worldPoint) const {
		if (!bar) {
			return false;
		}
		auto box = bar->worldBounds(true);
		return a_worldPoint.x >= box.minPoint.x && a_worldPoint.x <= box.maxPoint.x && a_worldPoint.y >= box.minPoint.y && a_worldPoint.y <= box.maxPoint.y;
	}

	std::optional<MV::Point<int>> CameraMode::barButtonScreenPoint(const std::string& a_label) const {
		for (auto&& [label, weakButton] : barButtons) {
			if (label == a_label) {
				if (auto button = weakButton.lock()) {
					return button->screenBounds().centerPoint();
				}
			}
		}
		return std::nullopt;
	}

	void CameraMode::buildBar() {
		barHost->clear();
		barButtons.clear();
		bar = barHost->make("cameraBar");
		const float buttonWidth = theme.px(84.0f);
		const float labelWidth = theme.px(60.0f);
		const float height = theme.rowHeight;
		const float gap = theme.px(3.0f);
		float width = 0.0f;
		float y = gap;
		auto addButton = [&](float& a_x, const std::string& a_label, const std::string& a_text, std::function<void()> a_action) {
			auto button = makeButton(bar, services, theme, "camera_" + a_label, MV::size(buttonWidth, height), a_text, std::move(a_action));
			button->owner()->position(MV::point(a_x, y));
			barButtons.emplace_back(a_label, button->owner());
			a_x += buttonWidth + gap;
		};
		auto addLabel = [&](float& a_x, const std::string& a_text) {
			makeLabel(bar, services, theme, "label_" + a_text, MV::size(labelWidth, height), a_text)->owner()->position(MV::point(a_x, y));
			a_x += labelWidth;
		};

		float x = gap;
		addLabel(x, "Camera");
		const auto picked = static_cast<int>(std::find(choices.begin(), choices.end(), subjectId) - choices.begin());
		auto chooser = makeCycleButton(bar, services, theme, "camera_pick", MV::size(buttonWidth * 2.0f, height), choices, picked, [this](int a_index) {
			if (a_index >= 0 && a_index < static_cast<int>(choices.size()) && choices[static_cast<size_t>(a_index)] != subjectId) {
				auto lookAt = scene;
				begin(lookAt, choices[static_cast<size_t>(a_index)]);
			}
		});
		chooser->owner()->position(MV::point(x, y));
		barButtons.emplace_back("Pick", chooser->owner());
		x += buttonWidth * 2.0f + gap;
		addLabel(x, "Zoom");
		auto field = makeScrubField(bar, services, theme, focus, "camera_zoom", MV::size(buttonWidth, height), eye->zoom(), [this](float a_zoom) {
			zoom(a_zoom);
			shownZoom = eye->zoom();
		});
		field->owner()->position(MV::point(x, y));
		zoomField = field;
		shownZoom = eye->zoom();
		x += buttonWidth + gap * 4.0f;
		addButton(x, "Rest", "Rest here", [this]() { restParallaxHere(); });
		addButton(x, "Exit", "Done", [this]() {
			if (onExit) {
				onExit();
			} else {
				end();
			}
		});
		width = std::max(width, x);
		y += height + gap;

		if (limits.lock()) {
			for (bool soft : { false, true }) {
				x = gap;
				const std::string name = soft ? "Soft" : "Hard";
				addLabel(x, name);
				addButton(x, name + " Left", "Left", [this, soft]() { toEdge(soft, Edge::LEFT); });
				addButton(x, name + " Right", "Right", [this, soft]() { toEdge(soft, Edge::RIGHT); });
				addButton(x, name + " Top", "Top", [this, soft]() { toEdge(soft, Edge::TOP); });
				addButton(x, name + " Bottom", "Bottom", [this, soft]() { toEdge(soft, Edge::BOTTOM); });
				x += gap * 4.0f;
				addButton(x, name + " Min", "Zoom min", [this, soft]() { toZoomLimit(soft, false); });
				addButton(x, name + " Max", "Zoom max", [this, soft]() { toZoomLimit(soft, true); });
				width = std::max(width, x);
				y += height + gap;
			}
		}
		auto back = bar->make("back");
		back->depth(-1.0f);
		attachOutlinedRect(back, services, theme, MV::size(width + gap, y), theme.header(), theme.windowBg());
		bar->normalizeDepth();
		layout(view);
	}

	void CameraMode::drawOverlay() {
		auto found = limits.lock();
		const auto box = frame();
		const auto hard = found ? found->owner()->worldFromLocal(found->hard()) : MV::BoxAABB<>();
		const auto soft = found ? found->owner()->worldFromLocal(found->soft()) : MV::BoxAABB<>();
		if (sameBox(box, drawnFrame) && sameBox(hard, drawnHard) && sameBox(soft, drawnSoft)) {
			return;
		}
		drawnFrame = box;
		drawnHard = hard;
		drawnSoft = soft;
		overlay->clear();
		auto fill = [&](const MV::BoxAABB<>& a_box, const MV::Color& a_color) {
			if (a_box.width() > 0.0f && a_box.height() > 0.0f) {
				overlay->make(MV::guid("fill_"))->worldPosition(a_box.minPoint)->attach<MV::Scene::Sprite>()->bounds(a_box.size())->color(a_color);
			}
		};
		auto outline = [&](const MV::BoxAABB<>& a_box, const MV::Color& a_color) {
			const auto thickness = theme.px(1.5f);
			fill({ a_box.minPoint, MV::point(a_box.maxPoint.x, a_box.minPoint.y + thickness) }, a_color);
			fill({ MV::point(a_box.minPoint.x, a_box.maxPoint.y - thickness), a_box.maxPoint }, a_color);
			fill({ a_box.minPoint, MV::point(a_box.minPoint.x + thickness, a_box.maxPoint.y) }, a_color);
			fill({ MV::point(a_box.maxPoint.x - thickness, a_box.minPoint.y), a_box.maxPoint }, a_color);
		};
		const MV::Point<> viewMin = view.position;
		const MV::Point<> viewMax(view.position.x + view.size.width, view.position.y + view.size.height);
		fill({ viewMin, MV::point(viewMax.x, box.minPoint.y) }, outsideFill);
		fill({ MV::point(viewMin.x, box.maxPoint.y), viewMax }, outsideFill);
		fill({ MV::point(viewMin.x, box.minPoint.y), MV::point(box.minPoint.x, box.maxPoint.y) }, outsideFill);
		fill({ MV::point(box.maxPoint.x, box.minPoint.y), MV::point(viewMax.x, box.maxPoint.y) }, outsideFill);
		if (found) {
			outline(hard, hardBoundsColor);
			outline(soft, softBoundsColor);
		}
		outline(box, frameColor);
	}

	void CameraMode::update() {
		if (!eye || !scene) {
			return;
		}
		const auto box = frame();
		const auto world = renderer.world().size();
		const auto left = box.minPoint.x / world.width;
		const auto top = box.minPoint.y / world.height;
		const auto width = std::max(box.width() / world.width, 0.0001f);
		const auto height = std::max(box.height() / world.height, 0.0001f);
		// clip space squeezed into the frame's part of the window, as a View squeezes it into its viewport
		MV::TransformMatrix place;
		place.access(0, 0) = width;
		place.access(1, 1) = height;
		place.access(3, 0) = left * 2.0f + width - 1.0f;
		place.access(3, 1) = 1.0f - (top * 2.0f + height);
		renderer.viewProjection(slot, place * eye->projectionFor(gameView(), renderer.window().contentScale(), renderer.window().userScale()) * eye->eye());
		drawOverlay();
		auto field = zoomField.lock();
		if (field && focus.active() != field && !services.get<MV::TapDevice>()->leftDown() && std::abs(eye->zoom() - shownZoom) > 0.0001f) {
			buildBar();
		}
	}

}
