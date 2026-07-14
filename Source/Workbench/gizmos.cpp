#include "gizmos.h"
#include <algorithm>
#include <vector>
#include "MV/Utility/generalUtility.h"

namespace Workbench {

	namespace {
		// Old-editor handle palette (editorDefines.h): translucent yellow move handle, amber corners.
		const MV::Color moveHandleFill(0x44ffff00);
		const MV::Color sizeHandleFill(0x44ffb400);
		const MV::Color pointHandleFill(1.0f, 0.0f, 1.0f, 0.25f);
		const MV::Color highlightFill(0x11ffffff);
		// Dock chrome Clickables sit at the default 100; +100 wins inside the viewport. The move
		// handle sits above a bounds body, point handles between, so overlaps resolve smallest-first.
		constexpr int64_t bodyPriorityBoost = 100;
		constexpr int64_t pointPriorityBoost = 105;
		constexpr int64_t movePriorityBoost = 110;
	}

	struct GizmoLayer::Gizmo {
		explicit Gizmo(GizmoLayer& a_layer) : layer(a_layer) {}
		virtual ~Gizmo() {
			if (container) {
				container->removeFromParent();
			}
		}

		// Rebuild handle geometry from the target's current bounds. Destroying a Clickable from
		// its own release signal is safe (dispatch holds the shared_ptr) — the old editor's
		// resetHandles relied on the same contract.
		virtual void sync() = 0;
		virtual bool dirty() const = 0;

		bool dragging = false;
		GizmoLayer& layer;
		std::shared_ptr<MV::Scene::Node> container;
	};

	struct GizmoLayer::NodeGizmo : GizmoLayer::Gizmo {
		NodeGizmo(GizmoLayer& a_layer, const std::shared_ptr<MV::Scene::Node>& a_target) :
			Gizmo(a_layer),
			target(a_target) {
			container = a_layer.overlay->make("nodeGizmo");
			sync();
		}

		MV::BoxAABB<> targetFrame() const {
			// World space, not screen: the overlay is identity in visor world, and screen
			// coordinates diverge from world under DPI scaling.
			auto box = target->worldBounds(false);
			if (box.size().width < 1.0f && box.size().height < 1.0f) {
				box = target->worldBounds(true);
			}
			return MV::round<MV::PointPrecision>(box);
		}

		bool dirty() const override {
			auto frame = targetFrame();
			return !(frame.minPoint == lastFrame.minPoint) || !(frame.maxPoint == lastFrame.maxPoint) ||
				!(target->worldPosition() == lastAnchor);
		}

		void sync() override {
			container->clear();
			lastFrame = targetFrame();
			lastAnchor = target->worldPosition();

			auto box = lastFrame;
			if (box.size().width >= 1.0f || box.size().height >= 1.0f) {
				MV::Color edge = layer.theme.accent();
				edge.A = 0.85f;
				float thickness = 1.5f;
				auto edgeAt = [&](const MV::Point<>& a_at, const MV::Size<>& a_size) {
					auto sprite = container->make(MV::guid("edge_"))->attach<MV::Scene::Sprite>();
					sprite->bounds(a_size)->color(edge);
					sprite->owner()->worldPosition(a_at);
				};
				edgeAt(box.topLeftPoint(), MV::size(box.size().width, thickness));
				edgeAt(MV::point(box.minPoint.x, box.maxPoint.y - thickness), MV::size(box.size().width, thickness));
				edgeAt(box.topLeftPoint(), MV::size(thickness, box.size().height));
				edgeAt(MV::point(box.maxPoint.x - thickness, box.minPoint.y), MV::size(thickness, box.size().height));
			}

			auto handle = container->make("move")->attach<MV::Scene::Clickable>(layer.mouse);
			handle->bounds({ MV::size(11.0f, 11.0f), true })->show()->color(moveHandleFill);
			handle->globalPriority(handle->globalPriority() + movePriorityBoost);
			handle->owner()->worldPosition(target->worldPosition());
			handle->owner()->attach<MV::Scene::Sprite>()->bounds({ MV::size(1.5f, 1.5f), true })->color(MV::Color(0xffffffff));

			std::weak_ptr<MV::Scene::Node> weakTarget = target;
			handle->onPress.connect("press", [this](std::shared_ptr<MV::Scene::Clickable>) {
				dragging = true;
				pressPosition = target->position();
				layer.pressed();
			});
			handle->onDrag.connect("drag", [this](std::shared_ptr<MV::Scene::Clickable> a_handle, const MV::Point<int>&, const MV::Point<int>& a_delta) {
				a_handle->owner()->translate(a_handle->owner()->renderer().worldFromScreen(a_delta));
				target->worldPosition(a_handle->owner()->worldPosition());
			});
			handle->onRelease.connect("release", [this, weakTarget](std::shared_ptr<MV::Scene::Clickable>, const MV::Point<MV::PointPrecision>&) {
				dragging = false;
				auto newPosition = target->position();
				if (!(newPosition == pressPosition)) {
					auto oldPosition = pressPosition;
					layer.history.record("move " + target->id(),
						[weakTarget, newPosition]() { if (auto locked = weakTarget.lock()) { locked->position(newPosition); } },
						[weakTarget, oldPosition]() { if (auto locked = weakTarget.lock()) { locked->position(oldPosition); } });
				}
				sync();
				layer.edited();
			});
		}

		std::shared_ptr<MV::Scene::Node> target;
		MV::Point<> pressPosition;
		MV::BoxAABB<> lastFrame;
		MV::Point<> lastAnchor;
	};

	struct GizmoLayer::BoundsGizmo : GizmoLayer::Gizmo {
		BoundsGizmo(GizmoLayer& a_layer, const std::shared_ptr<MV::Scene::Component>& a_component) :
			Gizmo(a_layer),
			component(a_component) {
			container = a_layer.overlay->make("boundsGizmo");

			std::weak_ptr<MV::Scene::Component> weak = a_component;
			readLocal = [weak]() {
				auto locked = weak.lock();
				return locked ? locked->bounds() : MV::BoxAABB<>();
			};
			writeLocal = [weak](const MV::BoxAABB<>& a_bounds) {
				if (auto locked = weak.lock()) {
					locked->bounds(a_bounds);
				}
			};

			if (auto emitter = std::dynamic_pointer_cast<MV::Scene::Emitter>(a_component)) {
				std::weak_ptr<MV::Scene::Emitter> weakEmitter = emitter;
				readLocal = [weakEmitter]() {
					auto locked = weakEmitter.lock();
					return locked ? MV::BoxAABB<>(locked->properties().minimumPosition, locked->properties().maximumPosition) : MV::BoxAABB<>();
				};
				writeLocal = [weakEmitter](const MV::BoxAABB<>& a_bounds) {
					if (auto locked = weakEmitter.lock()) {
						locked->properties().minimumPosition = a_bounds.minPoint;
						locked->properties().maximumPosition = a_bounds.maxPoint;
					}
				};
			} else if (auto pathMap = std::dynamic_pointer_cast<MV::Scene::PathMap>(a_component)) {
				pathMap->show();
				std::weak_ptr<MV::Scene::PathMap> weakMap = pathMap;
				restore = [weakMap]() { if (auto locked = weakMap.lock()) { locked->hide(); } };
				painter = true;
			} else if (auto clickable = std::dynamic_pointer_cast<MV::Scene::Clickable>(a_component)) {
				clickable->color(MV::Color(1.0f, 1.0f, 1.0f, 0.1f))->show();
				std::weak_ptr<MV::Scene::Clickable> weakClickable = clickable;
				restore = [weakClickable]() {
					if (auto locked = weakClickable.lock()) {
						locked->color(MV::Color(1.0f, 1.0f, 1.0f, 1.0f))->hide();
					}
				};
			}
			sync();
		}

		~BoundsGizmo() override {
			if (restore) {
				restore();
			}
		}

		bool dirty() const override {
			auto frame = MV::round<MV::PointPrecision>(component->worldBounds());
			return !(frame.minPoint == lastFrame.minPoint) || !(frame.maxPoint == lastFrame.maxPoint);
		}

		void sync() override {
			container->clear();
			auto rectBox = MV::round<MV::PointPrecision>(component->worldBounds());
			lastFrame = rectBox;
			auto handleSize = MV::size(8.0f, 8.0f);

			body = container->make(MV::guid("body_"))->worldPosition(rectBox.topLeftPoint())->attach<MV::Scene::Clickable>(layer.mouse);
			body->bounds({ MV::Point<>(), rectBox.size() });
			body->globalPriority(body->globalPriority() + bodyPriorityBoost);
			body->onPress.connect("press", [this](std::shared_ptr<MV::Scene::Clickable>) { beginGesture(); });
			if (painter) {
				body->onDrag.connect("paint", [this](std::shared_ptr<MV::Scene::Clickable> a_handle, const MV::Point<int>&, const MV::Point<int>&) {
					paintCell(a_handle->mouse().position());
				});
				body->onRelease.connect("release", [this](std::shared_ptr<MV::Scene::Clickable>, const MV::Point<MV::PointPrecision>&) { endPaintGesture(); });
			} else {
				body->onDrag.connect("position", [this](std::shared_ptr<MV::Scene::Clickable> a_handle, const MV::Point<int>&, const MV::Point<int>& a_delta) {
					auto worldDelta = a_handle->owner()->renderer().worldFromScreen(a_delta);
					a_handle->owner()->translate(worldDelta);
					for (auto&& corner : { &topLeft, &topRight, &bottomLeft, &bottomRight }) {
						(*corner)->owner()->translate(worldDelta);
					}
					auto current = readLocal();
					writeLocal(MV::BoxAABB<>(component->owner()->localFromWorld(a_handle->owner()->worldPosition()), current.size()));
				});
				body->onRelease.connect("release", [this](std::shared_ptr<MV::Scene::Clickable>, const MV::Point<MV::PointPrecision>&) { endBoundsGesture(); });
			}

			topLeft = makeCorner(rectBox.topLeftPoint() - MV::toPoint(handleSize), "topLeft_");
			topRight = makeCorner(rectBox.topRightPoint() - MV::point(0.0f, handleSize.height), "topRight_");
			bottomLeft = makeCorner(rectBox.bottomLeftPoint() - MV::point(handleSize.width, 0.0f), "bottomLeft_");
			bottomRight = makeCorner(rectBox.bottomRightPoint(), "bottomRight_");

			wireCorner(topLeft, &topRight, &bottomLeft);
			wireCorner(topRight, &topLeft, &bottomRight);
			wireCorner(bottomLeft, &bottomRight, &topLeft);
			wireCorner(bottomRight, &bottomLeft, &topRight);
		}

		MV::Scene::SafeComponent<MV::Scene::Clickable> makeCorner(const MV::Point<>& a_at, const std::string& a_name) {
			auto corner = container->make(MV::guid(a_name))->worldPosition(a_at)->attach<MV::Scene::Clickable>(layer.mouse);
			corner->bounds(MV::size(8.0f, 8.0f))->color(sizeHandleFill)->show();
			corner->globalPriority(corner->globalPriority() + bodyPriorityBoost);
			return corner;
		}

		// A corner drag moves its row neighbor vertically and its column neighbor horizontally,
		// then bounds recompute from the diagonal pair (the old ResizeHandles contract).
		void wireCorner(MV::Scene::SafeComponent<MV::Scene::Clickable>& a_corner,
				MV::Scene::SafeComponent<MV::Scene::Clickable>* a_rowNeighbor,
				MV::Scene::SafeComponent<MV::Scene::Clickable>* a_columnNeighbor) {
			a_corner->onPress.connect("press", [this](std::shared_ptr<MV::Scene::Clickable>) { beginGesture(); });
			a_corner->onDrag.connect("size", [this, a_rowNeighbor, a_columnNeighbor](std::shared_ptr<MV::Scene::Clickable> a_handle, const MV::Point<int>&, const MV::Point<int>& a_delta) {
				auto worldDelta = a_handle->owner()->renderer().worldFromScreen(a_delta);
				a_handle->owner()->translate(worldDelta);
				(*a_rowNeighbor)->owner()->translate(MV::point(0.0f, worldDelta.y));
				(*a_columnNeighbor)->owner()->translate(MV::point(worldDelta.x, 0.0f));
				applyHandleBounds();
			});
			a_corner->onRelease.connect("release", [this](std::shared_ptr<MV::Scene::Clickable>, const MV::Point<MV::PointPrecision>&) { endBoundsGesture(); });
		}

		void applyHandleBounds() {
			auto box = MV::BoxAABB<>(topLeft->worldBounds().bottomRightPoint(), bottomRight->worldBounds().topLeftPoint());
			writeLocal(component->owner()->localFromWorld(box));
			auto rectBox = MV::round<MV::PointPrecision>(component->worldBounds());
			body->bounds(rectBox.size());
			body->owner()->worldPosition(rectBox.minPoint);
		}

		void beginGesture() {
			dragging = true;
			pressBounds = readLocal();
			paintedCells.clear();
			lastPaintCell = MV::Point<int>(-1, -1);
			layer.pressed();
		}

		void endBoundsGesture() {
			dragging = false;
			auto newBounds = readLocal();
			if (!(newBounds.minPoint == pressBounds.minPoint) || !(newBounds.maxPoint == pressBounds.maxPoint)) {
				auto apply = writeLocal;
				auto oldBounds = pressBounds;
				layer.history.record("resize",
					[apply, newBounds]() { apply(newBounds); },
					[apply, oldBounds]() { apply(oldBounds); });
			}
			sync();
			layer.edited();
		}

		void paintCell(const MV::Point<int>& a_screenPoint) {
			auto pathMap = std::dynamic_pointer_cast<MV::Scene::PathMap>(component);
			if (!pathMap) {
				return;
			}
			auto gridPosition = MV::cast<int>(pathMap->gridFromLocal(pathMap->owner()->localFromScreen(a_screenPoint)));
			if (lastPaintCell == gridPosition) {
				return;
			}
			lastPaintCell = gridPosition;
			if (!pathMap->inBounds(gridPosition)) {
				return;
			}
			auto& gridNode = pathMap->nodeFromGrid(gridPosition);
			bool nowBlocked = !gridNode.staticallyBlocked();
			if (nowBlocked) {
				gridNode.staticBlock();
			} else {
				gridNode.staticUnblock();
			}
			paintedCells.emplace_back(gridPosition, nowBlocked);
		}

		void endPaintGesture() {
			dragging = false;
			if (!paintedCells.empty()) {
				std::weak_ptr<MV::Scene::Component> weak = component;
				auto applyCells = [weak](const std::vector<std::pair<MV::Point<int>, bool>>& a_cells, bool a_invert) {
					auto locked = std::dynamic_pointer_cast<MV::Scene::PathMap>(weak.lock());
					if (!locked) {
						return;
					}
					for (auto&& [cell, blocked] : a_cells) {
						if (!locked->inBounds(cell)) {
							continue;
						}
						auto& gridNode = locked->nodeFromGrid(cell);
						if (blocked != a_invert) {
							gridNode.staticBlock();
						} else {
							gridNode.staticUnblock();
						}
					}
				};
				auto cells = paintedCells;
				layer.history.record("paint path",
					[applyCells, cells]() { applyCells(cells, false); },
					[applyCells, cells]() { applyCells(cells, true); });
			}
			sync();
			layer.edited();
		}

		std::shared_ptr<MV::Scene::Component> component;
		std::function<MV::BoxAABB<>()> readLocal;
		std::function<void(const MV::BoxAABB<>&)> writeLocal;
		std::function<void()> restore;
		bool painter = false;

		MV::Scene::SafeComponent<MV::Scene::Clickable> body;
		MV::Scene::SafeComponent<MV::Scene::Clickable> topLeft;
		MV::Scene::SafeComponent<MV::Scene::Clickable> topRight;
		MV::Scene::SafeComponent<MV::Scene::Clickable> bottomLeft;
		MV::Scene::SafeComponent<MV::Scene::Clickable> bottomRight;

		MV::BoxAABB<> lastFrame;
		MV::BoxAABB<> pressBounds;
		std::vector<std::pair<MV::Point<int>, bool>> paintedCells;
		MV::Point<int> lastPaintCell{ -1, -1 };
	};

	struct GizmoLayer::PointsGizmo : GizmoLayer::Gizmo {
		PointsGizmo(GizmoLayer& a_layer, const std::shared_ptr<MV::Scene::Drawable>& a_target) :
			Gizmo(a_layer),
			target(a_target) {
			container = a_layer.overlay->make("pointsGizmo");
			sync();
		}

		std::vector<MV::Point<>> handlePositions() const {
			std::vector<MV::Point<>> result;
			for (size_t i = 0; i < target->pointSize(); ++i) {
				result.push_back(MV::round<MV::PointPrecision>(target->owner()->worldFromLocal(target->point(i).point())));
			}
			return result;
		}

		bool dirty() const override {
			auto current = handlePositions();
			if (current.size() != lastPositions.size()) {
				return true;
			}
			for (size_t i = 0; i < current.size(); ++i) {
				if (!(current[i] == lastPositions[i])) {
					return true;
				}
			}
			return false;
		}

		void sync() override {
			container->clear();
			lastPositions = handlePositions();
			for (size_t i = 0; i < lastPositions.size(); ++i) {
				auto position = lastPositions[i];
				bool duplicate = false;
				for (size_t j = 0; j < i && !duplicate; ++j) {
					duplicate = lastPositions[j] == position;
				}
				if (duplicate) {
					continue;   // welded clusters share one handle
				}
				auto handle = container->make(std::to_string(i))->worldPosition(position)->attach<MV::Scene::Clickable>(layer.mouse);
				handle->bounds({ MV::size(7.0f, 7.0f), true })->color(pointHandleFill)->show();
				handle->globalPriority(handle->globalPriority() + pointPriorityBoost);
				size_t pointIndex = i;
				handle->onPress.connect("press", [this](std::shared_ptr<MV::Scene::Clickable>) {
					dragging = true;
					pressPoints = target->getPoints();
					layer.pressed();
				});
				handle->onDrag.connect("drag", [this, pointIndex](std::shared_ptr<MV::Scene::Clickable> a_handle, const MV::Point<int>&, const MV::Point<int>& a_delta) {
					a_handle->owner()->translate(a_handle->owner()->renderer().worldFromScreen(a_delta));
					movePoint(pointIndex, target->owner()->localFromWorld(a_handle->owner()->worldPosition()));
				});
				handle->onRelease.connect("release", [this](std::shared_ptr<MV::Scene::Clickable>, const MV::Point<MV::PointPrecision>&) {
					endGesture();
				});
			}
		}

		// Points within 0.75 local units of the dragged point's pre-tick position weld along
		// with it; LSHIFT suppresses the weld (the old Drawable panel's contract).
		void movePoint(size_t a_index, const MV::Point<>& a_final) {
			auto pointList = target->getPoints();
			if (a_index >= pointList.size()) {
				return;
			}
			auto pointSelected = pointList[a_index];
			pointList[a_index] = a_final;
			target->setPoint(a_index, pointList[a_index]);
			const Uint8* keys = SDL_GetKeyboardState(nullptr);
			if (!keys[SDL_SCANCODE_LSHIFT]) {
				for (size_t i = 0; i < pointList.size(); ++i) {
					if (i != a_index && MV::distance(pointSelected.point(), pointList[i].point()) < 0.75f) {
						pointList[i] = a_final;
						target->setPoint(i, pointList[i]);
					}
				}
			}
		}

		void endGesture() {
			dragging = false;
			auto newPoints = target->getPoints();
			bool changed = newPoints.size() != pressPoints.size();
			for (size_t i = 0; !changed && i < newPoints.size(); ++i) {
				changed = !(newPoints[i].point() == pressPoints[i].point());
			}
			if (changed) {
				std::weak_ptr<MV::Scene::Drawable> weak = target;
				auto applyPoints = [weak](const std::vector<MV::DrawPoint>& a_points) {
					if (auto locked = weak.lock()) {
						for (size_t i = 0; i < a_points.size() && i < locked->pointSize(); ++i) {
							locked->setPoint(i, a_points[i]);
						}
					}
				};
				auto oldPoints = pressPoints;
				layer.history.record("points",
					[applyPoints, newPoints]() { applyPoints(newPoints); },
					[applyPoints, oldPoints]() { applyPoints(oldPoints); });
			}
			sync();
			layer.edited();
		}

		std::shared_ptr<MV::Scene::Drawable> target;
		std::vector<MV::DrawPoint> pressPoints;
		std::vector<MV::Point<>> lastPositions;
	};

	struct GizmoLayer::HighlightGizmo : GizmoLayer::Gizmo {
		HighlightGizmo(GizmoLayer& a_layer, const std::shared_ptr<MV::Scene::Component>& a_component) :
			Gizmo(a_layer),
			component(a_component) {
			container = a_layer.overlay->make("highlightGizmo");
			sync();
		}

		bool dirty() const override {
			auto frame = MV::round<MV::PointPrecision>(component->worldBounds());
			return !(frame.minPoint == lastFrame.minPoint) || !(frame.maxPoint == lastFrame.maxPoint);
		}

		void sync() override {
			container->clear();
			auto rectBox = MV::round<MV::PointPrecision>(component->worldBounds());
			lastFrame = rectBox;
			auto dimensions = MV::Size<>(std::max(rectBox.size().width, 5.0f), std::max(rectBox.size().height, 5.0f));
			auto sprite = container->make(MV::guid("fill_"))->worldPosition(rectBox.minPoint)->attach<MV::Scene::Sprite>();
			sprite->bounds(dimensions)->color(highlightFill);
		}

		std::shared_ptr<MV::Scene::Component> component;
		MV::BoxAABB<> lastFrame;
	};

	GizmoLayer::GizmoLayer(const std::shared_ptr<MV::Scene::Node>& a_overlay, MV::TapDevice& a_mouse, CommandHistory& a_history, const Theme& a_theme) :
		overlay(a_overlay),
		mouse(a_mouse),
		history(a_history),
		theme(a_theme) {
	}

	GizmoLayer::~GizmoLayer() = default;

	void GizmoLayer::pressed() {
		if (onHandlePressed) {
			onHandlePressed();
		}
	}

	void GizmoLayer::edited() {
		if (onEdited) {
			onEdited();
		}
	}

	void GizmoLayer::select(const std::shared_ptr<MV::Scene::Node>& a_node) {
		if (nodeTarget == a_node) {
			return;   // re-selecting keeps the active component gizmo
		}
		componentGizmo.reset();
		componentTarget.reset();
		componentOwner.reset();
		nodeGizmo.reset();
		nodeTarget = a_node;
		if (nodeTarget) {
			nodeGizmo = std::make_unique<NodeGizmo>(*this, nodeTarget);
		}
	}

	void GizmoLayer::editComponent(const std::shared_ptr<MV::Scene::Component>& a_component) {
		componentGizmo.reset();
		componentTarget.reset();
		componentOwner.reset();
		if (!a_component) {
			return;
		}
		componentGizmo = makeComponentGizmo(a_component);
		if (componentGizmo) {
			componentTarget = a_component;
			componentOwner = a_component->owner();
		}
	}

	bool GizmoLayer::supports(const std::shared_ptr<MV::Scene::Component>& a_component) {
		return a_component && (
			std::dynamic_pointer_cast<MV::Scene::Drawable>(a_component) ||
			std::dynamic_pointer_cast<MV::Scene::Clickable>(a_component) ||
			std::dynamic_pointer_cast<MV::Scene::Text>(a_component) ||
			std::dynamic_pointer_cast<MV::Scene::Spine>(a_component) ||
			std::dynamic_pointer_cast<MV::Scene::Parallax>(a_component) ||
			std::dynamic_pointer_cast<MV::Scene::PathMap>(a_component));
	}

	std::unique_ptr<GizmoLayer::Gizmo> GizmoLayer::makeComponentGizmo(const std::shared_ptr<MV::Scene::Component>& a_component) {
		if (std::dynamic_pointer_cast<MV::Scene::Grid>(a_component) ||
			std::dynamic_pointer_cast<MV::Scene::Spine>(a_component) ||
			std::dynamic_pointer_cast<MV::Scene::Parallax>(a_component)) {
			return std::make_unique<HighlightGizmo>(*this, a_component);
		}
		if (std::dynamic_pointer_cast<MV::Scene::PathMap>(a_component) ||
			std::dynamic_pointer_cast<MV::Scene::Emitter>(a_component) ||
			std::dynamic_pointer_cast<MV::Scene::Text>(a_component) ||
			std::dynamic_pointer_cast<MV::Scene::Sprite>(a_component) ||
			std::dynamic_pointer_cast<MV::Scene::Clickable>(a_component)) {
			return std::make_unique<BoundsGizmo>(*this, a_component);
		}
		if (auto drawable = std::dynamic_pointer_cast<MV::Scene::Drawable>(a_component)) {
			return std::make_unique<PointsGizmo>(*this, drawable);
		}
		return nullptr;
	}

	void GizmoLayer::update() {
		if (nodeTarget && !nodeTarget->parent()) {
			select(nullptr);   // an undo or external mutation removed the node underneath us
		}
		if (componentTarget && componentOwner.expired()) {
			editComponent(nullptr);
		}
		for (auto* gizmo : { nodeGizmo.get(), componentGizmo.get() }) {
			if (gizmo && !gizmo->dragging && gizmo->dirty()) {
				gizmo->sync();
			}
		}
	}

}
