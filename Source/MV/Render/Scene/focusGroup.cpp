#include "focusGroup.h"
#include <jaiscript/signals/signal_impl.hpp>
#include <jaiscript/core/dynamic_binder.hpp>

namespace MV {
	namespace Scene {

		void FocusGroup::jai_auto_bind(jai::dynamic_binder<FocusGroup>& a_builder) {
			a_builder.method("window", [](FocusGroup& a_self) { return a_self.kind() == Kind::WINDOW; });
			a_builder.method("tabs", [](FocusGroup& a_self) { return a_self.kind() == Kind::TABS; });
			a_builder.method("order", static_cast<int64_t(FocusGroup::*)() const>(&FocusGroup::order));
			a_builder.method("order", static_cast<std::shared_ptr<FocusGroup>(FocusGroup::*)(int64_t)>(&FocusGroup::order), jai::serial);
			a_builder.method("modal", static_cast<bool(FocusGroup::*)() const>(&FocusGroup::modal));
			a_builder.method("modal", static_cast<std::shared_ptr<FocusGroup>(FocusGroup::*)(bool)>(&FocusGroup::modal), jai::serial);
			a_builder.method("explicitSelect", static_cast<bool(FocusGroup::*)() const>(&FocusGroup::explicitSelect));
			a_builder.method("explicitSelect", static_cast<std::shared_ptr<FocusGroup>(FocusGroup::*)(bool)>(&FocusGroup::explicitSelect), jai::serial);
			a_builder.method("defaultFocus", static_cast<std::string(FocusGroup::*)() const>(&FocusGroup::defaultFocus));
			a_builder.method("defaultFocus", static_cast<std::shared_ptr<FocusGroup>(FocusGroup::*)(const std::string&)>(&FocusGroup::defaultFocus), jai::serial);
			a_builder.method("highlightNode", static_cast<std::shared_ptr<Node>(FocusGroup::*)() const>(&FocusGroup::highlightNode));
			a_builder.method("highlightNode", static_cast<std::shared_ptr<FocusGroup>(FocusGroup::*)(const std::shared_ptr<Node>&)>(&FocusGroup::highlightNode), jai::serial);
			a_builder.method("remembered", &FocusGroup::remembered);
			a_builder.method("selected", &FocusGroup::selected);
			a_builder.method("select", &FocusGroup::select, jai::serial);
			a_builder.method("enter", &FocusGroup::enter, jai::serial);
			a_builder.method("back", &FocusGroup::back, jai::serial);
			a_builder.transient_property("onHighlightEnter", &FocusGroup::onHighlightEnter);
			a_builder.transient_property("onHighlightLeave", &FocusGroup::onHighlightLeave);
		}

		FocusGroup::FocusGroup(const std::weak_ptr<Node>& a_owner, TapDevice& a_mouse) :
			jai::property_owner<FocusGroup, Component>(a_owner),
			onHighlightEnter(onHighlightEnterSignal),
			onHighlightLeave(onHighlightLeaveSignal),
			ourMouse(a_mouse) {
		}

		std::shared_ptr<FocusGroup> FocusGroup::kind(Kind a_kind) {
			groupKind = a_kind;
			return std::static_pointer_cast<FocusGroup>(shared_from_this());
		}

		std::shared_ptr<FocusGroup> FocusGroup::order(int64_t a_order) {
			groupOrder = a_order;
			return std::static_pointer_cast<FocusGroup>(shared_from_this());
		}

		std::shared_ptr<FocusGroup> FocusGroup::modal(bool a_modal) {
			isModal = a_modal;
			return std::static_pointer_cast<FocusGroup>(shared_from_this());
		}

		std::shared_ptr<FocusGroup> FocusGroup::explicitSelect(bool a_explicit) {
			isExplicit = a_explicit;
			return std::static_pointer_cast<FocusGroup>(shared_from_this());
		}

		std::shared_ptr<FocusGroup> FocusGroup::defaultFocus(const std::string& a_nodeId) {
			defaultFocusId = a_nodeId;
			return std::static_pointer_cast<FocusGroup>(shared_from_this());
		}

		std::shared_ptr<FocusGroup> FocusGroup::highlightNode(const std::shared_ptr<Node>& a_highlightView) {
			auto self = std::static_pointer_cast<FocusGroup>(shared_from_this());
			if (*highlightView) {
				highlightView->show();
			}
			highlightView = a_highlightView;
			if (*highlightView) {
				highlightView->visible(isSelected);
			}
			return self;
		}

		std::shared_ptr<Clickable> FocusGroup::remembered() const {
			if (!lastFocused || lastFocused->group) {
				return nullptr;
			}
			return std::static_pointer_cast<Clickable>(lastFocused->subject.lock());
		}

		void FocusGroup::select() {
			if (selectable(nullptr)) {
				ourMouse.focus(selfCandidate());
			}
		}

		void FocusGroup::enter() {
			if (ownerIsAlive()) {
				ourMouse.focusEnter(owner().get());
			}
		}

		void FocusGroup::back() {
			onBackSignal(std::static_pointer_cast<FocusGroup>(shared_from_this()));
		}

		bool FocusGroup::handlesBack() const {
			return onBack.observer_count() > 0;
		}

		void FocusGroup::initialize() {
			answerQueries();
		}

		void FocusGroup::postLoadInitialize() {
			// nothing is selected in a scene that has only just loaded
			if (*highlightView) {
				highlightView->hide();
			}
		}

		void FocusGroup::detachImplementation() {
			onGroupQueryHandle.reset();
			onFocusQueryHandle.reset();
		}

		void FocusGroup::reattachImplementation() {
			answerQueries();
		}

		std::shared_ptr<Component> FocusGroup::cloneHelper(const std::shared_ptr<Component>& a_clone) {
			Component::cloneHelper(a_clone);
			auto groupClone = std::static_pointer_cast<FocusGroup>(a_clone);
			if (*highlightView) {
				groupClone->highlightNode(groupClone->owner()->get(highlightView->id(), false));
			}
			return a_clone;
		}

		bool FocusGroup::under(const void* a_scope) const {
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

		// As one stop for a move made at a_scope's level: never from inside itself.
		bool FocusGroup::selectable(const void* a_scope) const {
			return isExplicit && !isModal && groupKind == Kind::WINDOW && ownerIsAlive() && owner()->visible() && owner().get() != a_scope && under(a_scope);
		}

		void FocusGroup::selectedChanged(bool a_selected) {
			if (isSelected == a_selected) {
				return;
			}
			isSelected = a_selected;
			auto self = std::static_pointer_cast<FocusGroup>(shared_from_this());
			if (*highlightView) {
				highlightView->visible(isSelected);
			}
			if (isSelected) {
				onHighlightEnterSignal(self);
			} else {
				onHighlightLeaveSignal(self);
			}
		}

		FocusCandidate FocusGroup::selfCandidate() {
			std::weak_ptr<FocusGroup> weakSelf = std::static_pointer_cast<FocusGroup>(shared_from_this());
			FocusCandidate result;
			result.group = true;
			result.order = groupOrder;
			result.treePath = owner()->parentIndexList(0);
			auto box = owner()->screenBounds();
			result.screenMin = box.minPoint;
			result.screenMax = box.maxPoint;
			result.subject = weakSelf;
			result.enter = [weakSelf]() { if (auto self = weakSelf.lock()) { self->selectedChanged(true); } };
			result.leave = [weakSelf]() { if (auto self = weakSelf.lock()) { self->selectedChanged(false); } };
			result.activate = [weakSelf]() { if (auto self = weakSelf.lock()) { self->enter(); } };
			result.canTake = [weakSelf](const void* a_scope) {
				auto self = weakSelf.lock();
				return self && self->selectable(a_scope);
			};
			result.under = [weakSelf](const void* a_scope) {
				auto self = weakSelf.lock();
				return self && self->ownerIsAlive() && self->under(a_scope);
			};
			return result;
		}

		// The one remembered, else the default, when it can still take the focus here.
		bool FocusGroup::focusPreferred() {
			const void* scope = owner().get();
			if (lastFocused && !lastFocused->subject.expired() && lastFocused->canTake && lastFocused->canTake(scope)) {
				auto last = *lastFocused;
				ourMouse.focus(std::move(last));
				return true;
			}
			if (!defaultFocusId->empty()) {
				auto named = owner()->get(*defaultFocusId, false);
				if (named) {
					auto group = named->component<FocusGroup>(false, false);
					if (group && group->selectable(scope)) {
						group->select();
						return true;
					}
					auto clickable = named->component<Clickable>(false, false);
					if (clickable && clickable->canTakeFocus(scope)) {
						clickable->focus();
						return true;
					}
				}
			}
			return false;
		}

		void FocusGroup::answerQueries() {
			onFocusQueryHandle = ourMouse.onFocusQuery.connect([this](TapDevice& a_mouse) {
				if (selectable(a_mouse.focusScope())) {
					a_mouse.queueFocusCandidate(selfCandidate());
				}
			});
			onGroupQueryHandle = ourMouse.onFocusGroupQuery.connect([this](TapDevice& a_mouse) {
				if (!ownerIsAlive() || !owner()->visible() || !under(a_mouse.focusScope())) {
					return;
				}
				std::weak_ptr<FocusGroup> weakSelf = std::static_pointer_cast<FocusGroup>(shared_from_this());
				FocusGroupCandidate result;
				result.tabs = groupKind == Kind::TABS;
				result.modal = isModal && groupKind == Kind::WINDOW;
				result.explicitSelect = selectable(nullptr);
				result.order = groupOrder;
				result.treePath = owner()->parentIndexList(0);
				result.scope = owner().get();
				result.subject = weakSelf;
				result.within = [weakSelf](const void* a_scope) {
					auto self = weakSelf.lock();
					return self && self->ownerIsAlive() && self->under(a_scope);
				};
				result.focusPreferred = [weakSelf]() {
					auto self = weakSelf.lock();
					return self && self->ownerIsAlive() && self->focusPreferred();
				};
				result.focusSelf = [weakSelf]() {
					if (auto self = weakSelf.lock()) {
						self->select();
					}
				};
				result.remember = [weakSelf](const FocusCandidate& a_focused) {
					if (auto self = weakSelf.lock()) {
						self->lastFocused = a_focused;
					}
				};
				result.remembered = [weakSelf]() -> std::weak_ptr<void> {
					auto self = weakSelf.lock();
					return self && self->lastFocused ? self->lastFocused->subject : std::weak_ptr<void>();
				};
				result.handlesBack = [weakSelf]() {
					auto self = weakSelf.lock();
					return self && self->handlesBack();
				};
				result.back = [weakSelf]() {
					if (auto self = weakSelf.lock()) {
						self->back();
					}
				};
				a_mouse.queueFocusGroupCandidate(std::move(result));
			});
		}

	}
}
