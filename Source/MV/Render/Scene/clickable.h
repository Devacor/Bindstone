#ifndef _MV_SCENE_CLICKABLE_H_
#define _MV_SCENE_CLICKABLE_H_

#include "sprite.h"
#include "MV/Utility/stopwatch.h"
#include "MV/Utility/signal.hpp"  // For backward-compatible deserialization of old MV::Signal data
#include "MV/Interface/tapDevice.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

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

		class Clickable : public jai::property_owner<Clickable, Sprite> {
			friend jai::access;
			friend Node;
		public:
			typedef void ButtonSignalSignature(const std::shared_ptr<Clickable>&);
			typedef void DragSignalSignature(const std::shared_ptr<Clickable>&, const Point<int>& startPosition, const Point<int>& deltaPosition);
			typedef void DropSignalSignature(const std::shared_ptr<Clickable>&, const Point<MV::PointPrecision>& velocity);

			enum class BoundsType { LOCAL, NODE, CHILDREN, NODE_CHILDREN, NONE };
		private:

			jai::signal_emitter<ButtonSignalSignature> onDisabledSignal;
			jai::signal_emitter<ButtonSignalSignature> onEnabledSignal;

			jai::signal_emitter<ButtonSignalSignature> onPressSignal;
			jai::signal_emitter<DropSignalSignature> onReleaseSignal;

			jai::signal_emitter<ButtonSignalSignature> onAcceptSignal;
			jai::signal_emitter<ButtonSignalSignature> onCancelSignal;

			jai::signal_emitter<DragSignalSignature> onDragSignal;
			jai::signal_emitter<DropSignalSignature> onDropSignal;

		public:
			SpriteDerivedAccessors(Clickable)

				//void (std::shared_ptr<Clickable>)
				jai::signal<ButtonSignalSignature> onEnabled;
			//void (std::shared_ptr<Clickable>)
			jai::signal<ButtonSignalSignature> onDisabled;

			//void (std::shared_ptr<Clickable>)
			jai::signal<ButtonSignalSignature> onPress;
			//void (std::shared_ptr<Clickable>, const Point<MV::PointPrecision> &velocity)
			jai::signal<DropSignalSignature> onRelease;

			//void (std::shared_ptr<Clickable>)
			jai::signal<ButtonSignalSignature> onAccept;
			//void (std::shared_ptr<Clickable>)
			jai::signal<ButtonSignalSignature> onCancel;

			//void (std::shared_ptr<Clickable>, const Point<int> &startPosition, const Point<int> &deltaPosition)
			jai::signal<DragSignalSignature> onDrag;
			//void (std::shared_ptr<Clickable>, const Point<MV::PointPrecision> &velocity)
			jai::signal<DropSignalSignature> onDrop;

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
			std::shared_ptr<Clickable> overridePriority(const std::vector<int64_t>& a_newPriority) {
				overrideClickPriority = a_newPriority;
				return std::static_pointer_cast<Clickable>(shared_from_this());
			}

			bool mouseInBounds(const TapDevice& a_state);
			bool mouseInBounds() { return mouseInBounds(ourMouse); }

			void press();

		protected:
			Clickable(const std::weak_ptr<Node>& a_owner, TapDevice& a_mouse);

			virtual void initialize() override;

			virtual std::vector<int64_t> clickPriority() const;

			virtual void acceptDownClick();

			virtual void acceptUpClick(bool a_ignoreBounds = false);

			template<class Archive>
			void save(Archive& archive, std::uint32_t const /*version*/) const {
				property_mgr.save(archive);
				Sprite::save(archive, 0);
			}

			template<class Archive>
			void load(Archive& archive, std::uint32_t const version) {
				property_mgr.load(archive);
				Sprite::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Clickable>& construct) {
				auto* services = ar.template get_user_context<MV::Services>();
				construct(std::shared_ptr<Node>(), *services->template get<MV::TapDevice>());
				construct->load(ar, 0);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node>& a_parent) {
				return cloneHelper(a_parent->attach<Clickable>(mouse()).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component>& a_clone);

			virtual void onOwnerDestroyed() {
				Sprite::onOwnerDestroyed();
				onLeftMouseDownHandle.reset();
				onLeftMouseUpHandle.reset();
				onMouseMoveHandle.reset();
			}

			JAI_PROPERTY((int64_t), globalClickPriority, 100, [](auto&, auto&) {});
			JAI_PROPERTY((int64_t), appendClickPriority, 0, [](auto&, auto&) {});
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

			JAI_PROPERTY((std::vector<int64_t>), overrideClickPriority, {}, [](auto&, auto&) {});
			JAI_PROPERTY((bool), eatTouches, true);
			JAI_PROPERTY((BoundsType), hitDetectionType, BoundsType::LOCAL);
		};

	}
}

#endif
