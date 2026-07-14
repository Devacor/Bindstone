#include "scriptPanel.h"
#include <iostream>
#include "MV/Utility/generalUtility.h"
#include "MV/Utility/services.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>

static jai::registrar<Workbench::ScriptPanel, MV::Services> _hookWorkbenchPanel("WorkbenchPanel",
	[](jai::dynamic_binder<Workbench::ScriptPanel>& builder, const MV::Services&) {
	using Workbench::ScriptPanel;
	builder.property("build", &ScriptPanel::scriptBuild);
	builder.property("tick", &ScriptPanel::scriptUpdate);
	builder.property("onResized", &ScriptPanel::scriptResized);

	builder.method("root", &ScriptPanel::root);
	builder.method("width", &ScriptPanel::width);
	builder.method("height", &ScriptPanel::height);
	builder.method("rowHeight", &ScriptPanel::rowHeight);
	builder.method("fieldHeight", &ScriptPanel::fieldHeight);
	builder.method("pad", &ScriptPanel::pad);
	builder.method("labelWidth", &ScriptPanel::labelWidth);
	builder.method("reportHeight", &ScriptPanel::reportHeight);
	builder.method("place", &ScriptPanel::place);

	builder.method("label", &ScriptPanel::label);
	builder.method("button", &ScriptPanel::button);
	builder.method("textField", &ScriptPanel::textField);

	builder.method("loadScene", &ScriptPanel::loadScene);
	builder.method("saveScene", &ScriptPanel::saveScene);
	builder.build();
});

namespace Workbench {

	std::shared_ptr<MV::Scene::Text> ScriptPanel::label(const std::shared_ptr<MV::Scene::Node>& a_parent, const std::string& a_id, float a_x, float a_y, float a_width, float a_height, const std::string& a_text) {
		auto host = a_parent->make(MV::guid(a_id + "_"));
		host->position(MV::point(a_x, a_y));
		return makeLabel(host, *services, *theme, a_id, MV::size(a_width, a_height), a_text);
	}

	std::shared_ptr<MV::Scene::Button> ScriptPanel::button(const std::shared_ptr<MV::Scene::Node>& a_parent, const std::string& a_id, float a_x, float a_y, float a_width, float a_height, const std::string& a_text) {
		auto host = a_parent->make(MV::guid(a_id + "_"));
		host->position(MV::point(a_x, a_y));
		return makeButton(host, *services, *theme, a_id, MV::size(a_width, a_height), a_text, nullptr);
	}

	std::shared_ptr<MV::Scene::Text> ScriptPanel::textField(const std::shared_ptr<MV::Scene::Node>& a_parent, const std::string& a_id, float a_x, float a_y, float a_width, float a_height, const std::string& a_initial) {
		auto host = a_parent->make(MV::guid(a_id + "_"));
		host->position(MV::point(a_x, a_y));
		return makeTextField(host, *services, *theme, *focus, a_id, MV::size(a_width, a_height), a_initial, nullptr);
	}

	void ScriptPanel::update(double a_dt) {
		reloadCheckAccumulator += a_dt;
		if (reloadCheckAccumulator > 0.5) {
			reloadCheckAccumulator = 0.0;
			std::error_code errorCode;
			auto currentWrite = std::filesystem::last_write_time(scriptPath, errorCode);
			if (!errorCode && currentWrite != lastWriteTime) {
				lastWriteTime = currentWrite;
				std::cout << "[workbench] reloading panel script: " << scriptPath << std::endl;
				runScript();
			}
		}
		if (scriptUpdate) {
			scriptUpdate(*this, a_dt);
		}
	}

	void ScriptPanel::runScript() {
		scriptBuild = nullptr;
		scriptUpdate = nullptr;
		scriptResized = nullptr;
		contentNode->clear();

		std::error_code errorCode;
		lastWriteTime = std::filesystem::last_write_time(scriptPath, errorCode);

		std::string contents = MV::fileContents(scriptPath);
		if (contents.empty()) {
			showError("missing: " + scriptPath);
			return;
		}
		auto* engine = services->get<jai::engine>();
		try {
			engine->add_global("panel", engine->make_object(shared_from_this()));
			engine->execute(contents);
			if (scriptBuild) {
				scriptBuild(*this);
			} else {
				showError("script did not assign panel.build");
			}
		} catch (const std::exception& a_error) {
			showError(a_error.what());
		}
	}

	void ScriptPanel::showError(const std::string& a_message) {
		std::cerr << "[workbench] panel script error (" << scriptPath << "): " << a_message << std::endl;
		contentNode->clear();
		makeLabel(contentNode, *services, *theme, "error", MV::size(std::max(120.0f, panelSize.width), theme->rowHeight * 2.0f), "script error: " + a_message);
	}

}
