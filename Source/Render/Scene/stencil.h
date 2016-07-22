#ifndef _MV_SCENE_STENCIL_H_
#define _MV_SCENE_STENCIL_H_

#include "sprite.h"
#include "Interface/mouse.h"

namespace MV {
	namespace Scene {

		class Stencil : public Sprite {
			friend Node;
			friend cereal::access;

		public:
			DrawableDerivedAccessors(Stencil)

			virtual void endDraw() override;

			std::shared_ptr<Stencil> size(const Size<> &a_size, const Point<> &a_centerPoint);
			std::shared_ptr<Stencil> size(const Size<> &a_size, bool a_center = false);
			template<typename PointAssign>
			std::shared_ptr<PointAssign> corners(const PointAssign &a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomLeft, const PointAssign & a_BottomRight) {
				dirtyTexture = true;
				return std::static_pointer_cast<Stencil>(Sprite::corners(a_TopLeft, a_TopRight, a_BottomLeft, a_BottomRight));
			}

			std::shared_ptr<Stencil> clearCaptureBounds();

			std::shared_ptr<Stencil> captureBounds(const BoxAABB<> &a_newCapturedBounds);
			BoxAABB<> captureBounds();

			std::shared_ptr<Stencil> clearCaptureOffset();

			std::shared_ptr<Stencil> captureOffset(const Point<> &a_newCapturedOffset);
			Point<> captureOffset() const;

			std::shared_ptr<Stencil> captureSize(const Size<> &a_size, const Point<> &a_centerPoint);
			std::shared_ptr<Stencil> captureSize(const Size<> &a_size, bool a_center = false);

			std::shared_ptr<Stencil> refreshShader(const std::string &a_refreshShaderId);
			std::string refreshShader();
		protected:
			Stencil(const std::weak_ptr<Node> &a_owner);

			void drawStencil();

			virtual void defaultDrawImplementation() override;

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const /*version*/) {
				archive(
					CEREAL_NVP(capturedBounds),
					CEREAL_NVP(capturedOffset),
					cereal::make_nvp("Sprite", cereal::base_class<Sprite>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Stencil> &construct, std::uint32_t const /*version*/) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("refreshShaderId", construct->refreshShaderId),
					cereal::make_nvp("capturedBounds", construct->capturedBounds),
					cereal::make_nvp("capturedOffset", construct->capturedOffset),
					cereal::make_nvp("Sprite", cereal::base_class<Sprite>(construct.ptr()))
				);
				construct->initialize();
			}

			virtual void initialize() override;

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Stencil>().self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

			virtual void onOwnerDestroyed() {
				Sprite::onOwnerDestroyed();
				onLeftMouseDownHandle.reset();
				onLeftMouseUpHandle.reset();
			}
		private:
 			virtual bool preDraw() override;
 			virtual bool postDraw() override;

			MouseState::SignalType onLeftMouseDownHandle;
			MouseState::SignalType onLeftMouseUpHandle;

			std::string refreshShaderId;
			BoxAABB<> capturedBounds;
			Point<> capturedOffset;

			static int totalStencilDepth;
		};
	}
}

#endif
