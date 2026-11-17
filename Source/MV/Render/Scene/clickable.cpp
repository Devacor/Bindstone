#include "clickable.h"
#include <cstdlib>
#include <iostream>
#include "clipped.h"
#include "stencil.h"

#include <jaiscript/core/dynamic_binder.hpp>

namespace MV {
	namespace Scene {
		void Clickable::jai_auto_bind(jai::dynamic_binder<Clickable>& a_builder) {
			a_builder.method("clickDetectionType", static_cast<Clickable::BoundsType(Clickable::*)() const>(&Clickable::clickDetectionType));
			a_builder.method("clickDetectionType", static_cast<std::shared_ptr<Clickable>(Clickable::*)(Clickable::BoundsType)>(&Clickable::clickDetectionType), jai::serial);

			a_builder.method("disable", &Clickable::disable, jai::serial);
			a_builder.method("disabled", &Clickable::disabled);
			a_builder.method("enabled", &Clickable::enabled);

			a_builder.method("cancelPress", &Clickable::cancelPress, jai::serial);
			a_builder.method("inPressEvent", &Clickable::inPressEvent);
			a_builder.method("press", &Clickable::press, jai::serial);

			a_builder.method("startEatingTouches", &Clickable::startEatingTouches);
			a_builder.method("stopEatingTouches", &Clickable::stopEatingTouches);
			a_builder.method("eatingTouches", &Clickable::eatingTouches);

			a_builder.method("dragDelta", &Clickable::dragDelta);
			a_builder.method("totalDragDistance", &Clickable::totalDragDistance);
			a_builder.method("dragTime", &Clickable::dragTime);

			a_builder.method("globalPriority", static_cast<int64_t(Clickable::*)() const>(&Clickable::globalPriority));
			a_builder.method("globalPriority", static_cast<std::shared_ptr<Clickable>(Clickable::*)(int64_t)>(&Clickable::globalPriority));
			a_builder.method("appendPriority", static_cast<int64_t(Clickable::*)() const>(&Clickable::appendPriority));
			a_builder.method("appendPriority", static_cast<std::shared_ptr<Clickable>(Clickable::*)(int64_t)>(&Clickable::appendPriority));

			a_builder.method("mouseInBounds", static_cast<bool(Clickable::*)()>(&Clickable::mouseInBounds), jai::serial);
			a_builder.method("hovered", &Clickable::hovered);
			a_builder.method("highlighted", &Clickable::highlighted);
			a_builder.method("focusable", static_cast<bool(Clickable::*)() const>(&Clickable::focusable));
			a_builder.method("focusable", static_cast<std::shared_ptr<Clickable>(Clickable::*)(bool)>(&Clickable::focusable), jai::serial);
			a_builder.method("focusOrder", static_cast<int64_t(Clickable::*)() const>(&Clickable::focusOrder));
			a_builder.method("focusOrder", static_cast<std::shared_ptr<Clickable>(Clickable::*)(int64_t)>(&Clickable::focusOrder), jai::serial);
			a_builder.method("focus", &Clickable::focus, jai::serial);
			a_builder.method("focusLink", static_cast<std::shared_ptr<Clickable>(Clickable::*)(const std::string&, const std::string&)>(&Clickable::focusLink), jai::serial);
			a_builder.method("focused", &Clickable::focused);
		}


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
				onDisabledSignal(self);
			} else if (hitDetectionType == BoundsType::NONE && a_type != BoundsType::NONE) {
				onEnabledSignal(self);
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
			jai::property_owner<Clickable, Sprite>(a_owner),
			ourMouse(a_mouse),
			onEnabled(onEnabledSignal),
			onDisabled(onDisabledSignal),
			onHoverEnter(onHoverEnterSignal),
			onHoverLeave(onHoverLeaveSignal),
			onFocusEnter(onFocusEnterSignal),
			onFocusLeave(onFocusLeaveSignal),
			onHighlightEnter(onHighlightEnterSignal),
			onHighlightLeave(onHighlightLeaveSignal) {

			shouldDraw = false;
		}

		// MV_CLICK_DIAG=1: trace every press/release decision (which clickable saw it, in bounds or not)
		static bool clickDiag() {
			static const bool enabled = std::getenv("MV_CLICK_DIAG") != nullptr;
			return enabled;
		}

		void Clickable::initialize() {
			Sprite::initialize();
			onLeftMouseDownHandle = ourMouse.onLeftMouseDown.connect([&](TapDevice& a_mouse) {
				bool inBounds = mouseInBounds(a_mouse);
				if (clickDiag() && ownerIsAlive() && owner()->visible()) { std::cerr << "[click] down " << owner()->id() << " at " << a_mouse.position().x << "," << a_mouse.position().y << (inBounds ? " IN" : " out") << std::endl; }
				if (inBounds) {
					a_mouse.queueExclusiveAction({ eatTouches, clickPriority(), [&]() {
						acceptDownClick();
					}, []() {}, owner()->id() });
				}
			});

			onLeftMouseUpHandle = ourMouse.onLeftMouseUp.connect([&](TapDevice& a_mouse) {
				acceptUpClick();
			});

			onFocusQueryHandle = ourMouse.onFocusQuery.connect([&](TapDevice& a_mouse) {
				if (canTakeFocus(a_mouse.focusScope())) {
					a_mouse.queueFocusCandidate(focusCandidate());
				}
			});

			onHoverQueryHandle = ourMouse.onHoverQuery.connect([&](TapDevice& a_mouse) {
				if (mouseInBounds(a_mouse)) {
					std::weak_ptr<Clickable> weakSelf = std::static_pointer_cast<Clickable>(shared_from_this());
					a_mouse.queueHoverCandidate({ clickPriority(), eatTouches, weakSelf,
						[weakSelf]() { if (auto self = weakSelf.lock()) { self->hover(true); } },
						[weakSelf]() { if (auto self = weakSelf.lock()) { self->hover(false); } } });
				}
			});
		}

		std::shared_ptr<Clickable> Clickable::focusable(bool a_focusable) {
			isFocusable = a_focusable;
			return std::static_pointer_cast<Clickable>(shared_from_this());
		}

		std::shared_ptr<Clickable> Clickable::focusOrder(int64_t a_order) {
			focusOrderValue = a_order;
			return std::static_pointer_cast<Clickable>(shared_from_this());
		}

		std::shared_ptr<Clickable> Clickable::pressOnFocus(bool a_press) {
			pressesOnFocus = a_press;
			return std::static_pointer_cast<Clickable>(shared_from_this());
		}

		const std::string& Clickable::focusLinkName(FocusLink a_link) {
			static const std::string names[] { "next", "previous", "left", "right", "up", "down", "upLeft", "upRight", "downLeft", "downRight" };
			return names[static_cast<size_t>(a_link)];
		}

		std::shared_ptr<Clickable> Clickable::focusLink(FocusLink a_link, const std::string& a_nodeId) {
			if (a_nodeId.empty()) {
				focusLinkIds->erase(focusLinkName(a_link));
			} else {
				(*focusLinkIds)[focusLinkName(a_link)] = a_nodeId;
			}
			return std::static_pointer_cast<Clickable>(shared_from_this());
		}

		std::string Clickable::focusLink(FocusLink a_link) const {
			auto found = focusLinkIds->find(focusLinkName(a_link));
			return found == focusLinkIds->end() ? std::string() : found->second;
		}

		std::shared_ptr<Clickable> Clickable::focusLink(const std::string& a_linkName, const std::string& a_nodeId) {
			for (int link = 0; link <= static_cast<int>(FocusLink::DOWN_RIGHT); ++link) {
				if (focusLinkName(static_cast<FocusLink>(link)) == a_linkName) {
					return focusLink(static_cast<FocusLink>(link), a_nodeId);
				}
			}
			require<RangeException>(false, "No focus link is named [", a_linkName, "]");
			return std::static_pointer_cast<Clickable>(shared_from_this());
		}

		bool Clickable::canTakeFocus(const void* a_scope) const {
			return isFocusable && ownerIsAlive() && owner()->visible() && enabled() && underFocusScope(a_scope);
		}

		namespace {
			FocusLink oppositeFocusLink(FocusLink a_link) {
				switch (a_link) {
					case FocusLink::NEXT:
						return FocusLink::PREVIOUS;
					case FocusLink::PREVIOUS:
						return FocusLink::NEXT;
					case FocusLink::LEFT:
						return FocusLink::RIGHT;
					case FocusLink::RIGHT:
						return FocusLink::LEFT;
					case FocusLink::UP:
						return FocusLink::DOWN;
					case FocusLink::DOWN:
						return FocusLink::UP;
					case FocusLink::UP_LEFT:
						return FocusLink::DOWN_RIGHT;
					case FocusLink::UP_RIGHT:
						return FocusLink::DOWN_LEFT;
					case FocusLink::DOWN_LEFT:
						return FocusLink::UP_RIGHT;
					default:
						return FocusLink::UP_LEFT;
				}
			}
		}

		bool Clickable::focusNeighbour(FocusLink a_link, const void* a_scope, FocusCandidate& a_neighbour) {
			if (!ownerIsAlive()) {
				return false;
			}
			auto named = focusLink(a_link);
			auto ourId = owner()->id();
			auto opposite = oppositeFocusLink(a_link);
			auto top = owner();
			while (top->parent()) {
				top = top->parent();
			}
			// the one this Clickable names, or failing that one that names this Clickable for the way back
			std::shared_ptr<Clickable> found;
			std::shared_ptr<Clickable> namesUs;
			std::function<void(const std::shared_ptr<Node>&)> search = [&](const std::shared_ptr<Node>& a_node) {
				if (found) {
					return;
				}
				auto clickable = a_node->component<Clickable>(false, false);
				if (clickable && clickable.self().get() != this && clickable->canTakeFocus(a_scope)) {
					if (!named.empty() && a_node->id() == named) {
						found = clickable.self();
						return;
					}
					if (!namesUs && !ourId.empty() && clickable->focusLink(opposite) == ourId) {
						namesUs = clickable.self();
					}
				}
				for (auto&& child : *a_node) {
					search(child);
				}
			};
			search(top);
			auto neighbour = found ? found : named.empty() ? namesUs : nullptr;
			if (!neighbour) {
				return false;
			}
			a_neighbour = neighbour->focusCandidate();
			return true;
		}

		bool Clickable::underFocusScope(const void* a_scope) const {
			if (!a_scope) {
				return true;
			}
			for (auto node = owner(); node; node = node->parent()) {
				if (node.get() == a_scope) {
					return true;
				}
			}
			return false;
		}

		FocusCandidate Clickable::focusCandidate() {
			std::weak_ptr<Clickable> weakSelf = std::static_pointer_cast<Clickable>(shared_from_this());
			FocusCandidate result;
			result.order = focusOrderValue;
			result.treePath = owner()->parentIndexList(0);
			auto box = bounds();
			result.screenMin = owner()->screenFromLocal(box.minPoint);
			result.screenMax = owner()->screenFromLocal(box.maxPoint);
			result.subject = weakSelf;
			result.enter = [weakSelf]() { if (auto self = weakSelf.lock()) { self->focusChanged(true); } };
			result.leave = [weakSelf]() { if (auto self = weakSelf.lock()) { self->focusChanged(false); } };
			result.activate = [weakSelf]() { if (auto self = weakSelf.lock()) { self->press(); } };
			result.linked = [weakSelf](FocusLink a_link, const void* a_scope, FocusCandidate& a_neighbour) {
				auto self = weakSelf.lock();
				return self && self->focusNeighbour(a_link, a_scope, a_neighbour);
			};
			result.canTake = [weakSelf](const void* a_scope) {
				auto self = weakSelf.lock();
				return self && self->canTakeFocus(a_scope);
			};
			result.under = [weakSelf](const void* a_scope) {
				auto self = weakSelf.lock();
				return self && self->ownerIsAlive() && self->underFocusScope(a_scope);
			};
			result.pressOnFocus = pressesOnFocus;
			return result;
		}

		void Clickable::focus() {
			if (ownerIsAlive()) {
				ourMouse.focus(focusCandidate());
			}
		}

		void Clickable::focusChanged(bool a_focused) {
			if (isFocused != a_focused) {
				const bool wasHighlighted = highlighted();
				isFocused = a_focused;
				auto self = std::static_pointer_cast<Clickable>(shared_from_this());
				if (isFocused) {
					onFocusEnterSignal(self);
				} else {
					onFocusLeaveSignal(self);
				}
				if (highlighted() != wasHighlighted) {
					(wasHighlighted ? onHighlightLeaveSignal : onHighlightEnterSignal)(self);
				}
			}
		}

		void Clickable::hover(bool a_hovered) {
			if (isHovered != a_hovered) {
				const bool wasHighlighted = highlighted();
				isHovered = a_hovered;
				auto self = std::static_pointer_cast<Clickable>(shared_from_this());
				if (isHovered) {
					onHoverEnterSignal(self);
				} else {
					onHoverLeaveSignal(self);
				}
				if (highlighted() != wasHighlighted) {
					(wasHighlighted ? onHighlightLeaveSignal : onHighlightEnterSignal)(self);
				}
			}
		}

		std::vector<int64_t> Clickable::clickPriority() const {
			if (overrideClickPriority->empty()) {
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
			if (clickDiag() && (inPressEvent() || inBounds)) { std::cerr << "[click] up " << (ownerIsAlive() ? owner()->id() : std::string("?")) << (inBounds ? " IN" : " out") << (inPressEvent() ? " pressed" : " not-pressed") << std::endl; }
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
			}
		}

		void Clickable::acceptDownClick() {
			auto protectFromDismissal = std::static_pointer_cast<Clickable>(shared_from_this());
			if (clickDiag()) { std::cerr << "[click] pressed " << (ownerIsAlive() ? owner()->id() : std::string("?")) << std::endl; }
			isInPressEvent = true;
			// a pointer's press takes the focus too, without pressing again
			if (isFocusable && !isFocused && !pressedByCall && ownerIsAlive()) {
				ourMouse.focus(focusCandidate(), false);
			}
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
			pressedByCall = true;
			acceptDownClick();
			pressedByCall = false;
			acceptUpClick(true);
		}

		std::shared_ptr<Component> Clickable::cloneHelper(const std::shared_ptr<Component>& a_clone) {
			Sprite::cloneHelper(a_clone);
			return a_clone;
		}
	}
}
