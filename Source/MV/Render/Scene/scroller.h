#ifndef _MV_SCENE_SCROLLER_H_
#define _MV_SCENE_SCROLLER_H_

#include "clickable.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {

		class Scroller : public jai::property_owner<Scroller, Clickable> {
			friend cereal::access;
			friend jai::access;
			friend Node;
		public:
			ClickableComponentDerivedAccessors(Scroller)

			std::shared_ptr<Node> content() const {
				return contentView;
			}

			std::shared_ptr<Scroller> content(const std::shared_ptr<Node> &a_activeView);

			virtual void cancelPress(bool a_callCancelCallbacks = true) override {
				isDragging = false;
				Clickable::cancelPress(a_callCancelCallbacks);
			}

		protected:
			Scroller(const std::weak_ptr<Node> &a_owner, TapDevice &a_mouse);

			void shiftContentByDelta(const MV::Point<int> & deltaPosition);

			template<class Archive>
			void save(Archive & archive, std::uint32_t const) const {
				if constexpr (jai::serialization::jai_archive<Archive>) {
					property_mgr.save(archive);
					Clickable::save(archive, 0);
				} else {
					archive(cereal::make_nvp("contentView", contentView.get()));
					archive(cereal::make_nvp("Clickable", cereal::base_class<Clickable>(this)));
				}
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				if constexpr (jai::serialization::jai_archive<Archive>) {
					property_mgr.load(archive);
					Clickable::load(archive, 0);
				} else {
					archive(cereal::make_nvp("contentView", contentView.get()));
					archive(cereal::make_nvp("Clickable", cereal::base_class<Clickable>(this)));
				}
			}

			template<class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Scroller> &construct, std::uint32_t const version) {
				MV::Services& services = cereal::get_user_data<MV::Services>(archive);
				auto* mouse = services.get<MV::TapDevice>();
				construct(std::shared_ptr<Node>(), *mouse);
				construct->load(archive, version);
				construct->initialize();
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Scroller>& construct) {
				auto* services = ar.template get_user_context<MV::Services>();
				construct(std::shared_ptr<Node>(), *services->template get<MV::TapDevice>());
				construct->load(ar, 0);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Scroller>(mouse()).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

			virtual void updateImplementation(double a_delta);

			virtual void acceptUpClick(bool a_ignoreBounds = false) override {
				isDragging = false;
				Clickable::acceptUpClick(a_ignoreBounds);
			}

		private:
			JAI_PROPERTY((std::shared_ptr<Node>), contentView);

			PointPrecision dragStartThreshold = 5.0f;
			PointPrecision cancelTimeThreshold = 1.25f;

			bool horizontalAllowed = true;
			bool verticalAllowed = true;
			
			bool isDragging = false;
			bool outOfBounds = false;
		};

	}
}

CEREAL_FORCE_DYNAMIC_INIT(mv_scenescroller);

#endif

