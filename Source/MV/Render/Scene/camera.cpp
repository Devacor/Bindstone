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
			a_builder.method("center", &Camera::center);
			a_builder.method("sceneFromView", &Camera::sceneFromView);
			a_builder.method("sceneTranslate", &Camera::sceneTranslate, jai::serial);
			a_builder.method("zoomAround", &Camera::zoomAround, jai::serial);
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

		Size<> Camera::orthographicSeen(const Size<>& a_viewSize, PointPrecision a_contentScale, PointPrecision a_userScale) const {
			const auto width = a_viewSize.width;
			const auto height = a_viewSize.height;
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
			return Size<>(width / shown, height / shown);
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
			const auto seen = orthographicSeen(Size<>(width, height), a_contentScale, a_userScale);
			if (originKind.get() == Origin::CENTER) {
				result.makeOrtho(-seen.width * 0.5f, seen.width * 0.5f, seen.height * 0.5f, -seen.height * 0.5f, *nearDistance, *farDistance);
			} else {
				result.makeOrtho(0.0f, seen.width, seen.height, 0.0f, *nearDistance, *farDistance);
			}
			return result;
		}

		TransformMatrix Camera::lens() {
			if (!ownerIsAlive()) {
				return TransformMatrix();
			}
			auto result = owner()->worldTransform();
			for (size_t column = 0; column < 3; ++column) {
				const auto length = std::sqrt(result(column, 0) * result(column, 0) + result(column, 1) * result(column, 1) + result(column, 2) * result(column, 2));
				if (length > 0.0f) {
					for (size_t row = 0; row < 3; ++row) {
						result.access(column, row) /= length;
					}
				}
			}
			return result;
		}

		TransformMatrix Camera::eye() {
			PointPrecision determinant = 0.0f;
			auto result = inverse(lens(), determinant);
			return MV::equals(determinant, 0.0f) ? TransformMatrix() : result;
		}

		Size<> Camera::viewSize() const {
			if (viewedSize.width > 0.0f && viewedSize.height > 0.0f) {
				return viewedSize;
			}
			return ownerIsAlive() ? owner()->renderer().world().size() : Size<>(1.0f, 1.0f);
		}

		std::shared_ptr<Camera> Camera::viewSize(const Size<>& a_size, PointPrecision a_contentScale, PointPrecision a_userScale) {
			viewedSize = a_size;
			viewedContentScale = a_contentScale;
			viewedUserScale = a_userScale;
			return std::static_pointer_cast<Camera>(shared_from_this());
		}

		Size<> Camera::seen() const {
			const auto view = viewSize();
			const auto width = std::max(view.width, 1.0f);
			const auto height = std::max(view.height, 1.0f);
			if (projectionKind.get() == Projection::PERSPECTIVE) {
				const auto distance = ownerIsAlive() ? std::abs(owner()->worldTransform().position().z) : 0.0f;
				const auto seenHeight = 2.0f * distance * std::tan(toRadians(*verticalFieldOfView / std::max(*zoomLevel, 0.0001f)) * 0.5f);
				return Size<>(seenHeight * width / height, seenHeight);
			}
			return orthographicSeen(Size<>(width, height), viewedContentScale, viewedUserScale);
		}

		Point<> Camera::sceneFromView(const Point<>& a_fraction) {
			const auto size = seen();
			Point<> local(a_fraction.x * size.width, a_fraction.y * size.height, 0.0f);
			if (projectionKind.get() == Projection::PERSPECTIVE || originKind.get() == Origin::CENTER) {
				local -= Point<>(size.width * 0.5f, size.height * 0.5f, 0.0f);
			}
			auto result = lens() * local;
			if (projectionKind.get() == Projection::PERSPECTIVE) {
				result.z = 0.0f;
			}
			return result;
		}

		Point<> Camera::center() {
			return sceneFromView(Point<>(0.5f, 0.5f));
		}

		BoxAABB<> Camera::visibleIn(const std::shared_ptr<Node>& a_node) {
			const auto fromScene = inverse(a_node->worldTransform());
			BoxAABB<> result;
			bool first = true;
			for (auto&& fraction : { Point<>(0.0f, 0.0f), Point<>(1.0f, 0.0f), Point<>(1.0f, 1.0f), Point<>(0.0f, 1.0f) }) {
				auto local = fromScene * sceneFromView(fraction);
				local.z = 0.0f;
				if (first) {
					result = BoxAABB<>(local, local);
					first = false;
				} else {
					result.expandWith(local);
				}
			}
			return result;
		}

		std::shared_ptr<Camera> Camera::sceneTranslate(const Point<>& a_delta) {
			if (ownerIsAlive()) {
				if (auto parent = owner()->parent()) {
					const auto fromScene = inverse(parent->worldTransform());
					owner()->translate(fromScene * a_delta - fromScene * Point<>());
				} else {
					owner()->translate(a_delta);
				}
			}
			return std::static_pointer_cast<Camera>(shared_from_this());
		}

		std::shared_ptr<Camera> Camera::zoomAround(PointPrecision a_zoom, const Point<>& a_fraction) {
			const auto before = sceneFromView(a_fraction);
			zoom(a_zoom);
			return sceneTranslate(before - sceneFromView(a_fraction));
		}

	}
}
