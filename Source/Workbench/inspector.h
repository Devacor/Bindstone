#ifndef _WORKBENCH_INSPECTOR_H_
#define _WORKBENCH_INSPECTOR_H_

#include <algorithm>
#include <map>
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

		void resized(const MV::Size<>& a_size) override {
			if (std::abs(a_size.width - panelWidth) > 0.5f) {
				panelWidth = a_size.width;
				rebuild();
			}
		}

	protected:
		void build() override;

	private:
		void rebuild();
		void buildNodeSection(float& a_y);
		void buildComponentSection(const std::shared_ptr<MV::Scene::Component>& a_component, float& a_y);
		void addPropertyRow(const std::shared_ptr<MV::Scene::Component>& a_component, const std::string& a_name, jai::property_base* a_property, float& a_y);

		bool addSectionHeader(const std::string& a_title, const std::string& a_key, float& a_y, const std::shared_ptr<MV::Scene::Component>& a_gizmoComponent = nullptr);
		void openColorPopup(const std::weak_ptr<MV::Scene::Component>& a_component, const std::string& a_propertyName, jai::property_base* a_property, const std::weak_ptr<MV::Scene::Sprite>& a_swatch, const MV::Point<int>& a_screenPoint);
		std::shared_ptr<MV::Scene::Node> makeRow(const std::string& a_label, float& a_y);
		void notifyEdited(const std::shared_ptr<MV::Scene::Component>& a_component);

		float labelWidth() const { return std::clamp(panelWidth * 0.4f, 90.0f, 160.0f); }
		float fieldWidth() const { return std::max(60.0f, panelWidth - labelWidth() - theme->pad * 3.0f); }

		// One chokepoint for property writes: name-keyed (survives inspector rebuilds), applied
		// through the reflection bridge; the returned closure is what undo/redo commands replay.
		template <typename ValueT>
		std::function<void(ValueT)> propertyApplier(const std::weak_ptr<MV::Scene::Component>& a_component, const std::string& a_name) {
			return [this, a_component, a_name](ValueT a_value) {
				if (auto locked = a_component.lock()) {
					if (auto* property = locked->find_reflected_property(a_name)) {
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
		std::map<std::string, bool> collapsedSections;
		float panelWidth = 260.0f;
	};

}

#endif
