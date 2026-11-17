#ifndef _MV_SCENE_CLICKABLE_H_
#define _MV_SCENE_CLICKABLE_H_

#include "sprite.h"
#include "MV/Utility/stopwatch.h"
#include "MV/Interface/tapDevice.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/signals/signal_property.hpp>

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
			static void jai_auto_bind(jai::dynamic_binder<Clickable>& a_builder);

			typedef void ButtonSignalSignature(const std::shared_ptr<Clickable>&);
			typedef void DragSignalSignature(const std::shared_ptr<Clickable>&, const Point<int>& startPosition, const Point<int>& deltaPosition);
			typedef void DropSignalSignature(const std::shared_ptr<Clickable>&, const Point<MV::PointPrecision>& velocity);

			enum class BoundsType { LOCAL, NODE, CHILDREN, NODE_CHILDREN, NONE };
		private:

			jai::signal_emitter<ButtonSignalSignature> onDisabledSignal;
			jai::signal_emitter<ButtonSignalSignature> onEnabledSignal;
			jai::signal_emitter<ButtonSignalSignature> onHoverEnterSignal;
			jai::signal_emitter<ButtonSignalSignature> onHoverLeaveSignal;
			jai::signal_emitter<ButtonSignalSignature> onFocusEnterSignal;
			jai::signal_emitter<ButtonSignalSignature> onFocusLeaveSignal;

		public:
			SpriteDerivedAccessors(Clickable)

				jai::signal<ButtonSignalSignature> onEnabled;
			jai::signal<ButtonSignalSignature> onDisabled;

			// The pointer came over this Clickable as the top-most one, or left it. Only while its TapDevice tracks
			// hover (TapDevice::trackHover); not serialized.
			jai::signal<ButtonSignalSignature> onHoverEnter;
			jai::signal<ButtonSignalSignature> onHoverLeave;
			bool hovered() const { return isHovered; }

			// Keyboard and controller focus, driven through the TapDevice (focusNext, focusMove, focusActivate). A
			// focusable Clickable takes part while it is enabled and visible; a Button is focusable from the start.
			// focusOrder comes before place in the scene when walking; pressOnFocus presses the Clickable as it takes
			// the focus, for a text field that should start taking keys at once.
			std::shared_ptr<Clickable> focusable(bool a_focusable);
			bool focusable() const { return isFocusable; }
			std::shared_ptr<Clickable> focusOrder(int64_t a_order);
			int64_t focusOrder() const { return focusOrderValue; }
			std::shared_ptr<Clickable> pressOnFocus(bool a_press);
			bool pressOnFocus() const { return pressesOnFocus; }
			void focus();
			bool focused() const { return isFocused; }
			// The neighbour a move goes to from here, named by its node's id and looked for in this Clickable's scene
			// when the move is made. A link is followed back as well: if A names B as its right, B's left is A unless
			// B names its own, so one end is enough. An empty id, or one that finds nothing able to take the focus,
			// leaves the move to be worked out.
			// Link names: next, previous, left, right, up, down, upLeft, upRight, downLeft, downRight.
			std::shared_ptr<Clickable> focusLink(FocusLink a_link, const std::string& a_nodeId);
			std::string focusLink(FocusLink a_link) const;
			std::shared_ptr<Clickable> focusLink(const std::string& a_linkName, const std::string& a_nodeId);
			static const std::string& focusLinkName(FocusLink a_link);
			jai::signal<ButtonSignalSignature> onFocusEnter;
			jai::signal<ButtonSignalSignature> onFocusLeave;

			// Serialized through property_mgr (script receivers persist across save/load);
			// emit through the generated onXSignal(args) emitter as before.
			JAI_SIGNAL(ButtonSignalSignature, onPress);
			JAI_SIGNAL(DropSignalSignature, onRelease);
			JAI_SIGNAL(ButtonSignalSignature, onAccept);
			JAI_SIGNAL(ButtonSignalSignature, onCancel);
			JAI_SIGNAL(DragSignalSignature, onDrag);
			JAI_SIGNAL(DropSignalSignature, onDrop);

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
				onHoverQueryHandle.reset();
				onFocusQueryHandle.reset();
			}

			JAI_PROPERTY((int64_t), globalClickPriority, 100, [](auto&, auto&) {});
			JAI_PROPERTY((int64_t), appendClickPriority, 0, [](auto&, auto&) {});
			JAI_NAMED_PROPERTY((bool), "focusable", isFocusable, false);
			JAI_NAMED_PROPERTY((int64_t), "focusOrder", focusOrderValue, 0);
			JAI_NAMED_PROPERTY((bool), "pressOnFocus", pressesOnFocus, false);
			JAI_NAMED_PROPERTY((std::map<std::string, std::string>), "focusLinks", focusLinkIds);
		private:
			TapDevice::SignalType onLeftMouseDownHandle;
			TapDevice::SignalType onLeftMouseUpHandle;
			TapDevice::SignalType onMouseMoveHandle;
			TapDevice::SignalType onHoverQueryHandle;

			void hover(bool a_hovered);
			bool isHovered = false;

			TapDevice::SignalType onFocusQueryHandle;
			FocusCandidate focusCandidate();
			bool focusNeighbour(FocusLink a_link, const void* a_scope, FocusCandidate& a_neighbour);
			bool canTakeFocus(const void* a_scope) const;
			bool underFocusScope(const void* a_scope) const;
			void focusChanged(bool a_focused);
			bool isFocused = false;

			Point<int> dragStartPosition;
			Point<int> priorMousePosition;
			Point<MV::PointPrecision> lastKnownVelocity;
			double lastDragDelta = 0.0;
			PointPrecision accumulatedDragDistance = 0.0f;
			Point<> objectLocationBeforeDrag;

			bool isInPressEvent = false;
			TapDevice& ourMouse;

			Stopwatch dragTimer;

			JAI_PROPERTY((std::vector<int64_t>), overrideClickPriority, {}, [](auto&, auto&) {});
			JAI_PROPERTY((bool), eatTouches, true);
			JAI_PROPERTY((BoundsType), hitDetectionType, BoundsType::LOCAL);
		};

	}
}

#endif
