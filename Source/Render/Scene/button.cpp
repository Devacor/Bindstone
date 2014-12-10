#include "button.h"
#include "cereal/archives/json.hpp"

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
			auto self = shared_from_this(); //guard against deletion
			Clickable::acceptDownClick();
			if (clickDetectionType() != Clickable::BoundsType::NONE) {
				setCurrentView(activeView);
			}
		}

		void Button::acceptUpClick() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto self = shared_from_this(); //guard against deletion
			Clickable::acceptUpClick();
			if (clickDetectionType() != Clickable::BoundsType::NONE) {
				setCurrentView(idleView);
			}
		}

		void Button::setCurrentView(const std::shared_ptr<Node> &a_view) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			currentView = a_view;

			showOrHideView(activeView);
			showOrHideView(idleView);
			showOrHideView(disabledView);
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

	}
}
