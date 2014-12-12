#ifndef _MV_SCENE_SPRITE_H_
#define _MV_SCENE_SPRITE_H_

#include "drawable.h"

#define SpriteDerivedAccessors(ComponentType) \
	DrawableDerivedAccessors(ComponentType) \
	std::shared_ptr<ComponentType> bounds(const BoxAABB<> &a_bounds) { \
		return std::static_pointer_cast<ComponentType>(Sprite::bounds(a_bounds)); \
	} \
	BoxAABB<> bounds() { \
		return boundsImplementation(); \
	} \
	std::shared_ptr<ComponentType> size(const Size<> &a_size, const Point<> &a_centerPoint) { \
		return std::static_pointer_cast<ComponentType>(Sprite::size(a_size, a_centerPoint)); \
	} \
	std::shared_ptr<ComponentType> size(const Size<> &a_size, bool a_center = false) { \
		return std::static_pointer_cast<ComponentType>(Sprite::size(a_size, a_center)); \
	} \
	template<typename PointAssign> \
	std::shared_ptr<PointAssign> corners(const PointAssign &a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomLeft, const PointAssign & a_BottomRight){ \
		return std::static_pointer_cast<ComponentType>(Sprite::corners(a_TopLeft, a_TopRight, a_BottomLeft, a_BottomRight)); \
	}

namespace MV {
	namespace Scene {
		class Sprite : public Drawable {
			friend Node;
			friend cereal::access;

		public:
			DrawableDerivedAccessors(Sprite)


			BoxAABB<> bounds() {
				return boundsImplementation();
			}

			std::shared_ptr<Sprite> bounds(const BoxAABB<> &a_bounds) {
				std::lock_guard<std::recursive_mutex> guard(lock);
				auto self = std::static_pointer_cast<Sprite>(shared_from_this());

				points[0] = a_bounds.minPoint;
				points[1].x = a_bounds.minPoint.x;	points[1].y = a_bounds.maxPoint.y;	points[1].z = (a_bounds.maxPoint.z + a_bounds.minPoint.z) / 2.0f;
				points[2] = a_bounds.maxPoint;
				points[3].x = a_bounds.maxPoint.x;	points[3].y = a_bounds.minPoint.y;	points[3].z = points[1].z;

				refreshBounds();

				return self;
			}

			std::shared_ptr<Sprite> size(const Size<> &a_size, const Point<> &a_centerPoint) {
				std::lock_guard<std::recursive_mutex> guard(lock);
				Point<> topLeft;
				Point<> bottomRight = toPoint(a_size);

				topLeft -= a_centerPoint;
				bottomRight -= a_centerPoint;

				return bounds({ topLeft, bottomRight });
			}

			std::shared_ptr<Sprite> size(const Size<> &a_size, bool a_center = false) {
				return size(a_size, (a_center) ? point(a_size.width / 2.0f, a_size.height / 2.0f) : point(0.0f, 0.0f));
			}

			template<typename PointAssign>
			std::shared_ptr<Sprite> corners(const PointAssign &a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomLeft, const PointAssign & a_BottomRight);

		protected:
			Sprite(const std::weak_ptr<Node> &a_owner) :
				Drawable(a_owner) {

				points.resize(4);
				clearTexturePoints(points);
				appendQuadVertexIndices(vertexIndices, 0);
			}


			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Sprite> &construct) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(construct.ptr()))
				);
				construct->initialize();
			}

		private:
			virtual void clearTextureCoordinates() {
				clearTexturePoints(points);
				notifyParentOfComponentChange();
			}

			virtual void updateTextureCoordinates() {
				if (ourTexture != nullptr) {
					ourTexture->apply(points);
					notifyParentOfComponentChange();
				} else {
					clearTextureCoordinates();
				}
			}
		};

		template<typename PointAssign>
		std::shared_ptr<Sprite> Sprite::corners(const PointAssign & a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomRight, const PointAssign & a_BottomLeft) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto self = std::static_pointer_cast<Sprite>(shared_from_this());
			points[0] = a_TopLeft;
			points[1] = a_BottomLeft;
			points[2] = a_BottomRight;
			points[3] = a_TopRight;
			refreshBounds();
			return self;
		}
	}
}
#endif
