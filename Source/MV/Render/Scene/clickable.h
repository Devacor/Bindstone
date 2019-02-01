#ifndef _MV_SCENE_CLICKABLE_H_
#define _MV_SCENE_CLICKABLE_H_

#include "sprite.h"
#include "MV/Utility/stopwatch.h"
#include "MV/Interface/mouse.h"

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

			static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script, TapDevice &a_tapDevice) {
				a_script.add(chaiscript::user_type<Clickable>(), "Clickable");
				a_script.add(chaiscript::base_class<Sprite, Clickable>());
				a_script.add(chaiscript::base_class<Drawable, Clickable>());
				a_script.add(chaiscript::base_class<Component, Clickable>());

				a_script.add(chaiscript::fun([&](Node &a_self) { return a_self.attach<Clickable>(a_tapDevice); }), "attachClickable");

				a_script.add(chaiscript::fun([](Node &a_self) { return a_self.componentInChildren<Clickable>(true, false, true); }), "componentClickable");

				a_script.add(chaiscript::fun(&Clickable::eatingTouches), "eatingTouches");
				a_script.add(chaiscript::fun(&Clickable::startEatingTouches), "startEatingTouches");
				a_script.add(chaiscript::fun(&Clickable::stopEatingTouches), "stopEatingTouches");

				a_script.add(chaiscript::fun([](Clickable &a_self) { return a_self.clickDetectionType(); }), "clickDetectionType");
				a_script.add(chaiscript::fun([](Clickable &a_self, BoundsType a_boundsType) { return a_self.clickDetectionType(a_boundsType); }), "clickDetectionType");

				a_script.add(chaiscript::fun(&Clickable::inPressEvent), "inPressEvent");

				a_script.add(chaiscript::fun(&Clickable::disable), "disable");

				a_script.add(chaiscript::fun(&Clickable::enabled), "enabled");
				a_script.add(chaiscript::fun(&Clickable::disabled), "disabled");

				a_script.add(chaiscript::fun(&Clickable::mouse), "mouse");

				a_script.add(chaiscript::fun(&Clickable::dragTime), "dragTime");
				a_script.add(chaiscript::fun(&Clickable::dragDelta), "dragDelta");
				a_script.add(chaiscript::fun(&Clickable::totalDragDistance), "totalDragDistance");

				a_script.add(chaiscript::fun(&Clickable::press), "press");
				a_script.add(chaiscript::fun(&Clickable::cancelPress), "cancelPress");

				SignalRegister<ButtonSignalSignature>::hook(a_script);
				a_script.add(chaiscript::fun(&Clickable::onPress), "onPress");
				a_script.add(chaiscript::fun(&Clickable::onRelease), "onRelease");
				a_script.add(chaiscript::fun(&Clickable::onAccept), "onAccept");
				a_script.add(chaiscript::fun(&Clickable::onCancel), "onCancel");
				SignalRegister<DragSignalSignature>::hook(a_script);
				a_script.add(chaiscript::fun(&Clickable::onDrag), "onDrag");
				SignalRegister<DropSignalSignature>::hook(a_script);
				a_script.add(chaiscript::fun(&Clickable::onDrop), "onDrop");

				a_script.add(chaiscript::type_conversion<SafeComponent<Clickable>, std::shared_ptr<Clickable>>([](const SafeComponent<Clickable> &a_item) { return a_item.self(); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Clickable>, std::shared_ptr<Sprite>>([](const SafeComponent<Clickable> &a_item) { return std::static_pointer_cast<Sprite>(a_item.self()); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Clickable>, std::shared_ptr<Drawable>>([](const SafeComponent<Clickable> &a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Clickable>, std::shared_ptr<Component>>([](const SafeComponent<Clickable> &a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));

				return a_script;
			}

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
				auto& services = cereal::get_user_data<MV::Services>(archive);
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
