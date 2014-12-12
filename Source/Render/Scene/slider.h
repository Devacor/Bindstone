#ifndef _MV_SCENE_SLIDER_H_
#define _MV_SCENE_SLIDER_H_

#include "Render/Scene/clickable.h"

namespace MV {
	namespace Scene {
		class Slider : public Clickable {
			friend cereal::access;
			friend Node;
		public:
			typedef void SliderSlotSignature(std::shared_ptr<Slider>);
		private:
			Slot<SliderSlotSignature> onPercentChangeSlot;
		public:
			SlotRegister<SliderSlotSignature> onPercentChange;

			ClickableComponentDerivedAccessors(Slider)

			PointPrecision percent() const;

			std::shared_ptr<Slider> percent(PointPrecision a_newPercent, bool a_notify = true);

			std::shared_ptr<Slider> handle(const std::shared_ptr<Node> &a_handleNode);

			std::shared_ptr<Node> handle() const;

		protected:
			Slider(const std::weak_ptr<Node> &a_owner, MouseState &a_mouse);

			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					CEREAL_NVP(dragPercent),
					CEREAL_NVP(dragHandle),
					cereal::make_nvp("Clickable", cereal::base_class<Clickable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Slider> &construct) {
				MouseState *mouse = nullptr;
				archive.extract(cereal::make_nvp("mouse", mouse));
				MV::require<PointerException>(mouse != nullptr, "Null mouse in Slider::load_and_construct.");
				construct(std::shared_ptr<Node>(), *mouse);
				archive(
					cereal::make_nvp("dragPercent", construct->dragPercent),
					cereal::make_nvp("dragHandle", construct->dragHandle),
					cereal::make_nvp("Clickable", cereal::base_class<Clickable>(construct.ptr()))
				);
				construct->initialize();
			}

			virtual void initialize() override {
				Clickable::initialize();
				Clickable::onDrag.connect("HandleDrag", [](const std::shared_ptr<Clickable> &a_clickable, const Point<int> &startPosition, const Point<int> &deltaPosition) {
					auto self = std::static_pointer_cast<Slider>(a_clickable);
					self->updateDragFromMouse();
				});
			}

		private:
			virtual void acceptDownClick();

			void updateDragFromMouse();

			PointPrecision calculatePercentFromPosition(const Point<> &a_localPoint);

			void updateHandlePosition();

			PointPrecision dragPercent = 0.0f;
			std::shared_ptr<Node> dragHandle;
		};

	}
}

#endif

