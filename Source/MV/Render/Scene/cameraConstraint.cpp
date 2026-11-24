#include "cameraConstraint.h"
#include <algorithm>
#include <cmath>
#include <jaiscript/core/dynamic_binder.hpp>

namespace MV {
	namespace Scene {

		namespace {
			bool hasArea(const BoxAABB<>& a_bounds) {
				return a_bounds.width() > 0.0f && a_bounds.height() > 0.0f;
			}
		}

		void CameraConstraint::jai_auto_bind(jai::dynamic_binder<CameraConstraint>& a_builder) {
			a_builder.method("camera", &CameraConstraint::camera);
			a_builder.method("hold", &CameraConstraint::hold, jai::serial);
			a_builder.method("release", &CameraConstraint::release, jai::serial);
			a_builder.method("held", &CameraConstraint::held);
			a_builder.method("clamp", &CameraConstraint::clamp, jai::serial);
			a_builder.method("settle", &CameraConstraint::settle, jai::serial);
			a_builder.method("settled", &CameraConstraint::settled);
			a_builder.method("visible", &CameraConstraint::visible);
			a_builder.method("minimumZoom", &CameraConstraint::minimumZoom);
			a_builder.method("maximumZoom", &CameraConstraint::maximumZoom);
		}

		CameraConstraint::CameraConstraint(const std::weak_ptr<Node>& a_owner) :
			jai::property_owner<CameraConstraint, Component>(a_owner) {
		}

		std::shared_ptr<CameraConstraint> CameraConstraint::cameraNode(const std::string& a_id) {
			cameraNodeId = a_id;
			resolvedCamera.reset();
			return std::static_pointer_cast<CameraConstraint>(shared_from_this());
		}

		std::shared_ptr<Camera> CameraConstraint::camera() {
			auto result = resolvedCamera.lock();
			if (result && result->ownerIsAlive() && result->owner()->id() == *cameraNodeId) {
				return result;
			}
			resolvedCamera.reset();
			if (cameraNodeId->empty() || !ownerIsAlive()) {
				return nullptr;
			}
			auto top = owner();
			while (top->parent()) {
				top = top->parent();
			}
			auto node = top->id() == *cameraNodeId ? top : top->get(cameraNodeId, false);
			auto found = node ? node->component<Camera>(false, false) : SafeComponent<Camera>();
			result = found ? found.self() : nullptr;
			resolvedCamera = result;
			return result;
		}

		std::shared_ptr<CameraConstraint> CameraConstraint::hard(const BoxAABB<>& a_bounds) {
			hardBounds = a_bounds;
			return std::static_pointer_cast<CameraConstraint>(shared_from_this());
		}

		std::shared_ptr<CameraConstraint> CameraConstraint::soft(const BoxAABB<>& a_bounds) {
			softBounds = a_bounds;
			return std::static_pointer_cast<CameraConstraint>(shared_from_this());
		}

		std::shared_ptr<CameraConstraint> CameraConstraint::hardZoom(PointPrecision a_minimum, PointPrecision a_maximum) {
			hardZoomMinimum = a_minimum;
			hardZoomMaximum = a_maximum;
			return std::static_pointer_cast<CameraConstraint>(shared_from_this());
		}

		std::shared_ptr<CameraConstraint> CameraConstraint::softZoom(PointPrecision a_minimum, PointPrecision a_maximum) {
			softZoomMinimum = a_minimum;
			softZoomMaximum = a_maximum;
			return std::static_pointer_cast<CameraConstraint>(shared_from_this());
		}

		std::shared_ptr<CameraConstraint> CameraConstraint::fitsMinimumZoom(bool a_fits) {
			fitMinimum = a_fits;
			return std::static_pointer_cast<CameraConstraint>(shared_from_this());
		}

		std::shared_ptr<CameraConstraint> CameraConstraint::easing(PointPrecision a_rate) {
			easingRate = std::max(a_rate, 0.0f);
			return std::static_pointer_cast<CameraConstraint>(shared_from_this());
		}

		PointPrecision CameraConstraint::minimumZoom(bool a_soft) {
			PointPrecision result = std::max(a_soft ? *softZoomMinimum : *hardZoomMinimum, 0.0001f);
			if (fitMinimum.get() && hasArea(bounds(a_soft)) && camera()) {
				result = std::max(result, zoomToFit(bounds(a_soft)));
			}
			return result;
		}

		PointPrecision CameraConstraint::maximumZoom(bool a_soft) const {
			return a_soft ? softZoomMaximum : hardZoomMaximum;
		}

		BoxAABB<> CameraConstraint::visible() {
			auto eye = camera();
			return eye && ownerIsAlive() ? eye->visibleIn(owner()) : BoxAABB<>();
		}

		Point<> CameraConstraint::correction(const BoxAABB<>& a_bounds) {
			Point<> result;
			if (!hasArea(a_bounds) || !camera()) {
				return result;
			}
			auto shown = visible();
			auto centering = a_bounds.centerPoint() - shown.centerPoint();
			if (shown.width() >= a_bounds.width()) { result.x = centering.x; }
			else if (shown.minPoint.x < a_bounds.minPoint.x) { result.x = a_bounds.minPoint.x - shown.minPoint.x; }
			else if (shown.maxPoint.x > a_bounds.maxPoint.x) { result.x = a_bounds.maxPoint.x - shown.maxPoint.x; }
			if (shown.height() >= a_bounds.height()) { result.y = centering.y; }
			else if (shown.minPoint.y < a_bounds.minPoint.y) { result.y = a_bounds.minPoint.y - shown.minPoint.y; }
			else if (shown.maxPoint.y > a_bounds.maxPoint.y) { result.y = a_bounds.maxPoint.y - shown.maxPoint.y; }
			return result;
		}

		PointPrecision CameraConstraint::zoomToFit(const BoxAABB<>& a_bounds) {
			auto eye = camera();
			if (!eye || !hasArea(a_bounds)) {
				return 0.0f;
			}
			auto shown = visible();
			return eye->zoom() * std::max(shown.width() / a_bounds.width(), shown.height() / a_bounds.height());
		}

		std::shared_ptr<CameraConstraint> CameraConstraint::moveCamera(const Point<>& a_delta) {
			if (auto eye = camera(); eye && ownerIsAlive()) {
				auto toScene = owner()->worldTransform();
				eye->sceneTranslate(toScene * a_delta - toScene * Point<>());
			}
			return std::static_pointer_cast<CameraConstraint>(shared_from_this());
		}

		void CameraConstraint::within(bool a_soft, PointPrecision a_share, const Point<>& a_around) {
			auto eye = camera();
			if (!eye) {
				return;
			}
			const auto zoom = eye->zoom();
			const auto target = std::max(std::min(zoom, maximumZoom(a_soft)), minimumZoom(a_soft));
			if (target != zoom) {
				const auto eased = zoom * std::pow(target / zoom, a_share);
				eye->zoomAround(std::abs(eased - target) < 0.0005f ? target : eased, a_around);
			}
			auto move = correction(bounds(a_soft));
			if (move.x != 0.0f || move.y != 0.0f) {
				const bool close = std::abs(move.x) < 0.01f && std::abs(move.y) < 0.01f;
				moveCamera(close ? move : move * a_share);
			}
		}

		std::shared_ptr<CameraConstraint> CameraConstraint::hold() {
			isHeld = true;
			return std::static_pointer_cast<CameraConstraint>(shared_from_this());
		}

		std::shared_ptr<CameraConstraint> CameraConstraint::release(const Point<>& a_around) {
			isHeld = false;
			releasedAround = a_around;
			return std::static_pointer_cast<CameraConstraint>(shared_from_this());
		}

		std::shared_ptr<CameraConstraint> CameraConstraint::clamp(const Point<>& a_around) {
			within(false, 1.0f, a_around);
			return std::static_pointer_cast<CameraConstraint>(shared_from_this());
		}

		std::shared_ptr<CameraConstraint> CameraConstraint::settle() {
			within(true, 1.0f, releasedAround);
			return std::static_pointer_cast<CameraConstraint>(shared_from_this());
		}

		bool CameraConstraint::settled() {
			auto eye = camera();
			if (!eye) {
				return true;
			}
			const auto zoom = eye->zoom();
			const auto move = correction(softBounds);
			return zoom >= minimumZoom(true) - 0.0005f && zoom <= maximumZoom(true) + 0.0005f && std::abs(move.x) < 0.01f && std::abs(move.y) < 0.01f;
		}

		std::shared_ptr<CameraConstraint> CameraConstraint::authoring(bool a_authoring) {
			isAuthoring = a_authoring;
			return std::static_pointer_cast<CameraConstraint>(shared_from_this());
		}

		void CameraConstraint::ease(double a_delta) {
			const auto share = *easingRate <= 0.0f ? 1.0f : static_cast<PointPrecision>(1.0 - std::exp(-static_cast<double>(*easingRate) * a_delta));
			within(true, share, releasedAround);
		}

		void CameraConstraint::updateImplementation(double a_delta) {
			if (isAuthoring || !camera()) {
				return;
			}
			if (isHeld) {
				clamp();
			} else if (a_delta > 0.0) {
				ease(a_delta);
			}
		}

	}
}
