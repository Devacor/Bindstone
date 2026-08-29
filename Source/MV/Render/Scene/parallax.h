#ifndef _MV_SCENE_PARALLAX_H_
#define _MV_SCENE_PARALLAX_H_

#include "node.h"
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {
		class Parallax : public jai::property_owner<Parallax, Component> {
			friend Node;
			friend jai::access;

		public:
			ComponentDerivedAccessors(Parallax)

			std::shared_ptr<Parallax> translateRatio(const Point<> &a_ratio) {
				ourTranslateRatio = a_ratio;
				return std::static_pointer_cast<Parallax>(shared_from_this());
			}

			Point<> translateRatio() const {
				return ourTranslateRatio;
			}

			std::shared_ptr<Parallax> localOffset(const Point<>& a_localOffset) {
				ourLocalOffset = a_localOffset;
				return std::static_pointer_cast<Parallax>(shared_from_this());
			}

			Point<> localOffset() const {
				return ourLocalOffset;
			}

			std::shared_ptr<Parallax> worldZoomOffset(const Point<>& a_worldZoomOffset) {
				ourZoomOffset = a_worldZoomOffset;
				return std::static_pointer_cast<Parallax>(shared_from_this());
			}

			Point<> worldZoomOffset() const {
				return ourZoomOffset;
			}
			// Share of the parent's zoom this layer takes, in log space: the layer's world scale is
			// parentScale^zoomRatio, so 1 zooms with the parent (default), 0 never changes size,
			// 0.5 halves every doubling. Pair with translateRatio for a layer at depth.
			std::shared_ptr<Parallax> zoomRatio(const Point<>& a_zoomRatio) {
				ourZoomRatio = a_zoomRatio;
				needsUpdate = true;
				return std::static_pointer_cast<Parallax>(shared_from_this());
			}
			Point<> zoomRatio() const {
				return ourZoomRatio;
			}

			bool enabled() const {
				return isEnabled;
			}

			std::shared_ptr<Parallax> enabled(bool a_enabled) {
				isEnabled = a_enabled;
				if (!isEnabled) {
					owner()->position(ourLocalOffset);
				}
				return std::static_pointer_cast<Parallax>(shared_from_this());
			}

			std::shared_ptr<Parallax> disable() {
				isEnabled = false;
				owner()->position(ourLocalOffset);
				return std::static_pointer_cast<Parallax>(shared_from_this());
			}

			std::shared_ptr<Parallax> enable() {
				isEnabled = true;
				return std::static_pointer_cast<Parallax>(shared_from_this());
			}

		protected:
			Parallax(const std::weak_ptr<Node> &a_owner);

			std::map<size_t, std::shared_ptr<TextureHandle>>::const_iterator disconnectTexture(size_t a_textureId);

			template<class Archive>
			void save(Archive & archive, std::uint32_t const) const {
				property_mgr.save(archive);
				Component::save(archive, 0);
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				property_mgr.load(archive);
				Component::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Parallax>& construct) {
				construct(std::shared_ptr<Node>());
				construct->load(ar, 0);
				construct->initialize();
			}

			std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) override {
				return cloneHelper(a_parent->attach<Parallax>().self());
			}

			std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone) override;

			void initialize() override;
			void postLoadInitialize() override;
			void detachImplementation() override;
			void reattachImplementation() override;
			void updateImplementation(double a_delta) override;

			Point<> absolutePosition() const;

			JAI_NAMED_PROPERTY((bool), "enabled", isEnabled, false);
			bool needsUpdate = false;

			JAI_NAMED_PROPERTY((Point<>), "translateRatio", ourTranslateRatio);
			JAI_NAMED_PROPERTY((Point<>), "localOffset", ourLocalOffset);
			JAI_NAMED_PROPERTY((Point<>), "zoomOffset", ourZoomOffset);
			JAI_NAMED_PROPERTY((Point<>), "zoomRatio", ourZoomRatio, (Point<>(1.0f, 1.0f, 1.0f)));
			Scale authoredScale;   // the node's own scale before zoom is applied on top of it
			bool authoredScaleKnown = false;
			JAI_DELETED_PROPERTY((int32_t), space);
			Node::BasicReceiverType parentObserver;
			Draw2D::CameraRecieveType cameraObserver;
		};
	}
}

#endif
