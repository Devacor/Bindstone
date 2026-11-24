#include "view.h"
#include <algorithm>
#include <jaiscript/core/dynamic_binder.hpp>

namespace MV {
	namespace Scene {

		void View::jai_auto_bind(jai::dynamic_binder<View>& a_builder) {
			a_builder.method("camera", &View::camera);
			a_builder.method("slot", &View::slot);
		}

		View::View(const std::weak_ptr<Node>& a_owner) :
			jai::property_owner<View, Sprite>(a_owner) {
		}

		std::shared_ptr<View> View::viewport(const BoxAABB<>& a_viewport) {
			viewportBox = a_viewport;
			refresh();
			return std::static_pointer_cast<View>(shared_from_this());
		}

		std::shared_ptr<View> View::clear(Clear a_clear) {
			clearKind = a_clear;
			return std::static_pointer_cast<View>(shared_from_this());
		}

		std::shared_ptr<View> View::depthMode(Depth a_depth) {
			depthKind = a_depth;
			return std::static_pointer_cast<View>(shared_from_this());
		}

		std::shared_ptr<View> View::cameraNode(const std::string& a_id) {
			cameraNodeId = a_id;
			refresh();
			return std::static_pointer_cast<View>(shared_from_this());
		}

		std::shared_ptr<View> View::subject(const std::string& a_id) {
			subjectId = a_id;
			return std::static_pointer_cast<View>(shared_from_this());
		}

		std::shared_ptr<Camera> View::camera() const {
			if (!ownerIsAlive()) {
				return nullptr;
			}
			if (!cameraNodeId->empty()) {
				auto node = owner()->id() == *cameraNodeId ? owner() : owner()->get(cameraNodeId, false);
				auto found = node ? node->component<Camera>(false, false) : SafeComponent<Camera>();
				return found ? found.self() : nullptr;
			}
			auto found = owner()->componentInChildren<Camera>(false, false, true);
			return found ? found.self() : nullptr;
		}

		TransformMatrix View::viewProjection() const {
			auto& renderer = owner()->renderer();
			const auto world = renderer.world().size();
			const auto left = std::clamp(viewportBox->minPoint.x, 0.0f, 1.0f);
			const auto top = std::clamp(viewportBox->minPoint.y, 0.0f, 1.0f);
			const auto width = std::max(std::clamp(viewportBox->maxPoint.x, 0.0f, 1.0f) - left, 0.0001f);
			const auto height = std::max(std::clamp(viewportBox->maxPoint.y, 0.0f, 1.0f) - top, 0.0001f);
			const Size<> seen(world.width * width, world.height * height);
			TransformMatrix projection;
			if (auto eye = camera()) {
				projection = eye->projectionFor(seen, renderer.window().contentScale(), renderer.window().userScale()) * eye->eye();
			} else {
				projection.makeOrtho(0.0f, seen.width, seen.height, 0.0f, -1.0f, 1.0f);
			}
			// clip space squeezed into the viewport's part of the window
			TransformMatrix place;
			place.access(0, 0) = width;
			place.access(1, 1) = height;
			place.access(3, 0) = left * 2.0f + width - 1.0f;
			place.access(3, 1) = 1.0f - (top * 2.0f + height);
			return place * projection;
		}

		BoxAABB<int> View::pixelRect() const {
			if (!ownerIsAlive()) {
				return {};
			}
			const auto pixels = owner()->renderer().targetPixelSize();
			return BoxAABB<int>(
				Point<int>(static_cast<int>(std::round(viewportBox->minPoint.x * pixels.width)), static_cast<int>(std::round(viewportBox->minPoint.y * pixels.height))),
				Point<int>(static_cast<int>(std::round(viewportBox->maxPoint.x * pixels.width)), static_cast<int>(std::round(viewportBox->maxPoint.y * pixels.height))));
		}

		std::shared_ptr<View> View::authoring(bool a_authoring) {
			if (isAuthoring != a_authoring) {
				isAuthoring = a_authoring;
				if (isAuthoring) {
					release();
				} else {
					claim();
				}
			}
			return std::static_pointer_cast<View>(shared_from_this());
		}

		void View::claim() {
			if (isAuthoring || !ownerIsAlive()) {
				return;
			}
			if (viewSlot < 0) {
				viewSlot = owner()->renderer().reserveViewSlot();
			}
			owner()->cameraId(viewSlot);
			refresh();
		}

		void View::release() {
			if (viewSlot >= 0 && ownerIsAlive()) {
				owner()->renderer().releaseViewSlot(viewSlot);
			}
			viewSlot = -1;
			if (ownerIsAlive()) {
				auto parent = owner()->parent();
				owner()->cameraId(parent ? parent->cameraId() : 0);
			}
		}

		void View::refresh() {
			if (isAuthoring || viewSlot < 0 || !ownerIsAlive()) {
				return;
			}
			owner()->renderer().viewProjection(viewSlot, viewProjection());
			// the area the camera sees, laid on the node's own plane: what anchors under it lay out against
			const auto rect = pixelRect();
			const Point<int> corners[] { rect.minPoint, Point<int>(rect.maxPoint.x, rect.minPoint.y), rect.maxPoint, Point<int>(rect.minPoint.x, rect.maxPoint.y) };
			BoxAABB<> area;
			bool first = true;
			for (auto&& corner : corners) {
				auto local = owner()->localFromScreen(corner);
				local.z = 0.0f;
				if (first) {
					area = BoxAABB<>(local, local);
					first = false;
				} else {
					area.expandWith(local);
				}
			}
			const auto shown = bounds();
			auto close = [](const Point<>& a_first, const Point<>& a_second) {
				return std::abs(a_first.x - a_second.x) < 0.01f && std::abs(a_first.y - a_second.y) < 0.01f;
			};
			if (!close(shown.minPoint, area.minPoint) || !close(shown.maxPoint, area.maxPoint)) {
				bounds(area);
			}
		}

		std::shared_ptr<Node> View::subjectNode() const {
			if (subjectId->empty() || !ownerIsAlive()) {
				return nullptr;
			}
			auto top = owner();
			while (top->parent()) {
				top = top->parent();
			}
			auto found = top->id() == *subjectId ? top : top->get(subjectId, false);
			// a subject that holds this View would draw it again inside itself
			for (auto node = owner(); found && node; node = node->parent()) {
				if (node == found) {
					return nullptr;
				}
			}
			return found;
		}

		void View::initialize() {
			Sprite::initialize();
			claim();
		}

		void View::postLoadInitialize() {
			Sprite::postLoadInitialize();
			claim();
		}

		void View::reattachImplementation() {
			Sprite::reattachImplementation();
			claim();
		}

		void View::detachImplementation() {
			release();
			Sprite::detachImplementation();
		}

		void View::onOwnerDestroyed() {
			viewSlot = -1;
			Sprite::onOwnerDestroyed();
		}

		void View::updateImplementation(double a_delta) {
			Sprite::updateImplementation(a_delta);
			refresh();
		}

		void View::defaultDrawImplementation() {
			if (drawingColor) {
				Sprite::defaultDrawImplementation();
			}
		}

		void View::beforeNodeDraw() {
			if (isAuthoring || viewSlot < 0 || !ownerIsAlive()) {
				return;
			}
			auto& renderer = owner()->renderer();
			// drawn as another View's subject, it is part of that View's picture (and two Views showing each other stop here)
			if (renderer.drawingThrough()) {
				return;
			}
			refresh();
			const auto rect = pixelRect();
			const auto pixels = renderer.targetPixelSize();
			scissored = rect.minPoint.x > 0 || rect.minPoint.y > 0 || rect.maxPoint.x < pixels.width || rect.maxPoint.y < pixels.height;
			if (scissored && renderer.device()) {
				renderer.device()->setScissor(Render::Rect{ rect.minPoint.x, pixels.height - rect.maxPoint.y, rect.width(), rect.height() });
			}
			const uint32_t groups = [&]() {
				auto eye = camera();
				return eye ? eye->sees() : 0xFFFFFFFFu;
			}();
			if (clearKind.get() == Clear::COLOR) {
				drawingColor = true;
				Sprite::defaultDrawImplementation();
				drawingColor = false;
			}
			if (auto other = subjectNode()) {
				renderer.pushDrawThrough(viewSlot, groups);
				other->draw();
				renderer.popDrawThrough();
			}
			renderer.pushDrawThrough(-1, groups);
			pushedGroups = true;
		}

		void View::afterNodeDraw() {
			if (!ownerIsAlive()) {
				return;
			}
			auto& renderer = owner()->renderer();
			if (pushedGroups) {
				renderer.popDrawThrough();
				pushedGroups = false;
			}
			if (scissored && renderer.device()) {
				const auto pixels = renderer.targetPixelSize();
				renderer.device()->setScissor(Render::Rect{ 0, 0, pixels.width, pixels.height });
			}
			scissored = false;
		}

	}
}
