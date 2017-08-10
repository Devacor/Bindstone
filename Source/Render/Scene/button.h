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

			std::shared_ptr<Node> activeNode() const {
				return activeView;
			}

			std::shared_ptr<Node> idleNode() const {
				return idleView;
			}

			std::shared_ptr<Node> disabledNode() const {
				return disabledView;
			}

			std::shared_ptr<Button> activeNode(const std::shared_ptr<Node> &a_activeView);
			std::shared_ptr<Button> idleNode(const std::shared_ptr<Node> &a_idleView);
			std::shared_ptr<Button> disabledNode(const std::shared_ptr<Node> &a_disabledView);

			//sets all text components contained.
			std::shared_ptr<Button> text(const std::string &a_newValue);
			//gets the first text value of a component.
			std::string text() const;

			virtual void cancelPress(bool a_callCancelCallbacks = true);

		protected:
			Button(const std::weak_ptr<Node> &a_owner, MouseState &a_mouse);

			void text(const std::shared_ptr<Node> &a_owner, const std::string &a_newValue);

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const /*version*/) {
				archive(
					CEREAL_NVP(activeView),
					CEREAL_NVP(idleView),
					CEREAL_NVP(disabledView),
					cereal::make_nvp("Clickable", cereal::base_class<Clickable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Button> &construct, std::uint32_t const /*version*/) {
				MouseState *mouse = nullptr;
				archive.extract(cereal::make_nvp("mouse", mouse));
                MV::require<MV::PointerException>(mouse != nullptr, "Null mouse in Button::load_and_construct.");
				construct(std::shared_ptr<Node>(), *mouse);
				archive(
					cereal::make_nvp("activeView", construct->activeView),
					cereal::make_nvp("idleView", construct->idleView),
					cereal::make_nvp("disabledView", construct->disabledView),
					cereal::make_nvp("Clickable", cereal::base_class<Clickable>(construct.ptr()))
				);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Button>(mouse()).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

		private:
			virtual void acceptDownClick();

			virtual void acceptUpClick(bool a_ignoreBounds = false);

			void setCurrentView(const std::shared_ptr<Node> &a_view);

			void showOrHideView(const std::shared_ptr<Node> &a_view);

			std::shared_ptr<Node> currentView;

			std::shared_ptr<Node> activeView;
			std::shared_ptr<Node> idleView;
			std::shared_ptr<Node> disabledView;
		};

	}
}

#endif
