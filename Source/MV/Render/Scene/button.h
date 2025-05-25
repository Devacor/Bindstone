#ifndef _MV_SCENE_BUTTON_H_
#define _MV_SCENE_BUTTON_H_

#include "clickable.h"
#include "text.h"

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
			Button(const std::weak_ptr<Node> &a_owner, TapDevice &a_mouse);

			void text(const std::shared_ptr<Node> &a_owner, const std::string &a_newValue);

			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				archive(
					cereal::make_nvp("Clickable", cereal::base_class<Clickable>(this))
				);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				if (version == 0) {
					properties.load(archive, {"activeView", "idleView", "disabledView"});
				}
				archive(
					cereal::make_nvp("Clickable", cereal::base_class<Clickable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Button> &construct, std::uint32_t const version) {
				MV::Services& services = cereal::get_user_data<MV::Services>(archive);
				auto* mouse = services.get<MV::TapDevice>();
				construct(std::shared_ptr<Node>(), *mouse);
				construct->load(archive, version);
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

			//Clone is a no-op because it is manually managed in the cloneHelper
			MV_PROPERTY((std::shared_ptr<Node>), activeView, {}, [](auto&, auto&) {});
			MV_PROPERTY((std::shared_ptr<Node>), idleView, {}, [](auto&, auto&) {});
			MV_PROPERTY((std::shared_ptr<Node>), disabledView, {}, [](auto&, auto&){ });
		};
	}
}

CEREAL_FORCE_DYNAMIC_INIT(mv_scenebutton);

#endif
