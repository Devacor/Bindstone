#ifndef _MV_SCENE_SPRITE_H_
#define _MV_SCENE_SPRITE_H_

#include "drawable.h"

#define SpriteDerivedAccessors(ComponentType) \
	DrawableDerivedAccessors(ComponentType) \
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

				a_script.add(chaiscript::fun([](const std::shared_ptr<Sprite> &a_self, uint16_t a_subdivisions) {
					return a_self->subdivide(a_subdivisions);
				}), "subdivide");
				a_script.add(chaiscript::fun([](const std::shared_ptr<Sprite> &a_self) {
					return a_self->subdivisions();
				}), "subdivisions");

				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Sprite>(Sprite::*)(const Point<> &, const Point<> &, const Point<> &, const Point<> &)>(&Sprite::corners<Point<>>)), "corners");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Sprite>(Sprite::*)(const Color &, const Color &, const Color &, const Color &)>(&Sprite::corners<Color>)), "corners");

				a_script.add(chaiscript::type_conversion<SafeComponent<Sprite>, std::shared_ptr<Sprite>>([](const SafeComponent<Sprite> &a_item) { return a_item.self(); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Sprite>, std::shared_ptr<Drawable>>([](const SafeComponent<Sprite> &a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Sprite>, std::shared_ptr<Component>>([](const SafeComponent<Sprite> &a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));
				
				return a_script;
			}

			std::shared_ptr<Sprite> subdivide(uint16_t a_subdivisions) {
				if (ourSubdivisions != a_subdivisions) {
					ourSubdivisions = a_subdivisions;
					updateSubdivision();
				}
				return std::static_pointer_cast<Sprite>(shared_from_this());
			}

			uint16_t subdivisions() const {
				return ourSubdivisions;
			}

			bool hasSlice(){
				auto found = ourTextures.find(0);
				return found != ourTextures.end() && found->second->hasSlice();
			}
		protected:
			Sprite(const std::weak_ptr<Node> &a_owner) :
				Drawable(a_owner) {

				clearTexturePoints(points);
				appendQuadVertexIndices(vertexIndices, 0);
			}

			template <class Archive>
			void save(Archive & archive, std::uint32_t const version) const {
				if (version > 0) {
					archive(cereal::make_nvp("subdivisions", ourSubdivisions));
				}
				archive(
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				if (version > 0) {
					archive(cereal::make_nvp("subdivisions", ourSubdivisions));
				}
				archive(
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Sprite> &construct, std::uint32_t const version) {
				construct(std::shared_ptr<Node>());
				construct->load(archive, version);
				construct->initialize();
			}

			void updateSliceColorsFromCorners();

			void updateSubdivision();
			void updateSubdivisionTexture();

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Sprite>().self());
			}

			void updateSlice();

			void clearSlice();

			virtual void refreshBounds() override;
		private:
			void clearTextureCoordinates(size_t a_textureId) override {
				if (a_textureId == 0) {
					clearSlice();

					clearTexturePoints(points);
					notifyParentOfComponentChange();
				}
			}

			void updateTextureCoordinates(size_t a_textureId) override;

			uint16_t ourSubdivisions = 0;
		};

		template<typename PointAssign>
		std::shared_ptr<Sprite> Sprite::corners(const PointAssign & a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomRight, const PointAssign & a_BottomLeft) {
			auto self = std::static_pointer_cast<Sprite>(shared_from_this());
			points[0] = a_TopLeft;
			points[1] = a_BottomLeft;
			points[2] = a_BottomRight;
			points[3] = a_TopRight;
			updateSubdivision();
			updateSliceColorsFromCorners();
			refreshBounds();
			return self;
		}
	}
}

#endif
