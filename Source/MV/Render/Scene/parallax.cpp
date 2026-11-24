#include "parallax.h"
#include "view.h"
#include <cmath>

#include <jaiscript/core/dynamic_binder.hpp>

namespace MV {
	namespace Scene {
		void Parallax::jai_auto_bind(jai::dynamic_binder<Parallax>& a_builder) {
			a_builder.method("translateRatio", static_cast<Point<>(Parallax::*)() const>(&Parallax::translateRatio));
			a_builder.method("translateRatio", static_cast<std::shared_ptr<Parallax>(Parallax::*)(const Point<>&)>(&Parallax::translateRatio), jai::serial);
			a_builder.method("localOffset", static_cast<Point<>(Parallax::*)() const>(&Parallax::localOffset));
			a_builder.method("localOffset", static_cast<std::shared_ptr<Parallax>(Parallax::*)(const Point<>&)>(&Parallax::localOffset), jai::serial);
			a_builder.method("zoomRatio", static_cast<Point<>(Parallax::*)() const>(&Parallax::zoomRatio));
			a_builder.method("zoomRatio", static_cast<std::shared_ptr<Parallax>(Parallax::*)(const Point<>&)>(&Parallax::zoomRatio), jai::serial);
			a_builder.method("restCenter", &Parallax::restCenter);
			a_builder.method("restZoom", &Parallax::restZoom);
			a_builder.method("restView", &Parallax::restView, jai::serial);
			a_builder.method("restViewFrom", &Parallax::restViewFrom, jai::serial);
			a_builder.method("placement", &Parallax::placement);

			a_builder.method("enabled", static_cast<bool(Parallax::*)() const>(&Parallax::enabled));
			a_builder.method("enabled", static_cast<std::shared_ptr<Parallax>(Parallax::*)(bool)>(&Parallax::enabled), jai::serial);
			a_builder.method("enable", &Parallax::enable, jai::serial);
			a_builder.method("disable", &Parallax::disable, jai::serial);
		}

		Parallax::Parallax(const std::weak_ptr<Node>& a_owner) :
			jai::property_owner<Parallax, Component>(a_owner) {
		}

		std::shared_ptr<Parallax> Parallax::translateRatio(const Point<>& a_ratio) {
			ourTranslateRatio = a_ratio;
			return std::static_pointer_cast<Parallax>(shared_from_this());
		}

		std::shared_ptr<Parallax> Parallax::localOffset(const Point<>& a_localOffset) {
			ourLocalOffset = a_localOffset;
			resting = false;
			return std::static_pointer_cast<Parallax>(shared_from_this());
		}

		std::shared_ptr<Parallax> Parallax::zoomRatio(const Point<>& a_zoomRatio) {
			ourZoomRatio = a_zoomRatio;
			return std::static_pointer_cast<Parallax>(shared_from_this());
		}

		std::shared_ptr<Parallax> Parallax::restView(const Point<>& a_center, PointPrecision a_zoom) {
			ourRestCenter = Point<>(a_center.x, a_center.y, 0.0f);
			ourRestZoom = std::max(a_zoom, 0.0001f);
			return std::static_pointer_cast<Parallax>(shared_from_this());
		}

		std::shared_ptr<Parallax> Parallax::restViewFrom(const std::shared_ptr<Camera>& a_camera) {
			if (a_camera && ownerIsAlive()) {
				if (auto parent = owner()->parent()) {
					restView(inverse(parent->worldTransform()) * a_camera->center(), magnification(a_camera));
				}
			}
			return std::static_pointer_cast<Parallax>(shared_from_this());
		}

		Scale Parallax::growth(PointPrecision a_zoom) const {
			const auto relative = std::max(a_zoom, 0.0001f) / std::max(*ourRestZoom, 0.0001f);
			return Scale(std::pow(relative, ourZoomRatio->x - 1.0f), std::pow(relative, ourZoomRatio->y - 1.0f), 1.0f);
		}

		Point<> Parallax::placement(const Point<>& a_center, PointPrecision a_zoom) const {
			const auto grown = growth(a_zoom);
			const auto& rest = *ourLocalOffset;
			const auto& ratio = *ourTranslateRatio;
			const auto& restCenter = *ourRestCenter;
			// what the layer does not follow of the camera's move, then its growth about the camera's centre
			return Point<>(
				a_center.x + grown.x * (rest.x + (1.0f - ratio.x) * (a_center.x - restCenter.x) - a_center.x),
				a_center.y + grown.y * (rest.y + (1.0f - ratio.y) * (a_center.y - restCenter.y) - a_center.y),
				rest.z);
		}

		std::shared_ptr<Parallax> Parallax::enabled(bool a_enabled) {
			if (!a_enabled && isEnabled) {
				rest();
			}
			isEnabled = a_enabled;
			resting = false;
			return std::static_pointer_cast<Parallax>(shared_from_this());
		}

		std::shared_ptr<Parallax> Parallax::authoring(bool a_authoring) {
			if (isAuthoring != a_authoring) {
				rest();
				isAuthoring = a_authoring;
				previewCamera.reset();
				resting = false;
			}
			return std::static_pointer_cast<Parallax>(shared_from_this());
		}

		std::shared_ptr<Parallax> Parallax::authoringPreview(const std::shared_ptr<Camera>& a_camera) {
			if (isAuthoring && previewCamera.lock() != a_camera) {
				rest();
				previewCamera = a_camera;
				resting = false;
			}
			return std::static_pointer_cast<Parallax>(shared_from_this());
		}

		std::shared_ptr<Camera> Parallax::placingCamera() const {
			if (isAuthoring) {
				return previewCamera.lock();
			}
			for (auto node = owner(); node; node = node->parent()) {
				if (auto view = node->component<View>(false, false); view && !view->authoring()) {
					return view->camera();
				}
			}
			return nullptr;
		}

		PointPrecision Parallax::magnification(const std::shared_ptr<Camera>& a_camera) const {
			auto parentScene = owner()->parent()->worldTransform();
			const auto parentScale = std::sqrt(parentScene(0, 0) * parentScene(0, 0) + parentScene(0, 1) * parentScene(0, 1) + parentScene(0, 2) * parentScene(0, 2));
			return a_camera->zoom() * parentScale;
		}

		void Parallax::rest() {
			if (!ownerIsAlive()) {
				return;
			}
			if (!(owner()->position() == *ourLocalOffset)) {
				owner()->position(*ourLocalOffset);
			}
			if (authoredScaleKnown) {
				owner()->scale(authoredScale);
				authoredScaleKnown = false;
			}
			placed = false;
			resting = true;
		}

		void Parallax::place() {
			if (!isEnabled || !ownerIsAlive()) {
				return;
			}
			auto parent = owner()->parent();
			if (!parent) {
				return;
			}
			auto eye = placingCamera();
			if (!eye) {
				if (!resting) {
					rest();
				} else if (isAuthoring && !(owner()->position() == *ourLocalOffset)) {
					ourLocalOffset = owner()->position();
				}
				return;
			}
			if (isAuthoring && placed && !(owner()->position() == placedPosition)) {
				// moved in the editor while placed: what that move is at rest
				const auto moved = owner()->position() - placedPosition;
				ourLocalOffset = *ourLocalOffset + Point<>(moved.x / placedGrowth.x, moved.y / placedGrowth.y, moved.z);
			}
			auto center = inverse(parent->worldTransform()) * eye->center();
			const auto zoom = magnification(eye);
			const auto position = placement(center, zoom);
			const auto grown = growth(zoom);
			if (!(owner()->position() == position)) {
				owner()->position(position);
			}
			if (zooms()) {
				if (!authoredScaleKnown) {
					authoredScale = owner()->scale();
					authoredScaleKnown = true;
				}
				const Scale scaled(authoredScale.x * grown.x, authoredScale.y * grown.y, authoredScale.z);
				if (!(owner()->scale() == scaled)) {
					owner()->scale(scaled);
				}
			} else if (authoredScaleKnown) {
				owner()->scale(authoredScale);
				authoredScaleKnown = false;
			}
			placed = true;
			resting = false;
			placedPosition = position;
			placedGrowth = grown;
		}

		void Parallax::updateImplementation(double) {
			place();
		}

		void Parallax::beforeNodeDraw() {
			// drawn again through another View, it stays where its own View's camera put it
			if (ownerIsAlive() && !owner()->renderer().drawingThrough()) {
				place();
			}
		}

		void Parallax::detachImplementation() {
			rest();
		}

	}
}
