#ifndef _MV_SCENE_CLICKABLE_H_
#define _MV_SCENE_CLICKABLE_H_

#include "sprite.h"
#include "Interface/mouse.h"

#define ClickableComponentDerivedAccessors(ComponentType) \
	SpriteDerivedAccessors(ComponentType) \
	std::shared_ptr<ComponentType> clickDetectionType(MV::Scene::Clickable::BoundsType a_type) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Clickable::clickDetectionType(a_type)); \
	} \
	size_t globalPriority() const { \
		return MV::Scene::Clickable::globalPriority(); \
	} \
	std::shared_ptr<Clickable> globalPriority(size_t a_newPriority) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Clickable::globalPriority(a_newPriority)); \
	} \
	std::vector<size_t> overridePriority() const { \
		return MV::Scene::Clickable::overridePriority(); \
	} \
	std::shared_ptr<Clickable> overridePriority(const std::vector<size_t> &a_newPriority) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Clickable::overridePriority(a_newPriority)); \
	} \
	MV::Scene::Clickable::BoundsType clickDetectionType() { \
		return MV::Scene::Clickable::clickDetectionType(); \
	}

namespace MV {
	namespace Scene {

		class Clickable : public Sprite {
			friend cereal::access;
			friend Node;
		public:
			typedef void ButtonSlotSignature(std::shared_ptr<Clickable>);
			typedef void DragSlotSignature(std::shared_ptr<Clickable>, const Point<int> &startPosition, const Point<int> &deltaPosition);
			typedef void DropSlotSignature(std::shared_ptr<Clickable>, const Point<MV::PointPrecision> &velocity);

			enum class BoundsType { NODE_CHILDREN, NODE, LOCAL, NONE };
		private:

			Slot<ButtonSlotSignature> onDisabledSlot;
			Slot<ButtonSlotSignature> onEnabledSlot;

			Slot<ButtonSlotSignature> onPressSlot;
			Slot<DropSlotSignature> onReleaseSlot;

			Slot<ButtonSlotSignature> onAcceptSlot;
			Slot<ButtonSlotSignature> onCancelSlot;

			Slot<DragSlotSignature> onDragSlot;
			Slot<DropSlotSignature> onDropSlot;

		public:
			SpriteDerivedAccessors(Clickable)

			SlotRegister<ButtonSlotSignature> onEnabled;
			SlotRegister<ButtonSlotSignature> onDisabled;

			SlotRegister<ButtonSlotSignature> onPress;
			SlotRegister<DropSlotSignature> onRelease;

			SlotRegister<ButtonSlotSignature> onAccept;
			SlotRegister<ButtonSlotSignature> onCancel;

			SlotRegister<DragSlotSignature> onDrag;
			SlotRegister<DropSlotSignature> onDrop;

			std::shared_ptr<Clickable> clickDetectionType(BoundsType a_type);

			void disable();

			virtual void cancelPress(bool a_callCancelCallbacks = true);

			void startEatingTouches();
			void stopEatingTouches();
			bool isEatingTouches() const;

			BoundsType clickDetectionType() const;

			bool inPressEvent() const;

			bool disabled() const;

			bool enabled() const;

			MouseState& mouse() const;

			size_t globalPriority() const;
			std::shared_ptr<Clickable> globalPriority(size_t a_newPriority);

			std::vector<size_t> overridePriority() const;
			std::shared_ptr<Clickable> overridePriority( const std::vector<size_t> &a_newPriority) {
				overrideClickPriority = a_newPriority;
				return std::static_pointer_cast<Clickable>(shared_from_this());
			}
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
					cereal::make_nvp("globalClickPriority", globalClickPriority),
					cereal::make_nvp("overrideClickPriority", overrideClickPriority),
					cereal::make_nvp("Sprite", cereal::base_class<Sprite>(this))
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
					cereal::make_nvp("globalClickPriority", construct->globalClickPriority),
					cereal::make_nvp("overrideClickPriority", construct->overrideClickPriority),
					cereal::make_nvp("Sprite", cereal::base_class<Sprite>(construct.ptr()))
				);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Clickable>(mouse()).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

		private:
			MouseState::SignalType onLeftMouseDownHandle;
			MouseState::SignalType onLeftMouseUpHandle;
			MouseState::SignalType onMouseMoveHandle;

			Point<int> dragStartPosition;
			Point<int> priorMousePosition;
			Point<MV::PointPrecision> lastKnownVelocity;
			Point<> objectLocationBeforeDrag;

			bool isInPressEvent = false;
			MouseState& ourMouse;

			Stopwatch dragTimer;

			size_t globalClickPriority = 100;
			std::vector<size_t> overrideClickPriority;
			bool eatTouches = true;
			BoundsType hitDetectionType = BoundsType::LOCAL;
		};

	}
}

#endif
