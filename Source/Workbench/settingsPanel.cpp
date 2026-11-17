#include "settingsPanel.h"
#include "widgets.h"

namespace Workbench {

	void SettingsPanel::build() {
		const float labelWidth = theme->px(120.0f);
		const float fieldWidth = theme->px(80.0f);
		auto rowGrid = contentNode->attach<MV::Scene::Grid>()->columns(1)->layoutPolicy(MV::Scene::Grid::AutoLayoutPolicy::Child);
		rowGrid->hide();
		rowGrid->cellSize(MV::size(labelWidth + theme->pad + fieldWidth, theme->fieldHeight))->padding(MV::Point<>(), MV::point(0.0f, theme->px(4.0f)));
		for (size_t index = 0; index < rows.size(); ++index) {
			auto row = contentNode->make(MV::guid("setting_"));
			makeLabel(row, *services, *theme, "label", MV::size(labelWidth, theme->fieldHeight), rows[index].label);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth + theme->pad, 0.0f));
			fields.push_back(makeTextField(fieldHost, *services, *theme, *focus, "value", MV::size(fieldWidth, theme->fieldHeight), rows[index].value(), [this, index](const std::string& a_text) {
				rows[index].apply(a_text);
				fields[index]->text(rows[index].value());
			}, true));
		}
		rowGrid->layoutCells();
		builtHeight = rowGrid->bounds().height();
	}

	void SettingsPanel::refresh() {
		for (size_t index = 0; index < fields.size(); ++index) {
			if (!focus->activeWithin(fields[index]->owner())) {
				fields[index]->text(rows[index].value());
			}
		}
	}

	std::shared_ptr<MV::Scene::Text> SettingsPanel::field(const std::string& a_label) const {
		for (size_t index = 0; index < fields.size(); ++index) {
			if (rows[index].label == a_label) {
				return fields[index];
			}
		}
		return nullptr;
	}

	MV::Point<int> SettingsPanel::fieldScreenPoint(const std::string& a_label) const {
		auto found = field(a_label);
		return found ? found->owner()->screenBounds().centerPoint() : MV::Point<int>();
	}

	std::string SettingsPanel::shown(const std::string& a_label) const {
		auto found = field(a_label);
		return found ? std::string(found->text()) : std::string();
	}

}
