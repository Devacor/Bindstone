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

			PointPrecision percent() const {
				return dragPercent;
			}

			std::shared_ptr<Slider> percent(PointPrecision a_newPercent, bool a_notify = true) {
				auto oldPercent = dragPercent;
				dragPercent = std::max(std::min(a_newPercent, 0.0f), 100.0f);
				auto self = std::static_pointer_cast<Slider>(shared_from_this());
				if (a_notify && oldPercent != dragPercent) {
					onPercentChangeSlot(self);
				}
				updateHandlePosition();
				return self;
			}

			std::shared_ptr<Slider> handle(const std::shared_ptr<Node> &a_handleNode) {
				dragHandle = a_handleNode;
				if (dragHandle->parent() != owner()) {
					dragHandle->parent(owner());
				}
				updateHandlePosition();
				return std::static_pointer_cast<Slider>(shared_from_this());
			}

			std::shared_ptr<Node> handle() const {
				return dragHandle;
			}

		protected:
			Slider(const std::weak_ptr<Node> &a_owner, MouseState &a_mouse) :
				Clickable(a_owner, a_mouse),
				onPercentChange(onPercentChangeSlot) {
			}

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
			}

		private:
			virtual void acceptDownClick() {
				std::lock_guard<std::recursive_mutex> guard(lock);
				Clickable::acceptDownClick();

				auto oldPercent = dragPercent;
				dragPercent = calculatePercentFromPosition(owner()->localFromScreen(mouse().position()));
				auto self = std::static_pointer_cast<Slider>(shared_from_this()); //keep alive
				if (dragPercent != oldPercent) {
					onPercentChangeSlot(self);
				}
				updateHandlePosition();
			}

			PointPrecision calculatePercentFromPosition(const Point<> &a_localPoint) {
				auto bounds = owner()->bounds();
				if (bounds.width() >= bounds.height()) {
					return (a_localPoint.x - bounds.minPoint.x) / bounds.width();
				} else {
					return (a_localPoint.y - bounds.minPoint.y) / bounds.height();
				}
			}

			void updateHandlePosition() {
				if (dragHandle) {
					auto areaBounds = owner()->bounds();
					auto areaPosition = owner()->position();
					areaBounds.minPoint -= areaPosition;
					areaBounds.maxPoint -= areaPosition;
					auto handleBounds = dragHandle->bounds();

					PointPrecision firstX = areaBounds.minPoint.x + (areaBounds.width() - handleBounds.width());
					PointPrecision secondY = areaBounds.minPoint.y + (areaBounds.height() - handleBounds.height());
					if (areaBounds.width() >= areaBounds.height()) {
						dragHandle->position({ mix(areaBounds.minPoint.x, firstX, dragPercent), mix(areaBounds.minPoint.y, secondY, .5f) });
					} else {
						dragHandle->position({ mix(areaBounds.minPoint.x, firstX, .5f), mix(areaBounds.minPoint.y, secondY, dragPercent) });
					}
				}
			}

			PointPrecision dragPercent = 0.0f;
			std::shared_ptr<Node> dragHandle;
		};

	}
}

#endif

