#ifndef _WORKBENCH_SCRIPTPANEL_H_
#define _WORKBENCH_SCRIPTPANEL_H_

#include <filesystem>
#include "dock.h"
#include "widgets.h"

namespace Workbench {

	// A dock panel whose UI lives in a hot-reloaded .jai file. Mirrors the InterfaceManager
	// page convention: the script receives this object as `panel` and assigns hook functions.
	// Editing the script file rebuilds the panel live — no recompile.
	class ScriptPanel : public Panel, public std::enable_shared_from_this<ScriptPanel> {
	public:
		ScriptPanel(const std::string& a_title, std::string a_scriptPath) :
			Panel(a_title),
			scriptPath(std::move(a_scriptPath)) {
		}

		// Script-assignable hooks.
		std::function<void(ScriptPanel&)> scriptBuild;
		std::function<void(ScriptPanel&, double)> scriptUpdate;
		std::function<void(ScriptPanel&, float, float)> scriptResized;

		// App-injected actions.
		std::function<void(const std::string&)> onLoadScene;
		std::function<void(const std::string&)> onSaveScene;

		// Script-facing surface (bound in scriptPanel.cpp).
		std::shared_ptr<MV::Scene::Node> root() const { return contentNode; }
		float width() const { return panelSize.width; }
		float height() const { return panelSize.height; }
		float rowHeight() const { return theme->rowHeight; }
		float fieldHeight() const { return theme->fieldHeight; }
		float pad() const { return theme->pad; }
		float labelWidth() const { return theme->labelWidth; }
		void reportHeight(float a_height) { builtHeight = a_height; }
		void place(const std::shared_ptr<MV::Scene::Node>& a_node, float a_x, float a_y) {
			a_node->position(MV::point(a_x, a_y));
		}

		std::shared_ptr<MV::Scene::Text> label(const std::shared_ptr<MV::Scene::Node>& a_parent, const std::string& a_id, float a_x, float a_y, float a_width, float a_height, const std::string& a_text);
		std::shared_ptr<MV::Scene::Button> button(const std::shared_ptr<MV::Scene::Node>& a_parent, const std::string& a_id, float a_x, float a_y, float a_width, float a_height, const std::string& a_text);
		std::shared_ptr<MV::Scene::Text> textField(const std::shared_ptr<MV::Scene::Node>& a_parent, const std::string& a_id, float a_x, float a_y, float a_width, float a_height, const std::string& a_initial);

		void loadScene(const std::string& a_fileName) { if (onLoadScene) { onLoadScene(a_fileName); } }
		void saveScene(const std::string& a_fileName) { if (onSaveScene) { onSaveScene(a_fileName); } }

		void update(double a_dt) override;
		void resized(const MV::Size<>& a_size) override {
			panelSize = a_size;
			if (scriptResized) {
				scriptResized(*this, a_size.width, a_size.height);
			}
		}

	protected:
		void build() override { runScript(); }

	private:
		void runScript();
		void showError(const std::string& a_message);

		std::string scriptPath;
		MV::Size<> panelSize{ 300.0f, 200.0f };
		std::filesystem::file_time_type lastWriteTime{};
		double reloadCheckAccumulator = 0.0;
	};

}

#endif
