#ifndef _MV_SCENE_SPRITE_H_
#define _MV_SCENE_SPRITE_H_

#include "drawable.h"

#define SpriteDerivedAccessors(ComponentType) \
	DrawableDerivedAccessors(ComponentType) \
	std::shared_ptr<ComponentType> size(const MV::Size<> &a_size, const MV::Point<> &a_centerPoint) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Sprite::size(a_size, a_centerPoint)); \
	} \
	std::shared_ptr<ComponentType> size(const MV::Size<> &a_size, bool a_center = false) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Sprite::size(a_size, a_center)); \
	} \
	template<typename PointAssign> \
	std::shared_ptr<PointAssign> corners(const PointAssign &a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomLeft, const PointAssign & a_BottomRight){ \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Sprite::corners(a_TopLeft, a_TopRight, a_BottomLeft, a_BottomRight)); \
	}

namespace MV {
	namespace Scene {

		class Sprite : public Drawable {
			friend Node;
			friend cereal::access;

		public:
			DrawableDerivedAccessors(Sprite)

			std::shared_ptr<Sprite> size(const Size<> &a_size, const Point<> &a_centerPoint) {
				std::lock_guard<std::recursive_mutex> guard(lock);
				Point<> topLeft;
				Point<> bottomRight = toPoint(a_size);

				topLeft -= a_centerPoint;
				bottomRight -= a_centerPoint;

				return bounds({ topLeft, bottomRight });
			}

			std::shared_ptr<Sprite> size(const Size<> &a_size, bool a_center) {
				return size(a_size, (a_center) ? MV::point(a_size.width / 2.0f, a_size.height / 2.0f) : MV::point(0.0f, 0.0f));
			}

			std::shared_ptr<Sprite> size(const Size<> &a_size) {
				return size(a_size, MV::point(0.0f, 0.0f));
			}

			template<typename PointAssign>
			std::shared_ptr<Sprite> corners(const PointAssign &a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomLeft, const PointAssign & a_BottomRight);

			static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
				a_script.add(chaiscript::user_type<Sprite>(), "Sprite");
				a_script.add(chaiscript::base_class<Drawable, Sprite>());
				a_script.add(chaiscript::base_class<Component, Sprite>());

				a_script.add(chaiscript::fun([](Node &a_self) { 
					return a_self.attach<Sprite>(); 
				}), "attachSprite");

				a_script.add(chaiscript::fun([](Node &a_self) {
					return a_self.componentInChildren<Sprite>();
				}), "spriteComponent");

				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Sprite>(Sprite::*)(const BoxAABB<> &)>(&Sprite::bounds)), "bounds");

				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Sprite>(Sprite::*)(const Size<> &, const Point<> &)>(&Sprite::size)), "size");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Sprite>(Sprite::*)(const Size<> &, bool)>(&Sprite::size)), "size");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Sprite>(Sprite::*)(const Size<> &)>(&Sprite::size)), "size");

				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Sprite>(Sprite::*)(const Point<> &, const Point<> &, const Point<> &, const Point<> &)>(&Sprite::corners<Point<>>)), "corners");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Sprite>(Sprite::*)(const Color &, const Color &, const Color &, const Color &)>(&Sprite::corners<Color>)), "corners");

				a_script.add(chaiscript::type_conversion<SafeComponent<Sprite>, std::shared_ptr<Sprite>>([](const SafeComponent<Sprite> &a_item) { return a_item.self(); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Sprite>, std::shared_ptr<Drawable>>([](const SafeComponent<Sprite> &a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Sprite>, std::shared_ptr<Component>>([](const SafeComponent<Sprite> &a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));
				
				return a_script;
			}
		protected:
			Sprite(const std::weak_ptr<Node> &a_owner) :
				Drawable(a_owner) {

				points.resize(4);
				clearTexturePoints(points);
				appendQuadVertexIndices(vertexIndices, 0);
			}

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const /*version*/) {
				archive(
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Sprite> &construct, std::uint32_t const /*version*/) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(construct.ptr()))
				);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Sprite>().self());
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
