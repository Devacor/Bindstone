#ifndef _WORKBENCH_SETTINGSPANEL_H_
#define _WORKBENCH_SETTINGSPANEL_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "dock.h"

namespace Workbench {

	// The editor's own preferences, one row each. A field applies on Enter; left any other way it shows the value in
	// use again.
	class SettingsPanel : public Panel {
	public:
		struct Row {
			std::string label;
			std::function<std::string()> value;
			// Gets what was entered; whatever it decides, the field shows value() afterwards.
			std::function<void(const std::string&)> apply;
		};

		explicit SettingsPanel(std::vector<Row> a_rows) : Panel("Settings"), rows(std::move(a_rows)) {}

		// A setting changed somewhere else.
		void refresh();

		MV::Point<int> fieldScreenPoint(const std::string& a_label) const;
		std::string shown(const std::string& a_label) const;

	protected:
		void build() override;

	private:
		std::shared_ptr<MV::Scene::Text> field(const std::string& a_label) const;

		std::vector<Row> rows;
		std::vector<std::shared_ptr<MV::Scene::Text>> fields;
	};

}

#endif
