#ifndef _MV_SCENE_STENCIL_H_
#define _MV_SCENE_STENCIL_H_

#include "sprite.h"
#include "MV/Interface/tapDevice.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {

		class Stencil : public jai::property_owner<Stencil, Sprite> {
			friend Node;
			friend jai::access;

		public:
			DrawableDerivedAccessors(Stencil)

			virtual void endDraw() override;

			template<typename PointAssign>
			std::shared_ptr<PointAssign> corners(const PointAssign &a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomLeft, const PointAssign & a_BottomRight) {
				return std::static_pointer_cast<Stencil>(Sprite::corners(a_TopLeft, a_TopRight, a_BottomLeft, a_BottomRight));
			}
		protected:
			Stencil(const std::weak_ptr<Node> &a_owner);
			Stencil(const Stencil&) = delete;
			Stencil& operator=(const Stencil&) = delete;
			Stencil(Stencil&&) = delete;
			Stencil& operator=(Stencil&&) = delete;

			void drawStencil();

			virtual void defaultDrawImplementation() override;

			template<class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				property_mgr.save(archive);
				Sprite::save(archive, 0);
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const /*version*/) {
				property_mgr.load(archive);
				Sprite::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Stencil>& construct) {
				construct(std::shared_ptr<Node>());
				construct->load(ar, 0);
				construct->initialize();
			}

			virtual void initialize() override;

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Stencil>().self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

			virtual void onOwnerDestroyed() {
				Sprite::onOwnerDestroyed();
			}
		private:
 			virtual bool preDraw() override;
 			virtual bool postDraw() override;
		};
	}
}

#endif
