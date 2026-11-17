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
			a_builder.method("defaultFocus", static_cast<std::string(FocusGroup::*)() const>(&FocusGroup::defaultFocus));
			a_builder.method("defaultFocus", static_cast<std::shared_ptr<FocusGroup>(FocusGroup::*)(const std::string&)>(&FocusGroup::defaultFocus), jai::serial);
			a_builder.method("remembered", &FocusGroup::remembered);
			a_builder.method("back", &FocusGroup::back, jai::serial);
		}

		FocusGroup::FocusGroup(const std::weak_ptr<Node>& a_owner, TapDevice& a_mouse) :
			jai::property_owner<FocusGroup, Component>(a_owner),
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

		std::shared_ptr<FocusGroup> FocusGroup::defaultFocus(const std::string& a_nodeId) {
			defaultFocusId = a_nodeId;
			return std::static_pointer_cast<FocusGroup>(shared_from_this());
		}

		void FocusGroup::back() {
			onBackSignal(std::static_pointer_cast<FocusGroup>(shared_from_this()));
		}

		void FocusGroup::initialize() {
			answerGroupQueries();
		}

		void FocusGroup::detachImplementation() {
			onGroupQueryHandle.reset();
		}

		void FocusGroup::reattachImplementation() {
			answerGroupQueries();
		}

		std::shared_ptr<Component> FocusGroup::cloneHelper(const std::shared_ptr<Component>& a_clone) {
			Component::cloneHelper(a_clone);
			return a_clone;
		}

		bool FocusGroup::holds(const std::shared_ptr<Clickable>& a_clickable) const {
			if (!a_clickable || !a_clickable->ownerIsAlive() || !ownerIsAlive()) {
				return false;
			}
			auto groupNode = owner();
			for (auto node = a_clickable->owner(); node; node = node->parent()) {
				if (node == groupNode) {
					return true;
				}
			}
			return false;
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

		// The one remembered, else the default, when it can still take the focus here.
		bool FocusGroup::focusPreferred() {
			const void* scope = owner().get();
			auto last = lastFocused.lock();
			if (last && holds(last) && last->canTakeFocus(scope)) {
				last->focus();
				return true;
			}
			if (!defaultFocusId->empty()) {
				auto named = owner()->get(*defaultFocusId, false);
				auto clickable = named ? named->component<Clickable>(false, false) : SafeComponent<Clickable>();
				if (clickable && clickable->canTakeFocus(scope)) {
					clickable->focus();
					return true;
				}
			}
			return false;
		}

		void FocusGroup::answerGroupQueries() {
			onGroupQueryHandle = ourMouse.onFocusGroupQuery.connect([this](TapDevice& a_mouse) {
				if (!ownerIsAlive() || !owner()->visible() || !under(a_mouse.focusScope())) {
					return;
				}
				std::weak_ptr<FocusGroup> weakSelf = std::static_pointer_cast<FocusGroup>(shared_from_this());
				FocusGroupCandidate result;
				result.tabs = groupKind == Kind::TABS;
				result.modal = isModal && groupKind == Kind::WINDOW;
				result.order = groupOrder;
				result.treePath = owner()->parentIndexList(0);
				result.scope = owner().get();
				result.subject = weakSelf;
				result.contains = [weakSelf](const std::shared_ptr<void>& a_focused) {
					auto self = weakSelf.lock();
					return self && self->holds(std::static_pointer_cast<Clickable>(a_focused));
				};
				result.within = [weakSelf](const void* a_scope) {
					auto self = weakSelf.lock();
					return self && self->ownerIsAlive() && self->under(a_scope);
				};
				result.focusPreferred = [weakSelf]() {
					auto self = weakSelf.lock();
					return self && self->ownerIsAlive() && self->focusPreferred();
				};
				result.remember = [weakSelf](const std::shared_ptr<void>& a_focused) {
					if (auto self = weakSelf.lock()) {
						self->lastFocused = std::static_pointer_cast<Clickable>(a_focused);
					}
				};
				result.remembered = [weakSelf]() -> std::shared_ptr<void> {
					auto self = weakSelf.lock();
					return self ? self->lastFocused.lock() : nullptr;
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
