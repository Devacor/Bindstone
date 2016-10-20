#ifndef _MV_SCENE_CLIPPED_H_
#define _MV_SCENE_CLIPPED_H_

#include "sprite.h"
#include "Interface/mouse.h"

namespace MV {
	namespace Scene {

		class Clipped : public Sprite {
			friend Node;
			friend cereal::access;

		public:
			DrawableDerivedAccessors(Clipped)

			std::shared_ptr<Clipped> size(const Size<> &a_size, const Point<> &a_centerPoint);
			std::shared_ptr<Clipped> size(const Size<> &a_size, bool a_center = false);
			template<typename PointAssign>
			std::shared_ptr<PointAssign> corners(const PointAssign &a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomLeft, const PointAssign & a_BottomRight) {
				dirtyTexture = true;
				return std::static_pointer_cast<Clipped>(Sprite::corners(a_TopLeft, a_TopRight, a_BottomLeft, a_BottomRight));
			}

			void refreshTexture(bool a_forceRefreshEvenIfNotDirty = true);

			bool refreshEveryFrame() const {
				return forceRefreshEveryFrame;
			}
			std::shared_ptr<Clipped> refreshEveryFrame(bool a_yesOrNo) {
				forceRefreshEveryFrame = a_yesOrNo;
				return std::static_pointer_cast<Clipped>(shared_from_this());
			}

			std::shared_ptr<Clipped> clearCaptureBounds();

			std::shared_ptr<Clipped> captureBounds(const BoxAABB<> &a_newCapturedBounds);
			BoxAABB<> captureBounds();

			std::shared_ptr<Clipped> clearCaptureOffset();

			std::shared_ptr<Clipped> captureOffset(const Point<> &a_newCapturedOffset);
			Point<> captureOffset() const;

			std::shared_ptr<Clipped> captureSize(const Size<> &a_size, const Point<> &a_centerPoint);
			std::shared_ptr<Clipped> captureSize(const Size<> &a_size, bool a_center = false);

			std::shared_ptr<Clipped> refreshShader(const std::string &a_refreshShaderId);
			std::string refreshShader();
		protected:
			Clipped(const std::weak_ptr<Node> &a_owner);

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const /*version*/) {
				archive(
					CEREAL_NVP(refreshShaderId),
					CEREAL_NVP(capturedBounds),
					CEREAL_NVP(capturedOffset),
					CEREAL_NVP(forceRefreshEveryFrame),
					cereal::make_nvp("Sprite", cereal::base_class<Sprite>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Clipped> &construct, std::uint32_t const /*version*/) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("refreshShaderId", construct->refreshShaderId),
					cereal::make_nvp("capturedBounds", construct->capturedBounds),
					cereal::make_nvp("capturedOffset", construct->capturedOffset),
					cereal::make_nvp("Sprite", cereal::base_class<Sprite>(construct.ptr()))
				);
				construct->initialize();
			}

			virtual BoxAABB<> boundsImplementation() override {
				return Sprite::boundsImplementation();
			}
			virtual void boundsImplementation(const BoxAABB<> &a_bounds) override;

			virtual void initialize() override;

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Clipped>().self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);
		private:
			virtual bool preDraw();
			virtual bool postDraw();

			std::shared_ptr<DynamicTextureDefinition> clippedTexture;
			std::shared_ptr<Framebuffer> framebuffer;

			std::string refreshShaderId;
			BoxAABB<> capturedBounds;
			Point<> capturedOffset;
			bool dirtyTexture = true;
			bool forceRefreshEveryFrame = false;
			Node::BasicSharedSignalType dirtyObserveSignal;
		};
	}
}

#endif
