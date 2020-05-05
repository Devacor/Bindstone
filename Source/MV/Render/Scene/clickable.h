#ifndef _MV_SCENE_CLICKABLE_H_
#define _MV_SCENE_CLICKABLE_H_

#include "sprite.h"
#include "MV/Utility/stopwatch.h"
#include "MV/Interface/tapDevice.h"

#define ClickableComponentDerivedAccessors(ComponentType) \
	SpriteDerivedAccessors(ComponentType) \
	std::shared_ptr<ComponentType> clickDetectionType(MV::Scene::Clickable::BoundsType a_type) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Clickable::clickDetectionType(a_type)); \
	} \
	int64_t globalPriority() const { \
		return MV::Scene::Clickable::globalPriority(); \
	} \
	std::shared_ptr<ComponentType> globalPriority(int64_t a_newPriority) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Clickable::globalPriority(a_newPriority)); \
	} \
	int64_t appendPriority() const { \
		return MV::Scene::Clickable::appendPriority(); \
	} \
	std::shared_ptr<ComponentType> appendPriority(int64_t a_newPriority) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Clickable::appendPriority(a_newPriority)); \
	} \
	std::vector<int64_t> overridePriority() const { \
		return MV::Scene::Clickable::overridePriority(); \
	} \
	std::shared_ptr<ComponentType> overridePriority(const std::vector<int64_t> &a_newPriority) { \
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
			typedef void ButtonSignalSignature(const std::shared_ptr<Clickable> &);
			typedef void DragSignalSignature(const std::shared_ptr<Clickable> &, const Point<int> &startPosition, const Point<int> &deltaPosition);
			typedef void DropSignalSignature(const std::shared_ptr<Clickable> &, const Point<MV::PointPrecision> &velocity);

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

			TapDevice& mouse() const;

			double dragDelta() const {
				return lastDragDelta;
			}

			PointPrecision totalDragDistance() const {
				return accumulatedDragDistance;
			}

			double dragTime() const {
				return dragTimer.check();
			}

			int64_t globalPriority() const;
			std::shared_ptr<Clickable> globalPriority(int64_t a_newPriority);

			int64_t appendPriority() const;
			std::shared_ptr<Clickable> appendPriority(int64_t a_newPriority);

			std::vector<int64_t> overridePriority() const;
			std::shared_ptr<Clickable> overridePriority( const std::vector<int64_t> &a_newPriority) {
				overrideClickPriority = a_newPriority;
				return std::static_pointer_cast<Clickable>(shared_from_this());
			}

			bool mouseInBounds(const TapDevice& a_state);
			bool mouseInBounds() { return mouseInBounds(ourMouse); }

			void press();

		protected:
			Clickable(const std::weak_ptr<Node> &a_owner, TapDevice &a_mouse);

			virtual void initialize() override;

			virtual std::vector<int64_t> clickPriority() const;

			virtual void acceptDownClick();

			virtual void acceptUpClick(bool a_ignoreBounds = false);

			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				archive(
					cereal::make_nvp("onPress", onPressSignal),
					cereal::make_nvp("onRelease", onReleaseSignal),
					cereal::make_nvp("onDrag", onDragSignal),
					cereal::make_nvp("onAccept", onAcceptSignal),
					cereal::make_nvp("onCancel", onCancelSignal),
					cereal::make_nvp("onDrop", onDropSignal),
					cereal::make_nvp("hitDetectionType", hitDetectionType),
					cereal::make_nvp("eatTouches", eatTouches),
					cereal::make_nvp("globalClickPriority", globalClickPriority),
					cereal::make_nvp("appendClickPriority", appendClickPriority),
					cereal::make_nvp("overrideClickPriority", overrideClickPriority),
					cereal::make_nvp("Sprite", cereal::base_class<Sprite>(this))
				);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const /*version*/) {
				archive(
					cereal::make_nvp("onPress", onPressSignal),
					cereal::make_nvp("onRelease", onReleaseSignal),
					cereal::make_nvp("onDrag", onDragSignal),
					cereal::make_nvp("onAccept", onAcceptSignal),
					cereal::make_nvp("onCancel", onCancelSignal),
					cereal::make_nvp("onDrop", onDropSignal),
					cereal::make_nvp("hitDetectionType", hitDetectionType),
					cereal::make_nvp("eatTouches", eatTouches),
					cereal::make_nvp("globalClickPriority", globalClickPriority),
					cereal::make_nvp("appendClickPriority", appendClickPriority),
					cereal::make_nvp("overrideClickPriority", overrideClickPriority),
					cereal::make_nvp("Sprite", cereal::base_class<Sprite>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Clickable> &construct, std::uint32_t const version) {
				MV::Services& services = cereal::get_user_data<MV::Services>(archive);
				auto* mouse = services.get<MV::TapDevice>();
				construct(std::shared_ptr<Node>(), *mouse);
				construct->load(archive, version);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Clickable>(mouse()).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

			virtual void onOwnerDestroyed() {
				Sprite::onOwnerDestroyed();
				onLeftMouseDownHandle.reset();
				onLeftMouseUpHandle.reset();
				onMouseMoveHandle.reset();
			}

			int64_t globalClickPriority = 100;
			int64_t appendClickPriority = 0;
		private:
			TapDevice::SignalType onLeftMouseDownHandle;
			TapDevice::SignalType onLeftMouseUpHandle;
			TapDevice::SignalType onMouseMoveHandle;

			Point<int> dragStartPosition;
			Point<int> priorMousePosition;
			Point<MV::PointPrecision> lastKnownVelocity;
			double lastDragDelta = 0.0;
			PointPrecision accumulatedDragDistance = 0.0f;
			Point<> objectLocationBeforeDrag;

			bool isInPressEvent = false;
			TapDevice& ourMouse;

			Stopwatch dragTimer;

			std::string onAcceptScript;
			std::string onCancelScript;

			std::vector<int64_t> overrideClickPriority;
			bool eatTouches = true;
			BoundsType hitDetectionType = BoundsType::LOCAL;
		};

	}
}

CEREAL_FORCE_DYNAMIC_INIT(mv_sceneclickable);

#endif
