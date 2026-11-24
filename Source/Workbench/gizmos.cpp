#include "gizmos.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>
#include "MV/Utility/generalUtility.h"

namespace Workbench {

	namespace {
		// Old-editor handle palette (editorDefines.h): amber corners.
		const MV::Color sizeHandleFill(0x44ffb400);
		const MV::Color highlightFill(0x11ffffff);
		const MV::Color hardBoundsColor(0xccff6040);
		const MV::Color softBoundsColor(0xcc40e080);
		// Dock chrome Clickables sit at the default 100; +100 wins inside the viewport. The transform
		// handles sit above a bounds body, point handles between, so overlaps resolve smallest-first.
		constexpr int64_t bodyPriorityBoost = 100;
		constexpr int64_t pointPriorityBoost = 105;
		constexpr int64_t axisPriorityBoost = 110;
		constexpr int64_t centerPriorityBoost = 112;

		const MV::Color axisXColor(0xe5412b);
		const MV::Color axisYColor(0x7ed63a);
		const MV::Color axisZColor(0x3a7bf8);
		const MV::Color handleHighlightColor(0xf6e532);
		const MV::Color centerHandleColor(0xe0e0e0);
		const MV::Color sweepFill(0x55f6e532);
		const MV::Color sweepLabelBackground(0xcc151515);

		// logical pixels
		constexpr float axisLength = 84.0f;
		constexpr float axisThickness = 2.0f;
		constexpr float axisHitThickness = 14.0f;
		constexpr float arrowHeadLength = 16.0f;
		constexpr float arrowHeadWidth = 11.0f;
		constexpr float scaleEndSize = 11.0f;
		constexpr float centerHandleSize = 12.0f;
		constexpr float pivotDotSize = 5.0f;
		constexpr float ringRadius = 72.0f;
		constexpr GLuint ringSegments = 96;
		constexpr int ringHitBoxes = 32;
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
		virtual void update() {
			if (!dragging && dirty()) {
				sync();
			}
		}
		virtual bool takesInput() const { return true; }
		virtual std::optional<MV::Point<int>> cornerScreenPoint(size_t, size_t) const { return std::nullopt; }

		bool dragging = false;
		GizmoLayer& layer;
		std::shared_ptr<MV::Scene::Node> container;
	};

	// The selection's bounds outline plus the active tool's transform handles. The handles hang off one node
	// at the target's pivot: a drag moves that node with the target and never rebuilds what is being dragged.
	struct GizmoLayer::NodeGizmo : GizmoLayer::Gizmo {
		enum class Gesture { NONE, MOVE, ROTATE, SCALE };

		struct Handle {
			std::string name;
			Gesture gesture = Gesture::NONE;
			GizmoAxis axis = GizmoAxis::BOTH;
			MV::Color idle;
			std::vector<std::shared_ptr<MV::Scene::Clickable>> hitAreas;
			std::vector<std::shared_ptr<MV::Scene::Drawable>> visuals;
			std::shared_ptr<MV::Scene::Node> grip;
			MV::Point<> gripPoint;
			bool highlighted = false;
		};

		NodeGizmo(GizmoLayer& a_layer, const std::shared_ptr<MV::Scene::Node>& a_target) :
			Gizmo(a_layer),
			target(a_target) {
			container = a_layer.overlay->make("nodeGizmo");
			outline = container->make("outline")->attach<MV::Scene::Drawable>().self();
			handleRoot = container->make("handles");
			sync();
		}

		~NodeGizmo() override {
			releaseHandles();
		}

		// The target's own bounds (its children's when it has none) as four corners, so the outline turns with the node.
		// World space, not screen: the overlay is identity in visor world, and screen coordinates diverge from world under DPI scaling.
		std::array<MV::Point<>, 4> targetCorners() const {
			auto box = target->bounds(false);
			if (box.size().width <= 0.0f && box.size().height <= 0.0f) {
				box = target->bounds(true);
			}
			std::array<MV::Point<>, 4> result{ box.topLeftPoint(), box.topRightPoint(), box.bottomRightPoint(), box.bottomLeftPoint() };
			for (auto&& corner : result) {
				corner = MV::round<MV::PointPrecision>(target->worldFromLocal(corner));
			}
			return result;
		}

		MV::Point<> targetAnchor() const {
			return MV::round<MV::PointPrecision>(target->worldPosition());
		}

		// Overlay-space angle in degrees of a direction given in the target's parent space.
		float overlayAngleOfParentDirection(const MV::Point<>& a_parentDirection, float a_fallback) const {
			auto parent = target->parent();
			if (!parent) {
				return a_fallback;
			}
			auto from = parent->worldFromLocal(target->position());
			auto to = parent->worldFromLocal(target->position() + a_parentDirection * 100.0f);
			if (MV::distance(from, to) < 0.0001f) {
				return a_fallback;
			}
			return MV::toDegrees(std::atan2(to.y - from.y, to.x - from.x));
		}

		// Move and rotate work in the parent's axes; scale works in the target's own, so its handles turn with it.
		// The vertical handle is drawn towards -y, which is up on screen.
		std::pair<float, float> handleAxisAngles() const {
			if (layer.shownTool() == Tool::SCALE) {
				float turned = target->rotationRad().z;
				return {
					overlayAngleOfParentDirection(MV::point(std::cos(turned), std::sin(turned)), 0.0f),
					overlayAngleOfParentDirection(MV::point(std::sin(turned), -std::cos(turned)), -90.0f)
				};
			}
			return { overlayAngleOfParentDirection(MV::point(1.0f, 0.0f), 0.0f), overlayAngleOfParentDirection(MV::point(0.0f, -1.0f), -90.0f) };
		}

		bool outlineDirty() const {
			return targetCorners() != lastCorners;
		}

		bool handlesDirty() const {
			if (builtTool != layer.shownTool()) {
				return true;
			}
			auto angles = handleAxisAngles();
			return std::abs(angles.first - builtAngles.first) > 0.01f || std::abs(angles.second - builtAngles.second) > 0.01f;
		}

		bool dirty() const override {
			return outlineDirty() || handlesDirty() || !(targetAnchor() == lastAnchor);
		}

		void sync() override {
			syncOutline();
			if (handlesDirty()) {
				buildHandles();
			}
			followTarget();
		}

		void update() override {
			if (dragging) {
				followTarget();
			} else if (dirty()) {
				sync();
			}
			highlightHandles();
		}

		void followTarget() {
			if (outlineDirty()) {
				syncOutline();
			}
			lastAnchor = targetAnchor();
			handleRoot->worldPosition(lastAnchor);
		}

		void syncOutline() {
			lastCorners = targetCorners();
			std::vector<MV::DrawPoint> points;
			std::vector<GLuint> indices;
			if (MV::distance(lastCorners[0], lastCorners[2]) >= 1.0f) {
				MV::Color edge = layer.theme.accent();
				edge.A = 0.85f;
				float halfThickness = layer.theme.px(1.0f) * 0.5f;
				for (size_t i = 0; i < lastCorners.size(); ++i) {
					auto from = lastCorners[i];
					auto to = lastCorners[(i + 1) % lastCorners.size()];
					auto along = (to - from).normalized() * halfThickness;
					MV::Point<> across(-along.y, along.x);
					auto first = static_cast<GLuint>(points.size());
					for (auto&& point : { from - along - across, from - along + across, to + along + across, to + along - across }) {
						points.emplace_back(point, edge);
					}
					indices.insert(indices.end(), { first, first + 1, first + 2, first + 2, first + 3, first });
				}
			}
			outline->setPoints(points, indices);
		}

		void releaseHandles() {
			// a pressed Clickable keeps itself alive through its move receiver, and its callbacks point here
			for (auto&& handle : handles) {
				for (auto&& hitArea : handle->hitAreas) {
					hitArea->cancelPress(false);
				}
			}
			handles.clear();
			activeHandle = nullptr;
			sweepWedge.reset();
			sweepLabel.reset();
			handleRoot->clear();
		}

		void buildHandles() {
			releaseHandles();
			builtTool = layer.shownTool();
			builtAngles = handleAxisAngles();
			switch (builtTool) {
				case Tool::MOVE:
					makeAxisHandle("moveX", Gesture::MOVE, GizmoAxis::X, builtAngles.first, axisXColor, true);
					makeAxisHandle("moveY", Gesture::MOVE, GizmoAxis::Y, builtAngles.second, axisYColor, true);
					makeCenterHandle("moveFree", Gesture::MOVE);
				break;
				case Tool::ROTATE:
					makeRingHandle();
				break;
				case Tool::SCALE:
					makeAxisHandle("scaleX", Gesture::SCALE, GizmoAxis::X, builtAngles.first, axisXColor, false);
					makeAxisHandle("scaleY", Gesture::SCALE, GizmoAxis::Y, builtAngles.second, axisYColor, false);
					makeCenterHandle("scaleUniform", Gesture::SCALE);
				break;
				default:
				break;
			}
		}

		Handle& addHandle(const std::string& a_name, Gesture a_gesture, GizmoAxis a_axis, const MV::Color& a_idle) {
			auto& result = *handles.emplace_back(std::make_unique<Handle>());
			result.name = a_name;
			result.gesture = a_gesture;
			result.axis = a_axis;
			result.idle = a_idle;
			return result;
		}

		void addHitArea(Handle& a_handle, const std::shared_ptr<MV::Scene::Node>& a_node, const MV::BoxAABB<>& a_bounds, int64_t a_priorityBoost) {
			auto hitArea = a_node->attach<MV::Scene::Clickable>(layer.mouse);
			hitArea->bounds(a_bounds);
			hitArea->globalPriority(hitArea->globalPriority() + a_priorityBoost);
			auto* handle = &a_handle;
			hitArea->onPress.connect("press", [this, handle](std::shared_ptr<MV::Scene::Clickable> a_hitArea) {
				pressHandle(*handle, a_hitArea->mouse().position());
			});
			hitArea->onDrag.connect("drag", [this](std::shared_ptr<MV::Scene::Clickable> a_hitArea, const MV::Point<int>&, const MV::Point<int>&) {
				dragHandle(a_hitArea->mouse().position());
			});
			hitArea->onRelease.connect("release", [this](std::shared_ptr<MV::Scene::Clickable>, const MV::Point<MV::PointPrecision>&) {
				endGesture();
			});
			a_handle.hitAreas.push_back(hitArea.self());
		}

		// A shaft along the node's +x with an arrow head (move) or a square end (scale); the node is turned to the axis.
		void makeAxisHandle(const std::string& a_name, Gesture a_gesture, GizmoAxis a_axis, float a_angle, const MV::Color& a_color, bool a_arrowHead) {
			auto& theme = layer.theme;
			auto& handle = addHandle(a_name, a_gesture, a_axis, a_color);
			auto node = handleRoot->make(a_name);
			node->rotation(MV::AxisAngles(0.0f, 0.0f, a_angle));

			float length = theme.px(axisLength);
			float start = theme.px(centerHandleSize) * 0.5f;
			float halfThickness = theme.px(axisThickness) * 0.5f;
			float endLength = theme.px(a_arrowHead ? arrowHeadLength : scaleEndSize);
			float endHalfWidth = theme.px(a_arrowHead ? arrowHeadWidth : scaleEndSize) * 0.5f;
			float halfHit = theme.px(axisHitThickness) * 0.5f;

			auto shaft = node->attach<MV::Scene::Sprite>();
			shaft->bounds(MV::BoxAABB<>(MV::point(start, -halfThickness), MV::point(length - endLength, halfThickness)))->color(a_color);
			handle.visuals.push_back(shaft.self());

			auto end = node->attach<MV::Scene::Drawable>();
			if (a_arrowHead) {
				end->setPoints({
					MV::DrawPoint(MV::point(length - endLength, -endHalfWidth), a_color),
					MV::DrawPoint(MV::point(length, 0.0f), a_color),
					MV::DrawPoint(MV::point(length - endLength, endHalfWidth), a_color)
				}, { 0, 1, 2 });
			} else {
				end->setPoints({
					MV::DrawPoint(MV::point(length - endLength, -endHalfWidth), a_color),
					MV::DrawPoint(MV::point(length, -endHalfWidth), a_color),
					MV::DrawPoint(MV::point(length, endHalfWidth), a_color),
					MV::DrawPoint(MV::point(length - endLength, endHalfWidth), a_color)
				}, { 0, 1, 2, 2, 3, 0 });
			}
			handle.visuals.push_back(end.self());

			addHitArea(handle, node, MV::BoxAABB<>(MV::point(start, -halfHit), MV::point(length, halfHit)), axisPriorityBoost);
			handle.grip = node;
			handle.gripPoint = MV::point(length * 0.6f, 0.0f);
		}

		void makeCenterHandle(const std::string& a_name, Gesture a_gesture) {
			auto& handle = addHandle(a_name, a_gesture, GizmoAxis::BOTH, centerHandleColor);
			auto node = handleRoot->make(a_name);
			MV::BoxAABB<> square(MV::size(layer.theme.px(centerHandleSize), layer.theme.px(centerHandleSize)), true);
			auto fill = node->attach<MV::Scene::Sprite>();
			fill->bounds(square)->color(centerHandleColor);
			handle.visuals.push_back(fill.self());
			addHitArea(handle, node, square, centerPriorityBoost);
			handle.grip = node;
		}

		void makeRingHandle() {
			auto& theme = layer.theme;
			auto& handle = addHandle("rotate", Gesture::ROTATE, GizmoAxis::BOTH, axisZColor);
			float radius = theme.px(ringRadius);
			float halfThickness = theme.px(axisThickness) * 0.5f;

			sweepWedge = handleRoot->make("sweep")->attach<MV::Scene::Drawable>().self();

			auto ringNode = handleRoot->make("rotate");
			std::vector<MV::DrawPoint> ringPoints;
			std::vector<GLuint> ringIndices;
			for (GLuint i = 0; i < ringSegments; ++i) {
				float angle = static_cast<float>(MV::PIE) * 2.0f * static_cast<float>(i) / static_cast<float>(ringSegments);
				MV::Point<> direction(std::cos(angle), std::sin(angle));
				ringPoints.emplace_back(direction * (radius - halfThickness), axisZColor);
				ringPoints.emplace_back(direction * (radius + halfThickness), axisZColor);
				GLuint next = (i + 1) % ringSegments;
				ringIndices.insert(ringIndices.end(), { i * 2, i * 2 + 1, next * 2 + 1, next * 2 + 1, next * 2, i * 2 });
			}
			auto ring = ringNode->attach<MV::Scene::Drawable>();
			ring->setPoints(ringPoints, ringIndices);
			handle.visuals.push_back(ring.self());
			auto pivotDot = ringNode->attach<MV::Scene::Sprite>();
			pivotDot->bounds(MV::BoxAABB<>(MV::size(theme.px(pivotDotSize), theme.px(pivotDotSize)), true))->color(axisZColor);
			handle.visuals.push_back(pivotDot.self());

			// a Clickable hit-tests one box, so the ring is a fan of them
			float halfHit = theme.px(axisHitThickness) * 0.5f;
			float halfChord = (radius + halfHit) * std::tan(static_cast<float>(MV::PIE) / static_cast<float>(ringHitBoxes));
			for (int i = 0; i < ringHitBoxes; ++i) {
				auto hitNode = ringNode->make("hit_" + std::to_string(i));
				hitNode->rotation(MV::AxisAngles(0.0f, 0.0f, 360.0f * static_cast<float>(i) / static_cast<float>(ringHitBoxes)));
				addHitArea(handle, hitNode, MV::BoxAABB<>(MV::point(radius - halfHit, -halfChord), MV::point(radius + halfHit, halfChord)), axisPriorityBoost);
				if (!handle.grip) {
					handle.grip = hitNode;
					handle.gripPoint = MV::point(radius, 0.0f);
				}
			}

			MV::Size<> labelSize(theme.px(72.0f), theme.rowHeight);
			auto labelNode = handleRoot->make("sweepLabel");
			labelNode->position(MV::point(-labelSize.width * 0.5f, -radius - labelSize.height - theme.px(8.0f)));
			labelNode->attach<MV::Scene::Sprite>()->bounds(labelSize)->color(sweepLabelBackground);
			sweepLabel = labelNode->attach<MV::Scene::Text>(layer.textLibrary, theme.labelFont).self();
			sweepLabel->bounds({ MV::Point<>(), labelSize });
			sweepLabel->justification(MV::TextJustification::CENTER)->wrapping(MV::TextWrapMethod::NONE, labelSize.width)->minimumLineHeight(labelSize.height)->text("");
			labelNode->hide();
		}

		// The pie the drag has swept and its angle, as applied (so a snapped drag steps).
		float shownSweep = 0.0f;

		void showSweep(float a_degrees) {
			if (!sweepWedge || !sweepLabel) {
				return;
			}
			// a mirrored parent turns the other way on screen; read from the parent's own axes, not from where the handles point
			float xAngle = overlayAngleOfParentDirection(MV::point(1.0f, 0.0f), 0.0f);
			float yAngle = overlayAngleOfParentDirection(MV::point(0.0f, 1.0f), 90.0f);
			float handedness = std::sin(MV::toRadians(yAngle - xAngle)) < 0.0f ? -1.0f : 1.0f;
			float shown = std::clamp(a_degrees * handedness, -360.0f, 360.0f);
			shownSweep = shown;
			float radius = layer.theme.px(ringRadius);
			int steps = std::max(1, static_cast<int>(std::ceil(std::abs(shown) / 4.0f)));
			std::vector<MV::DrawPoint> points{ MV::DrawPoint(MV::Point<>(), sweepFill) };
			std::vector<GLuint> indices;
			for (int i = 0; i <= steps; ++i) {
				float angle = MV::toRadians(pressCursorOverlayAngle + shown * static_cast<float>(i) / static_cast<float>(steps));
				points.emplace_back(MV::point(std::cos(angle) * radius, std::sin(angle) * radius), sweepFill);
				if (i > 0) {
					indices.insert(indices.end(), { 0u, static_cast<GLuint>(i), static_cast<GLuint>(i + 1) });
				}
			}
			sweepWedge->setPoints(points, indices);
			char text[32];
			std::snprintf(text, sizeof(text), "%+.1f", a_degrees);
			sweepLabel->text(std::string(text));
			sweepLabel->owner()->show();
		}

		void hideSweep() {
			shownSweep = 0.0f;
			if (sweepWedge) {
				sweepWedge->setPoints({}, {});
			}
			if (sweepLabel) {
				sweepLabel->owner()->hide();
			}
		}

		// Hover and drag feedback: the dragged handle, else the topmost one under the cursor, turns yellow.
		void highlightHandles() {
			Handle* lit = activeHandle;
			if (!dragging) {
				for (auto&& handle : handles) {
					for (auto&& hitArea : handle->hitAreas) {
						if (hitArea->mouseInBounds()) {
							lit = handle.get();
							break;
						}
					}
				}
			}
			for (auto&& handle : handles) {
				bool highlight = handle.get() == lit;
				if (handle->highlighted != highlight) {
					handle->highlighted = highlight;
					for (auto&& visual : handle->visuals) {
						visual->color(highlight ? handleHighlightColor : handle->idle);
					}
				}
			}
		}

		void beginGesture(Gesture a_gesture, GizmoAxis a_axis) {
			dragging = true;
			activeGesture = a_gesture;
			activeAxis = a_axis;
			pressPosition = target->position();
			pressRotation = target->rotationRad();
			pressScale = target->scale();
		}

		void pressHandle(Handle& a_handle, const MV::Point<int>& a_cursor) {
			beginGesture(a_handle.gesture, a_handle.axis);
			activeHandle = &a_handle;
			auto parent = target->parent();
			pressCursorParent = parent ? parent->localFromScreen(a_cursor) : MV::Point<>();
			pressCursorOverlay = layer.overlay->localFromScreen(a_cursor);
			auto fromPivot = pressCursorOverlay - target->worldPosition();
			pressCursorOverlayAngle = MV::toDegrees(std::atan2(fromPivot.y, fromPivot.x));
			lastCursorParentAngle = parentAngleAbout(pressCursorParent);
			sweptRadians = 0.0f;
			layer.pressed();
		}

		float parentAngleAbout(const MV::Point<>& a_parentPoint) const {
			return std::atan2(a_parentPoint.y - pressPosition.y, a_parentPoint.x - pressPosition.x);
		}

		// One axis length of drag along the handle doubles the scale; the centre grows right and up.
		float scaleFactorAt(const MV::Point<>& a_cursorOverlay) const {
			auto dragged = a_cursorOverlay - pressCursorOverlay;
			float length = layer.theme.px(axisLength);
			if (activeAxis == GizmoAxis::BOTH) {
				return 1.0f + (dragged.x - dragged.y) / length;
			}
			float angle = MV::toRadians(activeAxis == GizmoAxis::X ? builtAngles.first : builtAngles.second);
			return 1.0f + (dragged.x * std::cos(angle) + dragged.y * std::sin(angle)) / length;
		}

		void dragHandle(const MV::Point<int>& a_cursor) {
			auto parent = target->parent();
			if (!dragging || !parent) {
				return;
			}
			bool snap = ((SDL_GetModState() & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL)) != 0) != layer.snapAlways;
			switch (activeGesture) {
				case Gesture::MOVE:
					applyMove(parent->localFromScreen(a_cursor) - pressCursorParent, snap);
				break;
				case Gesture::ROTATE: {
					float angle = parentAngleAbout(parent->localFromScreen(a_cursor));
					float turned = angle - lastCursorParentAngle;
					float fullTurn = static_cast<float>(MV::PIE) * 2.0f;
					sweptRadians += turned - fullTurn * std::round(turned / fullTurn);
					lastCursorParentAngle = angle;
					applyRotate(MV::toDegrees(sweptRadians), snap);
				}
				break;
				case Gesture::SCALE:
					applyScale(scaleFactorAt(layer.overlay->localFromScreen(a_cursor)), snap);
				break;
				default:
				break;
			}
		}

		static float snapped(float a_value, float a_step) {
			return a_step > 0.0f ? std::round(a_value / a_step) * a_step : a_value;
		}

		void applyMove(const MV::Point<>& a_parentDelta, bool a_snap) {
			auto result = pressPosition;
			if (activeAxis != GizmoAxis::Y) {
				result.x += a_parentDelta.x;
				result.x = a_snap ? snapped(result.x, layer.snapSteps.moveX) : result.x;
			}
			if (activeAxis != GizmoAxis::X) {
				result.y += a_parentDelta.y;
				result.y = a_snap ? snapped(result.y, layer.snapSteps.moveY) : result.y;
			}
			target->position(result);
			applied();
		}

		void applyRotate(float a_degrees, bool a_snap) {
			auto result = pressRotation;
			float pressDegrees = MV::toDegrees(pressRotation.z);
			float appliedDegrees = a_degrees;
			if (a_snap) {
				appliedDegrees = snapped(pressDegrees + a_degrees, layer.snapSteps.rotate) - pressDegrees;
				result.z = MV::toRadians(pressDegrees + appliedDegrees);
			} else {
				result.z = pressRotation.z + MV::toRadians(a_degrees);
			}
			target->rotationRad(result);
			if (activeHandle) {
				showSweep(appliedDegrees);
			}
			applied();
		}

		void applyScale(float a_factor, bool a_snap) {
			auto result = pressScale;
			if (activeAxis != GizmoAxis::Y) {
				result.x = a_snap ? snapped(pressScale.x * a_factor, layer.snapSteps.scale) : pressScale.x * a_factor;
			}
			if (activeAxis != GizmoAxis::X) {
				result.y = a_snap ? snapped(pressScale.y * a_factor, layer.snapSteps.scale) : pressScale.y * a_factor;
			}
			target->scale(result);
			applied();
		}

		void applied() {
			followTarget();
			layer.editing();
		}

		void endGesture() {
			if (activeGesture == Gesture::NONE) {
				return;
			}
			auto gesture = activeGesture;
			dragging = false;
			activeGesture = Gesture::NONE;
			activeHandle = nullptr;
			hideSweep();
			recordGesture(gesture);
			followTarget();
			layer.edited();
		}

		void recordGesture(Gesture a_gesture) {
			std::weak_ptr<MV::Scene::Node> weakTarget = target;
			switch (a_gesture) {
				case Gesture::MOVE: {
					auto oldPosition = pressPosition;
					auto newPosition = target->position();
					if (newPosition != oldPosition) {
						layer.history.record("move " + target->id(),
							[weakTarget, newPosition]() { if (auto locked = weakTarget.lock()) { locked->position(newPosition); } },
							[weakTarget, oldPosition]() { if (auto locked = weakTarget.lock()) { locked->position(oldPosition); } });
					}
				}
				break;
				case Gesture::ROTATE: {
					auto oldRotation = pressRotation;
					auto newRotation = target->rotationRad();
					if (newRotation != oldRotation) {
						layer.history.record("rotate " + target->id(),
							[weakTarget, newRotation]() { if (auto locked = weakTarget.lock()) { locked->rotationRad(newRotation); } },
							[weakTarget, oldRotation]() { if (auto locked = weakTarget.lock()) { locked->rotationRad(oldRotation); } });
					}
				}
				break;
				case Gesture::SCALE: {
					auto oldScale = pressScale;
					auto newScale = target->scale();
					if (newScale != oldScale) {
						layer.history.record("scale " + target->id(),
							[weakTarget, newScale]() { if (auto locked = weakTarget.lock()) { locked->scale(newScale); } },
							[weakTarget, oldScale]() { if (auto locked = weakTarget.lock()) { locked->scale(oldScale); } });
					}
				}
				break;
				default:
				break;
			}
		}

		std::shared_ptr<MV::Scene::Node> target;
		std::shared_ptr<MV::Scene::Drawable> outline;
		std::shared_ptr<MV::Scene::Node> handleRoot;
		std::vector<std::unique_ptr<Handle>> handles;
		std::shared_ptr<MV::Scene::Drawable> sweepWedge;
		std::shared_ptr<MV::Scene::Text> sweepLabel;

		Tool builtTool = Tool::SELECT;
		std::pair<float, float> builtAngles{ 0.0f, 90.0f };
		std::array<MV::Point<>, 4> lastCorners;
		MV::Point<> lastAnchor;

		Gesture activeGesture = Gesture::NONE;
		GizmoAxis activeAxis = GizmoAxis::BOTH;
		Handle* activeHandle = nullptr;
		MV::Point<> pressPosition;
		MV::AxisAngles pressRotation;
		MV::Scale pressScale;
		MV::Point<> pressCursorParent;
		MV::Point<> pressCursorOverlay;
		float pressCursorOverlayAngle = 0.0f;
		float lastCursorParentAngle = 0.0f;
		float sweptRadians = 0.0f;
	};

	struct GizmoLayer::BoundsGizmo : GizmoLayer::Gizmo {
		// A box in the component's owner's space that is not the component's own bounds, framed in a_outline.
		BoundsGizmo(GizmoLayer& a_layer, const std::shared_ptr<MV::Scene::Component>& a_component, std::function<MV::BoxAABB<>()> a_read, std::function<void(const MV::BoxAABB<>&)> a_write, const MV::Color& a_outline, int64_t a_priority) :
			Gizmo(a_layer),
			component(a_component),
			readLocal(std::move(a_read)),
			writeLocal(std::move(a_write)),
			outline(a_outline),
			outlined(true),
			priority(a_priority) {
			container = a_layer.overlay->make(MV::guid("boundsGizmo_"));
			std::weak_ptr<MV::Scene::Component> weak = a_component;
			auto reader = readLocal;
			readFrame = [weak, reader]() {
				auto locked = weak.lock();
				return locked && locked->ownerIsAlive() ? locked->owner()->worldFromLocal(reader()) : MV::BoxAABB<>();
			};
			sync();
		}

		BoundsGizmo(GizmoLayer& a_layer, const std::shared_ptr<MV::Scene::Component>& a_component) :
			Gizmo(a_layer),
			component(a_component) {
			container = a_layer.overlay->make(MV::guid("boundsGizmo_"));

			std::weak_ptr<MV::Scene::Component> weak = a_component;
			readFrame = [weak]() {
				auto locked = weak.lock();
				return locked ? locked->worldBounds() : MV::BoxAABB<>();
			};
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
			auto frame = MV::round<MV::PointPrecision>(readFrame());
			return !(frame.minPoint == lastFrame.minPoint) || !(frame.maxPoint == lastFrame.maxPoint);
		}

		std::optional<MV::Point<int>> cornerScreenPoint(size_t a_box, size_t a_corner) const override {
			const MV::Scene::SafeComponent<MV::Scene::Clickable>* corners[] { &topLeft, &topRight, &bottomRight, &bottomLeft };
			if (a_box != 0 || a_corner >= 4 || !*corners[a_corner]) {
				return std::nullopt;
			}
			auto& corner = *corners[a_corner];
			return corner->owner()->screenFromLocal(corner->bounds()).centerPoint();
		}

		void sync() override {
			container->clear();
			auto rectBox = MV::round<MV::PointPrecision>(readFrame());
			lastFrame = rectBox;
			auto handleSize = MV::size(layer.theme.px(5.0f), layer.theme.px(5.0f));
			if (outlined) {
				drawOutline(rectBox);
			}

			body = container->make(MV::guid("body_"))->worldPosition(rectBox.topLeftPoint())->attach<MV::Scene::Clickable>(layer.mouse);
			body->bounds({ MV::Point<>(), rectBox.size() });
			body->globalPriority(body->globalPriority() + bodyPriorityBoost + priority);
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
					writeLocal(MV::BoxAABB<>(pressBounds.minPoint + localMove(pressBodyAt, a_handle->owner()->worldPosition()), current.size()));
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
			corner->bounds(MV::size(layer.theme.px(5.0f), layer.theme.px(5.0f)))->color(outlined ? outline : sizeHandleFill)->show();
			corner->globalPriority(corner->globalPriority() + bodyPriorityBoost + priority);
			return corner;
		}

		void drawOutline(const MV::BoxAABB<>& a_frame) {
			const auto thickness = layer.theme.px(1.5f);
			const MV::BoxAABB<> edges[] {
				{ a_frame.minPoint, MV::point(a_frame.maxPoint.x, a_frame.minPoint.y + thickness) },
				{ MV::point(a_frame.minPoint.x, a_frame.maxPoint.y - thickness), a_frame.maxPoint },
				{ a_frame.minPoint, MV::point(a_frame.minPoint.x + thickness, a_frame.maxPoint.y) },
				{ MV::point(a_frame.maxPoint.x - thickness, a_frame.minPoint.y), a_frame.maxPoint }
			};
			for (auto&& edge : edges) {
				container->make(MV::guid("edge_"))->worldPosition(edge.minPoint)->attach<MV::Scene::Sprite>()->bounds(edge.size())->color(outline);
			}
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

		// The handles frame the component's box in world space, which a turned owner makes larger than the box itself,
		// so a drag moves the box's corners by how far the handles moved in the component's own space.
		void applyHandleBounds() {
			MV::BoxAABB<> local(pressBounds.minPoint + localMove(pressTopLeftAt, topLeft->worldBounds().bottomRightPoint()));
			local.expandWith(pressBounds.maxPoint + localMove(pressBottomRightAt, bottomRight->worldBounds().topLeftPoint()));
			writeLocal(local);
			auto rectBox = MV::round<MV::PointPrecision>(readFrame());
			body->bounds(rectBox.size());
			body->owner()->worldPosition(rectBox.minPoint);
		}

		MV::Point<> localMove(const MV::Point<>& a_fromWorld, const MV::Point<>& a_toWorld) const {
			auto owner = component->owner();
			return owner->localFromWorld(a_toWorld) - owner->localFromWorld(a_fromWorld);
		}

		void beginGesture() {
			dragging = true;
			pressBounds = readLocal();
			pressBodyAt = body->owner()->worldPosition();
			pressTopLeftAt = topLeft->worldBounds().bottomRightPoint();
			pressBottomRightAt = bottomRight->worldBounds().topLeftPoint();
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
		std::function<MV::BoxAABB<>()> readFrame;
		std::function<void()> restore;
		bool painter = false;
		MV::Color outline;
		bool outlined = false;
		int64_t priority = 0;

		MV::Scene::SafeComponent<MV::Scene::Clickable> body;
		MV::Scene::SafeComponent<MV::Scene::Clickable> topLeft;
		MV::Scene::SafeComponent<MV::Scene::Clickable> topRight;
		MV::Scene::SafeComponent<MV::Scene::Clickable> bottomLeft;
		MV::Scene::SafeComponent<MV::Scene::Clickable> bottomRight;

		MV::BoxAABB<> lastFrame;
		MV::BoxAABB<> pressBounds;
		MV::Point<> pressBodyAt;
		MV::Point<> pressTopLeftAt;
		MV::Point<> pressBottomRightAt;
		std::vector<std::pair<MV::Point<int>, bool>> paintedCells;
		MV::Point<int> lastPaintCell{ -1, -1 };
	};

	// Two boxes on one component, each drawn and dragged as a BoundsGizmo: a CameraConstraint's hard and soft bounds.
	struct GizmoLayer::BoundsPairGizmo : GizmoLayer::Gizmo {
		BoundsPairGizmo(GizmoLayer& a_layer, const std::shared_ptr<MV::Scene::CameraConstraint>& a_constraint) :
			Gizmo(a_layer) {
			std::weak_ptr<MV::Scene::CameraConstraint> weak = a_constraint;
			auto box = [&](bool a_soft, const MV::Color& a_color, int64_t a_priority) {
				return std::make_unique<BoundsGizmo>(a_layer, a_constraint,
					[weak, a_soft]() {
						auto locked = weak.lock();
						return locked ? locked->bounds(a_soft) : MV::BoxAABB<>();
					},
					[weak, a_soft](const MV::BoxAABB<>& a_bounds) {
						if (auto locked = weak.lock()) {
							if (a_soft) {
								locked->soft(a_bounds);
							} else {
								locked->hard(a_bounds);
							}
						}
					}, a_color, a_priority);
			};
			hard = box(false, hardBoundsColor, 0);
			soft = box(true, softBoundsColor, 1);
		}

		bool dirty() const override { return false; }

		void sync() override {
			hard->sync();
			soft->sync();
		}

		void update() override {
			hard->update();
			soft->update();
			dragging = hard->dragging || soft->dragging;
		}

		std::optional<MV::Point<int>> cornerScreenPoint(size_t a_box, size_t a_corner) const override {
			return a_box == 0 ? hard->cornerScreenPoint(0, a_corner) : a_box == 1 ? soft->cornerScreenPoint(0, a_corner) : std::nullopt;
		}

		std::unique_ptr<BoundsGizmo> hard;
		std::unique_ptr<BoundsGizmo> soft;
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

		bool takesInput() const override { return false; }

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

	GizmoLayer::GizmoLayer(const std::shared_ptr<MV::Scene::Node>& a_overlay, MV::TapDevice& a_mouse, CommandHistory& a_history, const Theme& a_theme, MV::TextLibrary& a_textLibrary) :
		overlay(a_overlay),
		mouse(a_mouse),
		history(a_history),
		theme(a_theme),
		textLibrary(a_textLibrary) {
	}

	GizmoLayer::~GizmoLayer() = default;

	Tool GizmoLayer::shownTool() const {
		return hideHandles || (componentGizmo && componentGizmo->takesInput()) ? Tool::SELECT : activeTool;
	}

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

	void GizmoLayer::editing() {
		if (onEditing) {
			onEditing();
		}
	}

	void GizmoLayer::tool(Tool a_tool) {
		if (nodeGizmo && nodeGizmo->dragging) {
			return;
		}
		activeTool = a_tool;
		if (nodeGizmo) {
			nodeGizmo->sync();
		}
	}

	bool GizmoLayer::moveSelected(GizmoAxis a_axis, const MV::Point<>& a_parentDelta, bool a_snap) {
		if (!nodeGizmo || nodeGizmo->dragging) {
			return false;
		}
		nodeGizmo->beginGesture(NodeGizmo::Gesture::MOVE, a_axis);
		nodeGizmo->applyMove(a_parentDelta, a_snap);
		nodeGizmo->endGesture();
		return true;
	}

	bool GizmoLayer::rotateSelected(float a_degrees, bool a_snap) {
		if (!nodeGizmo || nodeGizmo->dragging) {
			return false;
		}
		nodeGizmo->beginGesture(NodeGizmo::Gesture::ROTATE, GizmoAxis::BOTH);
		nodeGizmo->applyRotate(a_degrees, a_snap);
		nodeGizmo->endGesture();
		return true;
	}

	bool GizmoLayer::scaleSelected(GizmoAxis a_axis, float a_factor, bool a_snap) {
		if (!nodeGizmo || nodeGizmo->dragging) {
			return false;
		}
		nodeGizmo->beginGesture(NodeGizmo::Gesture::SCALE, a_axis);
		nodeGizmo->applyScale(a_factor, a_snap);
		nodeGizmo->endGesture();
		return true;
	}

	std::optional<MV::Point<int>> GizmoLayer::handleScreenPoint(const std::string& a_handle) const {
		if (nodeGizmo) {
			for (auto&& handle : nodeGizmo->handles) {
				if (handle->name == a_handle) {
					return handle->grip->screenFromLocal(handle->gripPoint);
				}
			}
		}
		return std::nullopt;
	}

	std::optional<MV::Point<int>> GizmoLayer::componentCornerScreenPoint(size_t a_box, size_t a_corner) const {
		return componentGizmo ? componentGizmo->cornerScreenPoint(a_box, a_corner) : std::nullopt;
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
			std::dynamic_pointer_cast<MV::Scene::PathMap>(a_component) ||
			std::dynamic_pointer_cast<MV::Scene::CameraConstraint>(a_component));
	}

	std::unique_ptr<GizmoLayer::Gizmo> GizmoLayer::makeComponentGizmo(const std::shared_ptr<MV::Scene::Component>& a_component) {
		if (auto constraint = std::dynamic_pointer_cast<MV::Scene::CameraConstraint>(a_component)) {
			return std::make_unique<BoundsPairGizmo>(*this, constraint);
		}
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
		if (std::dynamic_pointer_cast<MV::Scene::Drawable>(a_component)) {
			return std::make_unique<HighlightGizmo>(*this, a_component);
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
		for (Gizmo* gizmo : { static_cast<Gizmo*>(nodeGizmo.get()), componentGizmo.get() }) {
			if (gizmo) {
				gizmo->update();
			}
		}
		gateInput();
	}

	float GizmoLayer::sweepShown() const {
		return nodeGizmo ? nodeGizmo->shownSweep : 0.0f;
	}

	void GizmoLayer::gateInput() {
		bool dragging = (nodeGizmo && nodeGizmo->dragging) || (componentGizmo && componentGizmo->dragging);
		bool covered = pointerOverScene && !dragging && !pointerOverScene(mouse.position());
		if (!covered) {
			for (auto&& [weakHitArea, boundsType] : gatedHitAreas) {
				if (auto hitArea = weakHitArea.lock()) {
					hitArea->clickDetectionType(boundsType);
				}
			}
			gatedHitAreas.clear();
			return;
		}
		// each frame while covered: a gizmo rebuilt in the meantime has new hit areas
		std::function<void(const std::shared_ptr<MV::Scene::Node>&)> gate = [&](const std::shared_ptr<MV::Scene::Node>& a_node) {
			if (auto components = a_node->property_value<std::vector<std::shared_ptr<MV::Scene::Component>>>("childComponents")) {
				for (auto&& component : *components) {
					auto hitArea = std::dynamic_pointer_cast<MV::Scene::Clickable>(component);
					if (hitArea && hitArea->enabled()) {
						gatedHitAreas.emplace_back(hitArea, hitArea->clickDetectionType());
						hitArea->disable();
					}
				}
			}
			for (auto&& child : *a_node) {
				gate(child);
			}
		};
		gate(overlay);
	}

}
