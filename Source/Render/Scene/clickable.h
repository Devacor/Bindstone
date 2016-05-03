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
			typedef void ButtonSignalSignature(std::shared_ptr<Clickable>);
			typedef void DragSignalSignature(std::shared_ptr<Clickable>, const Point<int> &startPosition, const Point<int> &deltaPosition);
			typedef void DropSignalSignature(std::shared_ptr<Clickable>, const Point<MV::PointPrecision> &velocity);

			enum class BoundsType { LOCAL, NODE, CHILDREN, NODE_CHILDREN, NONE };
		private:

			Signal<ButtonSignalSignature> onDisabledSignal;
			Signal<ButtonSignalSignature> onEnabledSignal;

			Signal<ButtonSignalSignature> onPressSignal;
			Signal<DropSignalSignature> onReleaseSignal;

			Signal<ButtonSignalSignature> onAcceptSignal;
			Signal<ButtonSignalSignature> onCancelSignal;

			Signal<DragSignalSignature> onDragSignal;
			Signal<DropSignalSignature> onDropSignal;

		public:
			SpriteDerivedAccessors(Clickable)

			//void (std::shared_ptr<Clickable>)
			SignalRegister<ButtonSignalSignature> onEnabled;
			//void (std::shared_ptr<Clickable>)
			SignalRegister<ButtonSignalSignature> onDisabled;

			//void (std::shared_ptr<Clickable>)
			SignalRegister<ButtonSignalSignature> onPress;
			//void (std::shared_ptr<Clickable>, const Point<MV::PointPrecision> &velocity)
			SignalRegister<DropSignalSignature> onRelease;

			//void (std::shared_ptr<Clickable>)
			SignalRegister<ButtonSignalSignature> onAccept;
			//void (std::shared_ptr<Clickable>)
			SignalRegister<ButtonSignalSignature> onCancel;

			//void (std::shared_ptr<Clickable>, const Point<int> &startPosition, const Point<int> &deltaPosition)
			SignalRegister<DragSignalSignature> onDrag;
			//void (std::shared_ptr<Clickable>, const Point<MV::PointPrecision> &velocity)
			SignalRegister<DropSignalSignature> onDrop;

			std::shared_ptr<Clickable> clickDetectionType(BoundsType a_type);

			void disable();

			virtual void cancelPress(bool a_callCancelCallbacks = true);

			void startEatingTouches();
			void stopEatingTouches();
			bool eatingTouches() const;

			BoundsType clickDetectionType() const;

			bool inPressEvent() const;

			bool disabled() const;

			bool enabled() const;

			MouseState& mouse() const;

			double dragDelta() const {
				return lastDragDelta;
			}

			PointPrecision totalDragDistance() const {
				return accumulatedDragDistance;
			}

			double dragTime() const {
				return dragTimer.check();
			}

			size_t globalPriority() const;
			std::shared_ptr<Clickable> globalPriority(size_t a_newPriority);

			std::vector<size_t> overridePriority() const;
			std::shared_ptr<Clickable> overridePriority( const std::vector<size_t> &a_newPriority) {
				overrideClickPriority = a_newPriority;
				return std::static_pointer_cast<Clickable>(shared_from_this());
			}

			bool mouseInBounds(const MouseState& a_state);
			bool mouseInBounds() { return mouseInBounds(ourMouse); }

			void press();

		protected:
			Clickable(const std::weak_ptr<Node> &a_owner, MouseState &a_mouse);

			virtual void initialize() override;

			virtual void acceptDownClick();

			virtual void acceptUpClick(bool a_ignoreBounds = false);

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const version) {
				archive(
					cereal::make_nvp("hitDetectionType", hitDetectionType),
					cereal::make_nvp("eatTouches", eatTouches),
					cereal::make_nvp("globalClickPriority", globalClickPriority),
					cereal::make_nvp("overrideClickPriority", overrideClickPriority),
					cereal::make_nvp("Sprite", cereal::base_class<Sprite>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Clickable> &construct, std::uint32_t const version) {
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

		protected:
			virtual void onOwnerDestroyed() {
				onLeftMouseDownHandle.reset();
				onLeftMouseUpHandle.reset();
				onMouseMoveHandle.reset();
			}

		private:
			MouseState::SignalType onLeftMouseDownHandle;
			MouseState::SignalType onLeftMouseUpHandle;
			MouseState::SignalType onMouseMoveHandle;

			Point<int> dragStartPosition;
			Point<int> priorMousePosition;
			Point<MV::PointPrecision> lastKnownVelocity;
			double lastDragDelta = 0.0;
			PointPrecision accumulatedDragDistance = 0.0f;
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
