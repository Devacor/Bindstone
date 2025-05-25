#ifndef _MV_SCENE_PARALLAX_H_
#define _MV_SCENE_PARALLAX_H_

#include "node.h"

namespace MV {
	namespace Scene {
		class Parallax : public Component {
			friend Node;
			friend cereal::access;

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

			template <class Archive>
			void save(Archive & archive, std::uint32_t const) const {
				archive(cereal::make_nvp("Component", cereal::base_class<Component>(this)));
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				if (version <= 2) {
					std::vector<std::string> propertyKeys;
					propertyKeys.push_back("isEnabled");
					if(version <= 1){
						propertyKeys.push_back("space");
					}
					propertyKeys.push_back("translateRatio");
					propertyKeys.push_back("localOffset");
					if(version > 1){
						propertyKeys.push_back("zoomOffset");
					}
					properties.load(archive, propertyKeys);
				}
				archive(cereal::make_nvp("Component", cereal::base_class<Component>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Parallax> &construct, std::uint32_t const version) {
				construct(std::shared_ptr<Node>());
				construct->load(archive, version);
				construct->initialize();
			}

			std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) override {
				return cloneHelper(a_parent->attach<Parallax>().self());
			}

			std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone) override;

			void initialize() override;
			void detachImplementation() override;
			void reattachImplementation() override;
			void updateImplementation(double a_delta) override;

			Point<> absolutePosition() const;

			MV_PROPERTY((bool), isEnabled, false);
			bool needsUpdate = false;

			MV_PROPERTY((Point<>), ourTranslateRatio);
			MV_PROPERTY((Point<>), ourLocalOffset);
			MV_PROPERTY((Point<>), ourZoomOffset);
			MV_DELETED_PROPERTY((bool), space);
			Node::BasicReceiverType parentObserver;
			Draw2D::CameraRecieveType cameraObserver;
		};
	}
}

CEREAL_FORCE_DYNAMIC_INIT(mv_sceneparallax);

#endif
