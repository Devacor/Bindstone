#ifndef _MV_SCENE_CLICKABLE_H_
#define _MV_SCENE_CLICKABLE_H_

#include "sprite.h"
#include "Interface/mouse.h"

#define ClickableComponentDerivedAccessors(ComponentType) \
	SpriteDerivedAccessors(ComponentType) \
	std::shared_ptr<ComponentType> clickDetectionType(BoundsType a_type) { \
		return std::static_pointer_cast<ComponentType>(Clickable::clickDetectionType(a_type)); \
	} \
	BoundsType clickDetectionType() { \
		return Clickable::clickDetectionType(); \
	}

namespace MV {
	namespace Scene {

		class Clickable : public Sprite {
			friend cereal::access;
			friend Node;
		public:
			typedef void ButtonSlotSignature(std::shared_ptr<Clickable>);
			typedef void DragSlotSignature(std::shared_ptr<Clickable>, const Point<int> &startPosition, const Point<int> &deltaPosition);

			enum class BoundsType { NODE_CHILDREN, NODE, LOCAL, NONE };
		private:

			Slot<ButtonSlotSignature> onDisabledSlot;
			Slot<ButtonSlotSignature> onEnabledSlot;

			Slot<ButtonSlotSignature> onPressSlot;
			Slot<ButtonSlotSignature> onReleaseSlot;

			Slot<ButtonSlotSignature> onDropSlot;

			Slot<ButtonSlotSignature> onAcceptSlot;
			Slot<ButtonSlotSignature> onCancelSlot;

			Slot<DragSlotSignature> onDragSlot;

		public:
			SpriteDerivedAccessors(Clickable)

			SlotRegister<ButtonSlotSignature> onEnabled;
			SlotRegister<ButtonSlotSignature> onDisabled;

			SlotRegister<ButtonSlotSignature> onPress;
			SlotRegister<ButtonSlotSignature> onRelease;
			SlotRegister<ButtonSlotSignature> onAccept;
			SlotRegister<ButtonSlotSignature> onCancel;

			SlotRegister<DragSlotSignature> onDrag;
			SlotRegister<ButtonSlotSignature> onDrop;

			std::shared_ptr<Clickable> clickDetectionType(BoundsType a_type);

			void disable();

			void startEatingTouches();
			void stopEatingTouches();
			bool isEatingTouches() const;

			BoundsType clickDetectionType() const;

			bool inPressEvent() const;

			bool disabled() const;

			bool enabled() const;

			MouseState& mouse() const;

		protected:
			Clickable(const std::weak_ptr<Node> &a_owner, MouseState &a_mouse);

			virtual void initialize();

			virtual void acceptDownClick();

			virtual void acceptUpClick();

			bool mouseInBounds(const MouseState& a_state);

			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					cereal::make_nvp("hitDetectionType", hitDetectionType),
					cereal::make_nvp("eatTouches", eatTouches),
					cereal::make_nvp("Clickable", cereal::base_class<Sprite>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Clickable> &construct) {
				MouseState *mouse = nullptr;
				archive.extract(cereal::make_nvp("mouse", mouse));
				MV::require<PointerException>(mouse != nullptr, "Null mouse in Clickable::load_and_construct.");
				construct(std::shared_ptr<Node>(), *mouse);
				archive(
					cereal::make_nvp("hitDetectionType", construct->hitDetectionType),
					cereal::make_nvp("eatTouches", construct->eatTouches),
					cereal::make_nvp("Sprite", cereal::base_class<Sprite>(construct.ptr()))
				);
			}

		private:
			MouseState::SignalType onLeftMouseDownHandle;
			MouseState::SignalType onLeftMouseUpHandle;
			MouseState::SignalType onMouseMoveHandle;

			Point<int> dragStartPosition;
			Point<int> priorMousePosition;
			Point<> objectLocationBeforeDrag;

			bool isInPressEvent = false;
			MouseState& ourMouse;

			bool eatTouches = true;
			BoundsType hitDetectionType = BoundsType::NODE_CHILDREN;
		};

	}
}

#endif
