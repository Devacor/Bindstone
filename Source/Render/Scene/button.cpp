#include "button.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

#include "text.h"

CEREAL_REGISTER_TYPE(MV::Scene::Button);

namespace MV {
	namespace Scene {

		Button::Button(const std::weak_ptr<Node> &a_owner, MouseState &a_mouse) :
			Clickable(a_owner, a_mouse) {
			onEnabled.connect(guid(), [&](const std::shared_ptr<Clickable> &a_node) {
				if (!inPressEvent()) {
					setCurrentView(idleView);
				} else {
					setCurrentView(activeView);
				}
			});

			onDisabled.connect(guid(), [&](const std::shared_ptr<Clickable> &a_node) {
				setCurrentView(disabledView);
			});
		}

		std::shared_ptr<Button> Button::activeNode(const std::shared_ptr<Node> &a_activeView) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			if (activeView) {
				activeView->show();
			}
			activeView = a_activeView;
			if (activeView) {
				if (inPressEvent() && enabled()) {
					currentView = activeView;
					activeView->show();
				} else {
					activeView->hide();
				}
			}
			return std::static_pointer_cast<Button>(shared_from_this());
		}

		std::shared_ptr<Button> Button::idleNode(const std::shared_ptr<Node> &a_idleView) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			if (idleView) {
				idleView->show();
			}
			idleView = a_idleView;
			if (idleView) {
				if (!inPressEvent() && enabled()) {
					currentView = idleView;
					idleView->show();
				} else {
					idleView->hide();
				}
			}
			return std::static_pointer_cast<Button>(shared_from_this());
		}

		std::shared_ptr<Button> Button::disabledNode(const std::shared_ptr<Node> &a_disabledView) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto self = shared_from_this(); //guard against deletion
			if (disabledView) {
				disabledView->show();
			}
			disabledView = a_disabledView;
			if (disabledView) {
				if (!enabled()) {
					currentView = disabledView;
					disabledView->show();
				} else {
					disabledView->hide();
				}
			}
			return std::static_pointer_cast<Button>(self);
		}

		void Button::acceptDownClick() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto self = shared_from_this(); //guard self against deletion
			Clickable::acceptDownClick(); //this may still pull the "node" rug out from under us, hence the ownerIsAlive check.
			if (clickDetectionType() != Clickable::BoundsType::NONE && ownerIsAlive()) {
				setCurrentView(activeView);
			}
		}

		void Button::acceptUpClick(bool a_ignoreBounds) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto self = shared_from_this();
			Clickable::acceptUpClick(a_ignoreBounds);
			if (clickDetectionType() != Clickable::BoundsType::NONE && ownerIsAlive()) {
				setCurrentView(idleView);
			}
		}

		void Button::cancelPress(bool a_callCancelCallbacks) {
			if (inPressEvent()) {
				auto self = std::static_pointer_cast<Button>(shared_from_this());
				Clickable::cancelPress(a_callCancelCallbacks);
				if (clickDetectionType() != Clickable::BoundsType::NONE && ownerIsAlive()) {
					setCurrentView(idleView);
				}
			}
		}

		void Button::setCurrentView(const std::shared_ptr<Node> &a_view) {
			if (currentView != a_view) {
				std::lock_guard<std::recursive_mutex> guard(lock);
				currentView = a_view;

				showOrHideView(activeView);
				showOrHideView(idleView);
				showOrHideView(disabledView);

				notifyParentOfComponentChange();
			}
		}

		void Button::showOrHideView(const std::shared_ptr<Node> &a_view) {
			if (a_view) {
				if (a_view == currentView) {
					a_view->show();
				} else {
					a_view->hide();
				}
			}
		}

		std::shared_ptr<Component> Button::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Clickable::cloneHelper(a_clone);
			auto buttonClone = std::static_pointer_cast<Button>(a_clone);
			if (activeView) {
				auto foundHandle = buttonClone->owner()->get(activeView->id());
				buttonClone->activeNode(foundHandle);
			}
			if (idleView) {
				auto foundHandle = buttonClone->owner()->get(idleView->id());
				buttonClone->idleNode(foundHandle);
			}
			if (disabledView) {
				auto foundHandle = buttonClone->owner()->get(disabledView->id());
				buttonClone->disabledNode(foundHandle);
			}
			return a_clone;
		}

		std::shared_ptr<Button> Button::text(const std::string &a_textString){
			auto self = std::static_pointer_cast<Button>(shared_from_this());
			auto textComponents = owner()->componentsInChildren<MV::Scene::Text>(false);
			MV::visit_each(textComponents, [&](const MV::Scene::SafeComponent<MV::Scene::Text> &a_text) {
				a_text->text(a_textString);
			});
			return self;
		}

		std::string Button::text() const {
			auto textComponents = owner()->componentInChildren<MV::Scene::Text>(false, false);
			return textComponents ? textComponents->text() : "";
		}

	}
}
