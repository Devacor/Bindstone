#ifndef _WORKBENCH_INSPECTOR_H_
#define _WORKBENCH_INSPECTOR_H_

#include <algorithm>
#include <map>
#include <optional>
#include "commands.h"
#include "dock.h"
#include "widgets.h"
#include <jaiscript/properties/property_manager.hpp>

namespace Workbench {

	// Property-grid inspector: rows are generated from the jai property reflection bridge
	// (visit_reflected_properties + value_type_id/value_as/assign_value), so any component
	// declaring JAI_PROPERTY fields is editable with no per-component editor code.
	class InspectorPanel : public Panel {
	public:
		explicit InspectorPanel(CommandHistory& a_history) : Panel("Inspector"), history(&a_history) {}

		void inspect(const std::shared_ptr<MV::Scene::Node>& a_node);

		// Gizmo bridge: header "G" buttons toggle a component's viewport gizmo through the app.
		std::function<void(const std::shared_ptr<MV::Scene::Component>&)> onEditComponent;
		std::function<bool(const std::shared_ptr<MV::Scene::Component>&)> componentGizmoActive;

		// Parallax preview bridge: the row shows on a node that has an enabled Parallax layer on it or below it.
		std::function<void(bool)> onParallaxPreview;
		std::function<bool()> parallaxPreviewActive;

		// A press on a reference field selects the node it names.
		std::function<void(const std::shared_ptr<MV::Scene::Node>&)> onSelectNode;
		// A node dragged out of the Hierarchy and released over a reference field is assigned to it. True when a field took it.
		bool dropNode(const std::shared_ptr<MV::Scene::Node>& a_node, const MV::Point<int>& a_screenPoint);

		// Where a row can be pressed, by its label (the first row with it): its field, or the whole row for a group's header.
		std::optional<MV::Point<int>> rowScreenPoint(const std::string& a_label, bool a_field = true) const;
		bool texturePickerOpen() const { return texturePicker.open; }
		std::optional<MV::Point<int>> texturePickerTileScreenPoint(size_t a_index) const;

		void resized(const MV::Size<>& a_size) override {
			if (std::abs(a_size.width - panelWidth) > 0.5f) {
				panelWidth = a_size.width;
				rebuild();
			}
		}

		void handleInput(SDL_Event& a_event) override;

	protected:
		void build() override;

	private:
		void rebuild();
		void buildNodeSection();
		void buildComponentSection(const std::shared_ptr<MV::Scene::Component>& a_component);
		void addPropertyRow(const std::shared_ptr<MV::Scene::Component>& a_component, const std::string& a_name, jai::property_base* a_property);
		// A value the inspector shows but cannot edit yet: plain text, no field box.
		void addReadOnlyRow(const std::string& a_name, const std::string& a_value);

		// Editors for the kinds a plain field cannot show (inspectorEditors.cpp). addCustomEditor returns false for a
		// property none of them takes.
		bool addCustomEditor(const std::shared_ptr<MV::Scene::Component>& a_component, const std::string& a_path, jai::property_base* a_property);
		// A foldout inside a section, closed until opened; the summary shows beside the title either way.
		bool addGroupHeader(const std::string& a_title, const std::string& a_key, const std::string& a_summary);
		// A node reference: press to select it, drop a Hierarchy node on it or press x to change it. No a_assign: read-only.
		void addNodeReferenceRow(const std::string& a_label, const std::shared_ptr<MV::Scene::Node>& a_node, std::function<void(const std::shared_ptr<MV::Scene::Node>&)> a_assign);
		void addAnchorsGroup(const std::shared_ptr<MV::Scene::Drawable>& a_drawable);
		void addTextureRows(const std::shared_ptr<MV::Scene::Drawable>& a_drawable);
		void addPointsGroup(const std::shared_ptr<MV::Scene::Drawable>& a_drawable);
		void addIndicesGroup(const std::shared_ptr<MV::Scene::Drawable>& a_drawable);
		// Rows of a list a page at a time, with the pager; returns the first index to show.
		size_t addPager(const std::string& a_key, size_t a_count, size_t a_perPage);
		void openTexturePicker(const std::weak_ptr<MV::Scene::Drawable>& a_drawable, size_t a_slot, const MV::Point<int>& a_screenPoint);
		void buildTexturePicker();
		void closeTexturePicker();

		// "spawnProperties/maximumParticles": a property inside a nested owner, named from the component down.
		static jai::property_base* resolveProperty(const std::shared_ptr<MV::Scene::Component>& a_component, const std::string& a_path);

	public:
		// A reference a component holds to a node, with the writes that clear it and put it back.
		struct NodeReference {
			std::shared_ptr<MV::Scene::Node> target;
			std::function<void()> clear;
			std::function<void()> restore;
		};
		// Every node a component refers to: its node properties (nested owners included) and its anchor parent's owner.
		static std::vector<NodeReference> nodeReferences(const std::shared_ptr<MV::Scene::Component>& a_component);

	private:
		static std::string leafName(const std::string& a_path);

		bool addSectionHeader(const std::string& a_title, const std::string& a_key, const std::shared_ptr<MV::Scene::Component>& a_gizmoComponent = nullptr);
		void openColorPopup(const std::weak_ptr<MV::Scene::Component>& a_component, const std::string& a_propertyName, jai::property_base* a_property, const std::weak_ptr<MV::Scene::Sprite>& a_swatch, const MV::Point<int>& a_screenPoint);
		void closeColorPopup(bool a_keepColor);
		std::shared_ptr<MV::Scene::Node> makeRow(const std::string& a_label);
		static void notifyEdited(const std::shared_ptr<MV::Scene::Component>& a_component);

		float labelWidth() const { return std::clamp(panelWidth * 0.4f, theme->px(80.0f), theme->px(180.0f)); }
		float fieldWidth() const { return std::max(theme->px(60.0f), panelWidth - labelWidth() - theme->pad * 3.0f); }

		// One chokepoint for property writes: name-keyed (survives inspector rebuilds), applied
		// through the reflection bridge; the returned closure is what undo/redo commands replay.
		template <typename ValueT>
		std::function<void(ValueT)> propertyApplier(const std::weak_ptr<MV::Scene::Component>& a_component, const std::string& a_name) {
			return [this, a_component, a_name](ValueT a_value) {
				if (auto locked = a_component.lock()) {
					if (auto* property = resolveProperty(locked, a_name)) {
						if (property->assign_value(a_value)) {
							notifyEdited(locked);
						}
					}
				}
			};
		}

		template <typename ValueT>
		void recordPropertyEdit(const std::weak_ptr<MV::Scene::Component>& a_component, const std::string& a_name, const ValueT& a_oldValue, const ValueT& a_newValue) {
			auto apply = propertyApplier<ValueT>(a_component, a_name);
			history->record(a_name,
				[apply, a_newValue]() { apply(a_newValue); },
				[apply, a_oldValue]() { apply(a_oldValue); });
		}

		CommandHistory* history = nullptr;

		std::weak_ptr<MV::Scene::Node> inspectedNode;
		// Ends the open colour picker session: true records the change as one command, false restores the colour it opened with.
		std::function<void(bool)> colorPopupClose;

		struct NodeDropZone {
			std::weak_ptr<MV::Scene::Node> row;
			std::function<void(const std::shared_ptr<MV::Scene::Node>&)> assign;
		};
		std::vector<NodeDropZone> nodeDropZones;
		std::vector<std::pair<std::string, std::weak_ptr<MV::Scene::Node>>> labelledRows;
		std::shared_ptr<MV::Scene::Grid> rowGrid;
		float rowIndent = 0.0f;
		std::map<std::string, bool> openGroups;
		std::map<std::string, size_t> listPages;

		struct TexturePicker {
			std::weak_ptr<MV::Scene::Drawable> drawable;
			size_t slot = 0;
			size_t source = 0;
			size_t page = 0;
			MV::Point<> position;
			bool open = false;
		};
		TexturePicker texturePicker;
		std::map<std::string, bool> collapsedSections;
		float panelWidth = 260.0f;
	};

}

#endif
