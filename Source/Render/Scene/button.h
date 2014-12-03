#ifndef _MV_SCENE_BUTTON_H_
#define _MV_SCENE_BUTTON_H_

#include "clickable.h"

namespace MV {
	namespace Scene {

		class Button : public Clickable {
			friend cereal::access;
			friend Node;
		public:
			ClickableComponentDerivedAccessors(Button)

			std::shared_ptr<Node> activeNode() {
				return activeView;
			}

			std::shared_ptr<Node> idleNode() {
				return idleView;
			}

			std::shared_ptr<Node> disabledNode() {
				return disabledView;
			}

			std::shared_ptr<Button> activeNode(const std::shared_ptr<Node> &a_activeView) {
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
			std::shared_ptr<Button> idleNode(const std::shared_ptr<Node> &a_idleView) {
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
			std::shared_ptr<Button> disabledNode(const std::shared_ptr<Node> &a_disabledView) {
				std::lock_guard<std::recursive_mutex> guard(lock);
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
				return std::static_pointer_cast<Button>(shared_from_this());
			}

		protected:
			Button(const std::weak_ptr<Node> &a_owner, MouseState &a_mouse) :
				Clickable(a_owner, a_mouse) {

				onEnabled.connect(guid(), [&](const std::shared_ptr<Clickable> &a_node) {
					if (!inPressEvent()) {
						setCurrentView(idleView);
					}
					else {
						setCurrentView(activeView);
					}
				});

				onDisabled.connect(guid(), [&](const std::shared_ptr<Clickable> &a_node) {
					setCurrentView(disabledView);
				});
			}


			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					CEREAL_NVP(activeView),
					CEREAL_NVP(idleView),
					CEREAL_NVP(disabledView),
					cereal::make_nvp("Clickable", cereal::base_class<Clickable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Button> &construct) {
				MouseState *mouse = nullptr;
				archive.extract(cereal::make_nvp("mouse", mouse));
				MV::require<PointerException>(mouse != nullptr, "Null mouse in Button::load_and_construct.");
				construct(std::shared_ptr<Node>(), *mouse);
				archive(
					cereal::make_nvp("activeView", construct->activeView),
					cereal::make_nvp("idleView", construct->idleView),
					cereal::make_nvp("disabledView", construct->disabledView),
					cereal::make_nvp("Clickable", cereal::base_class<Clickable>(construct.ptr()))
				);
			}

		private:
			virtual void acceptDownClick() {
				std::lock_guard<std::recursive_mutex> guard(lock);
				Clickable::acceptDownClick();
				if (clickDetectionType() != Clickable::BoundsType::NONE) {
					setCurrentView(activeView);
				}
			}

			virtual void acceptUpClick() {
				std::lock_guard<std::recursive_mutex> guard(lock);
				Clickable::acceptUpClick();
				if (clickDetectionType() != Clickable::BoundsType::NONE) {
					setCurrentView(idleView);
				}
			}

			void setCurrentView(const std::shared_ptr<Node> &a_view) {
				std::lock_guard<std::recursive_mutex> guard(lock);
				currentView = a_view;

				showOrHideView(activeView);
				showOrHideView(idleView);
				showOrHideView(disabledView);
			}

			void showOrHideView(const std::shared_ptr<Node> &a_view) {
				if (a_view) {
					if (a_view == currentView) {
						a_view->show();
					}
					else {
						a_view->hide();
					}
				}
			}

			std::shared_ptr<Node> currentView;

			std::shared_ptr<Node> activeView;
			std::shared_ptr<Node> idleView;
			std::shared_ptr<Node> disabledView;
		};

	}
}

#endif
