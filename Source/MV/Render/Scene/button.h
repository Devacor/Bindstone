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

			static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script, TapDevice &a_tapDevice) {
				a_script.add(chaiscript::user_type<Button>(), "Button");
				a_script.add(chaiscript::base_class<Clickable, Button>());
				a_script.add(chaiscript::base_class<Sprite, Button>());
				a_script.add(chaiscript::base_class<Drawable, Button>());
				a_script.add(chaiscript::base_class<Component, Button>());

				a_script.add(chaiscript::fun([&](Node &a_self) { return a_self.attach<Button>(a_tapDevice); }), "attachButton");

				a_script.add(chaiscript::fun([](Node &a_self) { return a_self.componentInChildren<Button>(true, false, true); }), "componentButton");

				a_script.add(chaiscript::fun([](Button &a_self, const std::string &a_newValue) { return a_self.text(a_newValue); }), "text");
				a_script.add(chaiscript::fun([](Button &a_self) { return a_self.text(); }), "text");

				a_script.add(chaiscript::fun([](Button &a_self, const std::shared_ptr<Node> &a_activeView) { return a_self.activeNode(a_activeView); }), "activeNode");
				a_script.add(chaiscript::fun([](Button &a_self) { return a_self.activeNode(); }), "activeNode");

				a_script.add(chaiscript::fun([](Button &a_self, const std::shared_ptr<Node> &a_disabledView) { return a_self.disabledNode(a_disabledView); }), "disabledNode");
				a_script.add(chaiscript::fun([](Button &a_self) { return a_self.disabledNode(); }), "disabledNode");

				a_script.add(chaiscript::fun([](Button &a_self, const std::shared_ptr<Node> &a_idleView) { return a_self.idleNode(a_idleView); }), "idleNode");
				a_script.add(chaiscript::fun([](Button &a_self) { return a_self.idleNode(); }), "idleNode");

				a_script.add(chaiscript::type_conversion<SafeComponent<Button>, std::shared_ptr<Button>>([](const SafeComponent<Button> &a_item) { return a_item.self(); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Button>, std::shared_ptr<Clickable>>([](const SafeComponent<Button> &a_item) { return std::static_pointer_cast<Clickable>(a_item.self()); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Button>, std::shared_ptr<Sprite>>([](const SafeComponent<Button> &a_item) { return std::static_pointer_cast<Sprite>(a_item.self()); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Button>, std::shared_ptr<Drawable>>([](const SafeComponent<Button> &a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Button>, std::shared_ptr<Component>>([](const SafeComponent<Button> &a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));

				return a_script;
			}

		protected:
			Button(const std::weak_ptr<Node> &a_owner, TapDevice &a_mouse);

			void text(const std::shared_ptr<Node> &a_owner, const std::string &a_newValue);

			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				archive(
					CEREAL_NVP(activeView),
					CEREAL_NVP(idleView),
					CEREAL_NVP(disabledView),
					cereal::make_nvp("Clickable", cereal::base_class<Clickable>(this))
				);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const /*version*/) {
				archive(
					CEREAL_NVP(activeView),
					CEREAL_NVP(idleView),
					CEREAL_NVP(disabledView),
					cereal::make_nvp("Clickable", cereal::base_class<Clickable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Button> &construct, std::uint32_t const version) {
				auto& services = cereal::get_user_data<MV::Services>(archive);
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

			std::shared_ptr<Node> activeView;
			std::shared_ptr<Node> idleView;
			std::shared_ptr<Node> disabledView;
		};

	}
}

CEREAL_FORCE_DYNAMIC_INIT(mv_scenebutton);

#endif
