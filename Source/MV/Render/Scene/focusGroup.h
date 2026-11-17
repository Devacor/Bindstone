#ifndef _MV_SCENE_FOCUSGROUP_H_
#define _MV_SCENE_FOCUSGROUP_H_

#include "node.h"
#include "clickable.h"
#include "MV/Interface/tapDevice.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {

		// A selection context: the node it is on and everything under it.
		// A WINDOW keeps focus moves inside it while it holds the focus. Stepping between windows (a controller's
		// triggers) goes to the one each remembers, else its defaultFocus, else its first focusable. A modal window
		// takes the focus as it shows and nothing leaves it until it hides, when the focus goes back where it was.
		// TABS marks a strip of tab headers: stepping tabs (a controller's bumpers) presses the next or previous
		// header in it, as a pointer would.
		// Backing out (a controller's east button) emits onBack on the window in use; what that does is its script's.
		class FocusGroup : public jai::property_owner<FocusGroup, Component> {
			friend Node;
			friend jai::access;

		public:
			enum class Kind { WINDOW, TABS };
			typedef void GroupSignalSignature(const std::shared_ptr<FocusGroup>&);

			static void jai_auto_bind(jai::dynamic_binder<FocusGroup>& a_builder);

			ComponentDerivedAccessors(FocusGroup)

			JAI_SIGNAL(GroupSignalSignature, onBack);

		public:
			Kind kind() const { return groupKind; }
			std::shared_ptr<FocusGroup> kind(Kind a_kind);

			// Where the group comes when stepping between groups, before its place in the scene.
			int64_t order() const { return groupOrder; }
			std::shared_ptr<FocusGroup> order(int64_t a_order);

			bool modal() const { return isModal; }
			std::shared_ptr<FocusGroup> modal(bool a_modal);

			// The id of the node under this one whose Clickable takes the focus when nothing is remembered.
			std::string defaultFocus() const { return defaultFocusId; }
			std::shared_ptr<FocusGroup> defaultFocus(const std::string& a_nodeId);

			// The Clickable under this group that last held the focus, or the tab header last chosen.
			std::shared_ptr<Clickable> remembered() const { return lastFocused.lock(); }

			// Emits onBack.
			void back();

		protected:
			FocusGroup(const std::weak_ptr<Node>& a_owner, TapDevice& a_mouse);

			template<class Archive>
			void save(Archive& archive, std::uint32_t const) const {
				property_mgr.save(archive);
				Component::save(archive, 0);
			}

			template<class Archive>
			void load(Archive& archive, std::uint32_t const) {
				property_mgr.load(archive);
				Component::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<FocusGroup>& construct) {
				auto* services = ar.template get_user_context<MV::Services>();
				construct(std::shared_ptr<Node>(), *services->template get<MV::TapDevice>());
				construct->load(ar, 0);
				construct->initialize();
			}

			std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node>& a_parent) override {
				return cloneHelper(a_parent->attach<FocusGroup>(ourMouse).self());
			}

			std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component>& a_clone) override;

			void initialize() override;
			void detachImplementation() override;
			void reattachImplementation() override;

			void onOwnerDestroyed() override {
				Component::onOwnerDestroyed();
				onGroupQueryHandle.reset();
			}

		private:
			void answerGroupQueries();
			bool holds(const std::shared_ptr<Clickable>& a_clickable) const;
			bool under(const void* a_scope) const;
			bool focusPreferred();

			JAI_NAMED_PROPERTY((Kind), "kind", groupKind, Kind::WINDOW);
			JAI_NAMED_PROPERTY((int64_t), "order", groupOrder, 0);
			JAI_NAMED_PROPERTY((bool), "modal", isModal, false);
			JAI_NAMED_PROPERTY((std::string), "defaultFocus", defaultFocusId);

			TapDevice& ourMouse;
			TapDevice::SignalType onGroupQueryHandle;
			std::weak_ptr<Clickable> lastFocused;
		};
	}
}

#endif
