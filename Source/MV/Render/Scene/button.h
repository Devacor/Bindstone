#ifndef _MV_SCENE_BUTTON_H_
#define _MV_SCENE_BUTTON_H_

#include "clickable.h"
#include "text.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {

		class Button : public jai::property_owner<Button, Clickable> {
			friend jai::access;
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

			template<class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				property_mgr.save(archive);
				Clickable::save(archive, 0);
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				property_mgr.load(archive);
				Clickable::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Button>& construct) {
				auto* services = ar.template get_user_context<MV::Services>();
				construct(std::shared_ptr<Node>(), *services->template get<MV::TapDevice>());
				construct->load(ar, 0);
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
			JAI_PROPERTY((std::shared_ptr<Node>), activeView, nullptr, [](auto&, auto&) {});
			JAI_PROPERTY((std::shared_ptr<Node>), idleView, nullptr, [](auto&, auto&) {});
			JAI_PROPERTY((std::shared_ptr<Node>), disabledView, nullptr, [](auto&, auto&) {});
		};
	}
}

#endif
