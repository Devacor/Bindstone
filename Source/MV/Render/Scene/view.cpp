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

		std::shared_ptr<View> View::lookThrough(const std::shared_ptr<Camera>& a_camera) {
			cameraOverride = a_camera;
			refresh();
			return std::static_pointer_cast<View>(shared_from_this());
		}

		std::shared_ptr<View> View::show(const std::shared_ptr<Node>& a_subject) {
			subjectOverride = a_subject;
			return std::static_pointer_cast<View>(shared_from_this());
		}

		std::shared_ptr<Camera> View::camera() const {
			if (!ownerIsAlive()) {
				return nullptr;
			}
			if (auto overridden = cameraOverride.lock()) {
				return overridden;
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
			// the viewport's part of the target, as fractions of the whole window
			const auto pixels = renderer.targetPixelSize();
			const auto target = renderer.target();
			const float windowWidth = static_cast<float>(std::max(pixels.width, 1));
			const float windowHeight = static_cast<float>(std::max(pixels.height, 1));
			const float targetLeft = static_cast<float>(target.minPoint.x) / windowWidth;
			const float targetTop = static_cast<float>(target.minPoint.y) / windowHeight;
			const float targetWidth = static_cast<float>(target.width()) / windowWidth;
			const float targetHeight = static_cast<float>(target.height()) / windowHeight;
			const auto viewportLeft = std::clamp(viewportBox->minPoint.x, 0.0f, 1.0f);
			const auto viewportTop = std::clamp(viewportBox->minPoint.y, 0.0f, 1.0f);
			const auto left = targetLeft + viewportLeft * targetWidth;
			const auto top = targetTop + viewportTop * targetHeight;
			const auto width = std::max((std::clamp(viewportBox->maxPoint.x, 0.0f, 1.0f) - viewportLeft) * targetWidth, 0.0001f);
			const auto height = std::max((std::clamp(viewportBox->maxPoint.y, 0.0f, 1.0f) - viewportTop) * targetHeight, 0.0001f);
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
			const auto target = owner()->renderer().target();
			const float width = static_cast<float>(target.width());
			const float height = static_cast<float>(target.height());
			return BoxAABB<int>(
				Point<int>(target.minPoint.x + static_cast<int>(std::round(viewportBox->minPoint.x * width)), target.minPoint.y + static_cast<int>(std::round(viewportBox->minPoint.y * height))),
				Point<int>(target.minPoint.x + static_cast<int>(std::round(viewportBox->maxPoint.x * width)), target.minPoint.y + static_cast<int>(std::round(viewportBox->maxPoint.y * height))));
		}

		std::shared_ptr<View> View::authoring(bool a_authoring) {
			if (isAuthoring != a_authoring) {
				isAuthoring = a_authoring;
				if (isAuthoring) {
					stepAside();
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

		// Authoring keeps the slot reserved, so an editor showing the game can turn the View on and off each frame.
		void View::stepAside() {
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
			if (!ownerIsAlive()) {
				return nullptr;
			}
			auto found = subjectOverride.lock();
			if (!found) {
				if (subjectId->empty()) {
					return nullptr;
				}
				auto top = owner();
				while (top->parent()) {
					top = top->parent();
				}
				found = top->id() == *subjectId ? top : top->get(subjectId, false);
			}
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
				lazyInitializeShader();
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
