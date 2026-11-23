#ifndef _WORKBENCH_SETTINGSPANEL_H_
#define _WORKBENCH_SETTINGSPANEL_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include "dock.h"
#include "keyBindings.h"

namespace Workbench {

	// The editor's own preferences, one row each. A field applies on Enter; left any other way it shows the value in
	// use again. Below them, the key bindings by group: a press on a binding's key waits for the next key with its
	// modifiers (Esc gives up), and Reset puts back its standard key.
	class SettingsPanel : public Panel {
	public:
		struct Row {
			std::string label;
			std::function<std::string()> value;
			// Gets what was entered; whatever it decides, the field shows value() afterwards.
			std::function<void(const std::string&)> apply;
		};

		explicit SettingsPanel(std::vector<Row> a_rows, KeyBindings* a_keys = nullptr) : Panel("Settings"), rows(std::move(a_rows)), keys(a_keys) {}

		// A setting changed somewhere else.
		void refresh();

		MV::Point<int> fieldScreenPoint(const std::string& a_label) const;
		std::string shown(const std::string& a_label) const;

		// Waiting for the key to bind; the editor gives it every key-down until it has one.
		bool capturing() const { return !capturingId.empty(); }
		bool captureKey(const SDL_Event& a_event);
		void beginCapture(const std::string& a_id);
		// Stops waiting, the key left as it was: a press anywhere, or Settings closing.
		void cancelCapture();
		std::optional<MV::Point<int>> bindingScreenPoint(const std::string& a_id) const;
		std::optional<MV::Point<int>> resetScreenPoint(const std::string& a_id) const;
		std::optional<MV::Point<int>> resetAllScreenPoint() const;
		std::string bindingShown(const std::string& a_id) const;
		// A new key that another action in its group already has.
		std::function<void(const std::string&)> onWarning;

	protected:
		void build() override;

	private:
		std::shared_ptr<MV::Scene::Text> field(const std::string& a_label) const;

		void buildBindings();
		std::string bindingText(const std::string& a_id) const;

		std::vector<Row> rows;
		std::vector<std::shared_ptr<MV::Scene::Text>> fields;
		KeyBindings* keys = nullptr;
		std::shared_ptr<MV::Scene::Node> bindingHost;
		float rowsHeight = 0.0f;
		std::string capturingId;
		std::map<std::string, std::string> bindingsShown;
		std::map<std::string, std::weak_ptr<MV::Scene::Node>> bindingButtons;
		std::map<std::string, std::weak_ptr<MV::Scene::Node>> resetButtons;
		std::weak_ptr<MV::Scene::Node> resetAllButton;
	};

}

#endif
