#include "clickable.h"
#include "clipped.h"
#include "stencil.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Clickable);
CEREAL_REGISTER_DYNAMIC_INIT(mv_sceneclickable);

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

		void Clickable::cancelPress(bool a_callCancelCallbacks) {
			if (inPressEvent()) {
				auto self = std::static_pointer_cast<Clickable>(shared_from_this());
				onMouseMoveHandle = nullptr;
				isInPressEvent = false;
				if (a_callCancelCallbacks) {
					onCancelSignal(self);
					onReleaseSignal(self, lastKnownVelocity);
				}
			}
		}

		bool Clickable::enabled() const {
			return !disabled();
		}

		bool Clickable::eatingTouches() const {
			return eatTouches;
		}

		void Clickable::stopEatingTouches() {
			eatTouches = false;
		}

		std::shared_ptr<Clickable> Clickable::clickDetectionType(BoundsType a_type) {
			auto self = std::static_pointer_cast<Clickable>(shared_from_this());
			if (a_type == BoundsType::NONE && hitDetectionType != BoundsType::NONE) {
				onEnabledSignal(self);
			} else if (hitDetectionType == BoundsType::NONE && a_type != BoundsType::NONE) {
				onDisabledSignal(self);
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

		TapDevice & Clickable::mouse() const {
			return ourMouse;
		}

		int64_t Clickable::globalPriority() const {
			return globalClickPriority;
		}

		std::shared_ptr<Clickable> Clickable::globalPriority(int64_t a_newPriority) {
			globalClickPriority = a_newPriority;
			return std::static_pointer_cast<Clickable>(shared_from_this());
		}

		int64_t Clickable::appendPriority() const {
			return appendClickPriority;
		}

		std::shared_ptr<Clickable> Clickable::appendPriority(int64_t a_newPriority) {
			appendClickPriority = a_newPriority;
			return std::static_pointer_cast<Clickable>(shared_from_this());
		}

		std::vector<int64_t> Clickable::overridePriority() const {
			return overrideClickPriority;
		}

		Clickable::Clickable(const std::weak_ptr<Node>& a_owner, TapDevice & a_mouse) :
			Sprite(a_owner),
			ourMouse(a_mouse),
			onPress(onPressSignal),
			onRelease(onReleaseSignal),
			onCancel(onCancelSignal),
			onAccept(onAcceptSignal),
			onDrag(onDragSignal),
			onDrop(onDropSignal),
			onEnabled(onEnabledSignal),
			onDisabled(onDisabledSignal) {

			shouldDraw = false;
		}

		void Clickable::initialize() {
			Sprite::initialize();
			onLeftMouseDownHandle = ourMouse.onLeftMouseDown.connect([&](TapDevice& a_mouse) {
				if (mouseInBounds(a_mouse)) {
					a_mouse.queueExclusiveAction({ eatTouches, clickPriority(), [&]() {
						acceptDownClick();
					}, []() {}, owner()->id() });
				}
			});

			onLeftMouseUpHandle = ourMouse.onLeftMouseUp.connect([&](TapDevice& a_mouse) {
				acceptUpClick();
			});
		}

		std::vector<int64_t> Clickable::clickPriority() const {
			if (overrideClickPriority.empty()) {
				auto priority = owner()->parentIndexList(globalClickPriority);
				if (appendPriority() != 0) {
					priority.push_back(appendPriority());
				}
				return priority;
			} else {
				return overrideClickPriority;
			}
		}

		void Clickable::acceptUpClick(bool a_ignoreBounds) {
			auto protectFromDismissal = std::static_pointer_cast<Clickable>(shared_from_this());
			bool inBounds = a_ignoreBounds || mouseInBounds(ourMouse);
			try {
				if (inPressEvent()) {
					onMouseMoveHandle = nullptr;

					isInPressEvent = false;
					if (inBounds) {
						onAcceptSignal(protectFromDismissal);
					} else {
						onCancelSignal(protectFromDismissal);
					}
					onReleaseSignal(protectFromDismissal, lastKnownVelocity);
				}
				if (inBounds) {
					onDropSignal(protectFromDismissal, lastKnownVelocity);
				}
			} catch (const std::exception& e) {
				std::cerr << "Clickable::acceptUpClick Exception [" << e.what() << "]" << std::endl;
			} catch (chaiscript::Boxed_Value &bv) {
				std::cerr << "Clickable::acceptUpClick Exception [" << chaiscript::boxed_cast<chaiscript::exception::eval_error&>(bv).what() << "]" << std::endl;
			}
		}

		void Clickable::acceptDownClick() {
			auto protectFromDismissal = std::static_pointer_cast<Clickable>(shared_from_this());
			isInPressEvent = true;
			onPressSignal(protectFromDismissal);

			objectLocationBeforeDrag = owner()->position();
			dragStartPosition = ourMouse.position();
			priorMousePosition = dragStartPosition;
			lastKnownVelocity.clear();
			dragTimer.start();
			accumulatedDragDistance = 0.0f;
			onMouseMoveHandle = ourMouse.onMove.connect([this, protectFromDismissal](TapDevice& a_mouseInner) {
				auto dragDeltaPosition = a_mouseInner.position() - priorMousePosition;
				auto dt = dragTimer.delta();
				lastDragDelta = dt;
				accumulatedDragDistance += dragDeltaPosition.magnitude();
				if (dragDeltaPosition.x != 0 || dragDeltaPosition.y != 0) {
					auto previousLastVelocity = lastKnownVelocity;
					lastKnownVelocity = (lastKnownVelocity + (cast<MV::PointPrecision>(dragDeltaPosition) * static_cast<MV::PointPrecision>(dt))) / 2.0f;
					onDragSignal(protectFromDismissal, dragStartPosition, dragDeltaPosition);
				}
				priorMousePosition = a_mouseInner.position();
			});
		}

		bool Clickable::mouseInBounds(const TapDevice & a_state) {
			if (ownerIsAlive() && owner()->visible() && hitDetectionType != BoundsType::NONE) {
				BoxAABB<> hitBox;
				if (hitDetectionType == BoundsType::LOCAL) {
					hitBox = bounds();
				} else if (hitDetectionType == BoundsType::NODE) {
					hitBox = owner()->bounds(false);
				} else if (hitDetectionType == BoundsType::NODE_CHILDREN) {
					hitBox = owner()->bounds(true);
				} else if (hitDetectionType == BoundsType::CHILDREN) {
					hitBox = owner()->childBounds();
				}

				auto foundClippedParents = owner()->componentsInParents<Clipped, Stencil>(false, false);
				for (auto&& clippedParent : foundClippedParents) {
					BoxAABB<int> screenBounds;

					MV::visit(clippedParent,
						[&](const MV::Scene::SafeComponent<MV::Scene::Clipped> &a_clipped) {
							screenBounds = a_clipped->owner()->screenFromLocal(a_clipped->bounds());
						}, [&](const MV::Scene::SafeComponent<MV::Scene::Stencil> &a_stencil) {
							screenBounds = a_stencil->owner()->screenFromLocal(a_stencil->bounds());
						});

					if (!screenBounds.contains(a_state.position())) {
						return false;
					}
				}

				return hitBox.contains(owner()->localFromScreen(a_state.position()));
			} else {
				return false;
			}
		}

		void Clickable::press() {
			auto self = shared_from_this();
			acceptDownClick();
			acceptUpClick(true);
		}

		std::shared_ptr<Component> Clickable::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Sprite::cloneHelper(a_clone);
			auto clickableClone = std::static_pointer_cast<Clickable>(a_clone);
			clickableClone->eatTouches = eatTouches;
			clickableClone->hitDetectionType = hitDetectionType;
			return a_clone;
		}

	}
}