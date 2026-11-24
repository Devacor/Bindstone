#include "camera.h"
#include <algorithm>
#include <cmath>
#include <jaiscript/core/dynamic_binder.hpp>

namespace MV {
	namespace Scene {

		void Camera::jai_auto_bind(jai::dynamic_binder<Camera>& a_builder) {
			a_builder.method("zoom", static_cast<PointPrecision(Camera::*)() const>(&Camera::zoom));
			a_builder.method("zoom", static_cast<std::shared_ptr<Camera>(Camera::*)(PointPrecision)>(&Camera::zoom), jai::serial);
			a_builder.method("fieldOfView", static_cast<PointPrecision(Camera::*)() const>(&Camera::fieldOfView));
			a_builder.method("fieldOfView", static_cast<std::shared_ptr<Camera>(Camera::*)(PointPrecision)>(&Camera::fieldOfView), jai::serial);
			a_builder.method("sees", static_cast<uint32_t(Camera::*)() const>(&Camera::sees));
			a_builder.method("sees", static_cast<std::shared_ptr<Camera>(Camera::*)(uint32_t)>(&Camera::sees), jai::serial);
			a_builder.method("perspective", [](Camera& a_self) { return a_self.projection() == Projection::PERSPECTIVE; });
		}

		Camera::Camera(const std::weak_ptr<Node>& a_owner) :
			jai::property_owner<Camera, Component>(a_owner) {
		}

		std::shared_ptr<Camera> Camera::projection(Projection a_projection) {
			projectionKind = a_projection;
			return std::static_pointer_cast<Camera>(shared_from_this());
		}

		std::shared_ptr<Camera> Camera::units(Units a_units) {
			unitKind = a_units;
			return std::static_pointer_cast<Camera>(shared_from_this());
		}

		std::shared_ptr<Camera> Camera::origin(Origin a_origin) {
			originKind = a_origin;
			return std::static_pointer_cast<Camera>(shared_from_this());
		}

		std::shared_ptr<Camera> Camera::width(PointPrecision a_width) {
			referenceWidth = std::max(a_width, 0.001f);
			return std::static_pointer_cast<Camera>(shared_from_this());
		}

		std::shared_ptr<Camera> Camera::match(PointPrecision a_match) {
			widthHeightMatch = std::clamp(a_match, 0.0f, 1.0f);
			return std::static_pointer_cast<Camera>(shared_from_this());
		}

		std::shared_ptr<Camera> Camera::userScaled(bool a_userScaled) {
			followsUserScale = a_userScaled;
			return std::static_pointer_cast<Camera>(shared_from_this());
		}

		std::shared_ptr<Camera> Camera::height(PointPrecision a_height) {
			fixedHeight = std::max(a_height, 0.001f);
			return std::static_pointer_cast<Camera>(shared_from_this());
		}

		std::shared_ptr<Camera> Camera::zoom(PointPrecision a_zoom) {
			zoomLevel = std::max(a_zoom, 0.0001f);
			return std::static_pointer_cast<Camera>(shared_from_this());
		}

		std::shared_ptr<Camera> Camera::fieldOfView(PointPrecision a_degrees) {
			verticalFieldOfView = std::clamp(a_degrees, 1.0f, 179.0f);
			return std::static_pointer_cast<Camera>(shared_from_this());
		}

		std::shared_ptr<Camera> Camera::planes(PointPrecision a_near, PointPrecision a_far) {
			nearDistance = a_near;
			farDistance = a_far;
			return std::static_pointer_cast<Camera>(shared_from_this());
		}

		std::shared_ptr<Camera> Camera::sees(uint32_t a_groups) {
			groupMask = a_groups;
			return std::static_pointer_cast<Camera>(shared_from_this());
		}

		TransformMatrix Camera::projectionFor(const Size<>& a_viewSize, PointPrecision a_contentScale, PointPrecision a_userScale) const {
			TransformMatrix result;
			const auto width = std::max(a_viewSize.width, 1.0f);
			const auto height = std::max(a_viewSize.height, 1.0f);
			if (projectionKind.get() == Projection::PERSPECTIVE) {
				const auto nearPlaneDistance = std::max(*nearDistance, 0.001f);
				result.makePerspective(*verticalFieldOfView / std::max(*zoomLevel, 0.0001f), width / height, nearPlaneDistance, std::max(*farDistance, nearPlaneDistance + 0.001f));
				return result;
			}
			// how many window units one of this camera's units covers
			PointPrecision unitSize = 1.0f;
			if (unitKind.get() == Units::SCALED_PIXELS) {
				unitSize = std::max(a_contentScale, 0.0001f);
			} else if (unitKind.get() == Units::REFERENCE) {
				// blended in log space, so a view twice as wide and half as tall comes out at 1 for a match of 0.5
				const auto widthFit = std::log2(width / *referenceWidth);
				const auto heightFit = std::log2(height / *fixedHeight);
				unitSize = std::exp2(widthFit + (heightFit - widthFit) * *widthHeightMatch);
			}
			if (*followsUserScale) {
				unitSize *= std::max(a_userScale, 0.0001f);
			}
			const PointPrecision shown = unitSize * *zoomLevel;
			const Size<> seen(width / shown, height / shown);
			if (originKind.get() == Origin::CENTER) {
				result.makeOrtho(-seen.width * 0.5f, seen.width * 0.5f, seen.height * 0.5f, -seen.height * 0.5f, *nearDistance, *farDistance);
			} else {
				result.makeOrtho(0.0f, seen.width, seen.height, 0.0f, *nearDistance, *farDistance);
			}
			return result;
		}

		TransformMatrix Camera::eye() {
			if (!ownerIsAlive()) {
				return TransformMatrix();
			}
			PointPrecision determinant = 0.0f;
			auto result = inverse(owner()->worldTransform(), determinant);
			return MV::equals(determinant, 0.0f) ? TransformMatrix() : result;
		}

	}
}
