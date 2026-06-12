#ifndef _MV_SCENE_SLIDER_H_
#define _MV_SCENE_SLIDER_H_

#include "MV/Render/Scene/clickable.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {
		class Slider : public jai::property_owner<Slider, Clickable> {
			friend jai::access;
			friend Node;
		public:
			typedef void SliderSignalSignature(std::shared_ptr<Slider>);
		private:
			jai::signal_emitter<SliderSignalSignature> onPercentChangeSignal;
		public:
			jai::signal<SliderSignalSignature> onPercentChange;

			ClickableComponentDerivedAccessors(Slider)

			PointPrecision percent() const;

			std::shared_ptr<Slider> percent(PointPrecision a_newPercent, bool a_notify = true);

			std::shared_ptr<Slider> handle(const std::shared_ptr<Node> &a_handleNode);

			std::shared_ptr<Node> handle() const;

		protected:
			Slider(const std::weak_ptr<Node> &a_owner, TapDevice &a_mouse);

			template<class Archive>
			void save(Archive & archive, std::uint32_t const) const {
				property_mgr.save(archive);
				Clickable::save(archive, 0);
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				property_mgr.load(archive);
				Clickable::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Slider>& construct) {
				auto* services = ar.template get_user_context<MV::Services>();
				construct(std::shared_ptr<Node>(), *services->template get<MV::TapDevice>());
				construct->load(ar, 0);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Slider>(mouse()).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

			virtual void initialize() override;

		private:
			virtual void acceptDownClick();

			void updateDragFromMouse();

			PointPrecision calculatePercentFromPosition(const Point<> &a_localPoint);

			void updateHandlePosition();

			JAI_PROPERTY((float), dragPercent, 0.0f);
			JAI_PROPERTY((std::shared_ptr<Node>), dragHandle);
		};

	}
}

#endif
