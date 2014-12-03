#include "clickable.h"
#include "cereal/archives/json.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Clickable);

namespace MV {
	namespace Scene {

		Clickable::BoundsType Clickable::clickDetectionType() const {
			return hitDetectionType;
		}

		bool Clickable::inPressEvent() const {
			return isInPressEvent;
		}

		void Clickable::disable() {
			clickDetectionType(BoundsType::NONE);
		}

		bool Clickable::enabled() const {
			return !disabled();
		}

		bool Clickable::isEatingTouches() const {
			return eatTouches;
		}

		void Clickable::stopEatingTouches() {
			eatTouches = false;
		}

		std::shared_ptr<Clickable> Clickable::clickDetectionType(BoundsType a_type) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto self = std::static_pointer_cast<Clickable>(shared_from_this());
			if (a_type == BoundsType::NONE && hitDetectionType != BoundsType::NONE) {
				onEnabledSlot(self);
			} else if (hitDetectionType == BoundsType::NONE && a_type != BoundsType::NONE) {
				onDisabledSlot(self);
			}
			hitDetectionType = a_type;
			return self;
		}

		void Clickable::startEatingTouches() {
			eatTouches = true;
		}

		bool Clickable::disabled() const {
			return hitDetectionType == BoundsType::NONE;
		}

		MouseState & Clickable::mouse() const {
			return ourMouse;
		}

		Clickable::Clickable(const std::weak_ptr<Node>& a_owner, MouseState & a_mouse) :
			Sprite(a_owner),
			ourMouse(a_mouse),
			onPress(onPressSlot),
			onRelease(onReleaseSlot),
			onCancel(onCancelSlot),
			onAccept(onAcceptSlot),
			onDrag(onDragSlot),
			onDrop(onDropSlot),
			onEnabled(onEnabledSlot),
			onDisabled(onDisabledSlot) {

			shouldDraw = false;
		}

		void Clickable::initialize() {
			onLeftMouseDownHandle = ourMouse.onLeftMouseDown.connect([&](MouseState& a_mouse) {
				if (mouseInBounds(a_mouse)) {
					a_mouse.queueExclusiveAction({ eatTouches, owner()->parentIndexList(100), [&]() {
						acceptDownClick();
					}, []() {} });
				}
			});

			onLeftMouseUpHandle = ourMouse.onLeftMouseUp.connect([&](MouseState& a_mouse) {
				acceptUpClick();
			});
		}

		void Clickable::acceptUpClick() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto protectFromDismissal = std::static_pointer_cast<Clickable>(shared_from_this());
			bool inBounds = mouseInBounds(ourMouse);
			if (inPressEvent()) {
				onMouseMoveHandle = nullptr;

				isInPressEvent = false;
				if (inBounds) {
					onAcceptSlot(protectFromDismissal);
				} else {
					onCancelSlot(protectFromDismissal);
				}
				onReleaseSlot(protectFromDismissal);
			}
			if (inBounds) {
				onDropSlot(protectFromDismissal);
			}
		}

		void Clickable::acceptDownClick() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto protectFromDismissal = std::static_pointer_cast<Clickable>(shared_from_this());
			isInPressEvent = true;
			onPressSlot(protectFromDismissal);

			objectLocationBeforeDrag = owner()->position();
			dragStartPosition = ourMouse.position();
			priorMousePosition = dragStartPosition;

			onMouseMoveHandle = ourMouse.onMove.connect([&](MouseState& a_mouseInner) {
				auto protectFromDismissal = std::static_pointer_cast<Clickable>(shared_from_this());
				onDragSlot(protectFromDismissal, dragStartPosition, a_mouseInner.position() - priorMousePosition);
				priorMousePosition = a_mouseInner.position();
			});
		}

		bool Clickable::mouseInBounds(const MouseState & a_state) {
			if (owner()->active() && hitDetectionType != BoundsType::NONE) {
				BoxAABB<> hitBox;
				if (hitDetectionType == BoundsType::LOCAL) {
					hitBox = bounds();
				}
				else if (hitDetectionType == BoundsType::NODE) {
					hitBox = owner()->bounds(false);
				}
				else if (hitDetectionType == BoundsType::NODE_CHILDREN) {
					hitBox = owner()->bounds(true);
				}

				return owner()->screenFromLocal(hitBox).contains(a_state.position());
			} else {
				return false;
			}
		}
	}
}