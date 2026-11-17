#ifndef _MV_SCENE_FOCUSGROUP_H_
#define _MV_SCENE_FOCUSGROUP_H_

#include <optional>
#include "node.h"
#include "clickable.h"
#include "MV/Interface/tapDevice.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {

		// A selection context: the node it is on and everything under it. Groups nest, and the one in use is the
		// innermost one the focus is in.
		// A WINDOW is entered in one of two ways.
		// Left as it is, it is contiguous with what is around it: a move from outside goes straight onto one of its
		// controls, moves inside follow its own layout first, and a move with nowhere to go inside carries on out.
		// With explicitSelect it is one stop: a move from outside selects the group as a whole (its highlightView
		// shows), activating it (a controller's south button) goes in, nothing moves out of it, and backing out (the
		// east button) selects it as a whole again.
		// Stepping (a controller's triggers) goes between the groups beside it under the same parent, onto what each
		// remembers, else its defaultFocus, else its first focusable, or onto an explicitSelect group as a whole; a
		// group with none beside it steps the level above. Backing out of a group without explicitSelect emits
		// onBack when something listens for it, and otherwise goes up a level, onto the parent group's own controls.
		// A modal window takes the focus as it shows and nothing leaves it until it hides, when the focus goes back
		// where it was.
		// TABS marks a strip of tab headers: stepping tabs (a controller's bumpers) presses the next or previous
		// header in it, as a pointer would.
		class FocusGroup : public jai::property_owner<FocusGroup, Component> {
			friend Node;
			friend jai::access;

		public:
			enum class Kind { WINDOW, TABS };
			typedef void GroupSignalSignature(const std::shared_ptr<FocusGroup>&);

			static void jai_auto_bind(jai::dynamic_binder<FocusGroup>& a_builder);

			ComponentDerivedAccessors(FocusGroup)

			JAI_SIGNAL(GroupSignalSignature, onBack);

		private:
			jai::signal_emitter<GroupSignalSignature> onHighlightEnterSignal;
			jai::signal_emitter<GroupSignalSignature> onHighlightLeaveSignal;

		public:
			// The group itself took or lost the focus, selected as a whole; not serialized.
			jai::signal<GroupSignalSignature> onHighlightEnter;
			jai::signal<GroupSignalSignature> onHighlightLeave;

			Kind kind() const { return groupKind; }
			std::shared_ptr<FocusGroup> kind(Kind a_kind);

			// Where the group comes when stepping between groups, before its place in the scene.
			int64_t order() const { return groupOrder; }
			std::shared_ptr<FocusGroup> order(int64_t a_order);

			bool modal() const { return isModal; }
			std::shared_ptr<FocusGroup> modal(bool a_modal);

			bool explicitSelect() const { return isExplicit; }
			std::shared_ptr<FocusGroup> explicitSelect(bool a_explicit);

			// The id of the node under this one that takes the focus when nothing is remembered: its Clickable, or
			// its explicitSelect group as a whole.
			std::string defaultFocus() const { return defaultFocusId; }
			std::shared_ptr<FocusGroup> defaultFocus(const std::string& a_nodeId);

			// Shown only while the group is selected as a whole.
			std::shared_ptr<Node> highlightNode() const { return highlightView; }
			std::shared_ptr<FocusGroup> highlightNode(const std::shared_ptr<Node>& a_highlightView);

			// The Clickable under this group that last held the focus, or the tab header last chosen.
			std::shared_ptr<Clickable> remembered() const;

			// The group holds the focus itself, selected as a whole.
			bool selected() const { return isSelected; }
			// Selects the group as a whole, when it has explicitSelect and shows.
			void select();
			// Puts the focus inside: on what it remembers, else its default, else its first focusable.
			void enter();

			// Emits onBack. handlesBack is whether anything is connected to it.
			void back();
			bool handlesBack() const;

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
			void postLoadInitialize() override;
			void detachImplementation() override;
			void reattachImplementation() override;

			void onOwnerDestroyed() override {
				Component::onOwnerDestroyed();
				onGroupQueryHandle.reset();
				onFocusQueryHandle.reset();
			}

		private:
			void answerQueries();
			bool under(const void* a_scope) const;
			bool selectable(const void* a_scope) const;
			bool focusPreferred();
			FocusCandidate selfCandidate();
			bool screenExtent(Point<int>& a_min, Point<int>& a_max);
			void selectedChanged(bool a_selected);

			JAI_NAMED_PROPERTY((Kind), "kind", groupKind, Kind::WINDOW);
			JAI_NAMED_PROPERTY((int64_t), "order", groupOrder, 0);
			JAI_NAMED_PROPERTY((bool), "modal", isModal, false);
			JAI_NAMED_PROPERTY((bool), "explicitSelect", isExplicit, false);
			JAI_NAMED_PROPERTY((std::string), "defaultFocus", defaultFocusId);
			//Clone is a no-op because it is manually managed in the cloneHelper
			JAI_PROPERTY((std::shared_ptr<Node>), highlightView, nullptr, [](auto&, auto&) {});

			TapDevice& ourMouse;
			TapDevice::SignalType onGroupQueryHandle;
			TapDevice::SignalType onFocusQueryHandle;
			std::optional<FocusCandidate> lastFocused;
			bool isSelected = false;
		};
	}
}

#endif
