#ifndef _MV_SCENE_CLIPPED_H_
#define _MV_SCENE_CLIPPED_H_

#include "sprite.h"
#include "MV/Interface/tapDevice.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {

		class Clipped : public jai::property_owner<Clipped, Sprite> {
			friend Node;
			friend jai::access;

		public:
			DrawableDerivedAccessors(Clipped)

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

			template<class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				property_mgr.save(archive);
				Sprite::save(archive, 0);
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				property_mgr.load(archive);
				Sprite::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Clipped>& construct) {
				construct(std::shared_ptr<Node>());
				construct->load(ar, 0);
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

			JAI_PROPERTY((std::string), refreshShaderId, PREMULTIPLY_ID, [](auto&, auto&){});
			JAI_PROPERTY((BoxAABB<>), capturedBounds);
			JAI_PROPERTY((Point<>), capturedOffset);
			Point<> prevLocalPoint{};
			bool dirtyTexture = true;
			JAI_PROPERTY((bool), forceRefreshEveryFrame, false);
			Node::BasicReceiverType dirtyObserveSignal;
		};
	}
}

#endif
