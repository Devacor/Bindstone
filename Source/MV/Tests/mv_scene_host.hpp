#ifndef _MV_TESTS_SCENE_HOST_HPP_
#define _MV_TESTS_SCENE_HOST_HPP_

#include <jaiscript/core/engine.hpp>
#include "MV/Render/render.h"
#include "MV/Render/formattedText.h"
#include "MV/Render/sharedTextures.h"
#include "MV/Render/Scene/node.h"
#include "MV/Interface/tapDevice.h"
#include "MV/Script/script.h"
#include "MV/Utility/services.hpp"
#include "MV/Utility/generalUtility.h"

#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <memory>
#include <string>

namespace mv_tests {

	// Headless stand-in for Managers: every service Node::load resolves, plus a script engine.
	struct SceneHost {
		MV::Draw2D renderer;
		MV::TextLibrary textLibrary;
		MV::TapDevice mouse;
		MV::SharedTextures textures;
		MV::Services services;
		std::shared_ptr<jai::engine> engine;

		SceneHost() :
			textLibrary(renderer) {
			renderer.makeHeadless();
			renderer.initialize(MV::Size<int>(1280, 720)); // headless projection needs a viewport or world math collapses to zero
			renderer.loadShaderCode(MV::PREMULTIPLY_ID, "", "");
			textures.renderer(&renderer);
			services.connect(&renderer);
			services.connect(&textLibrary);
			services.connect(&mouse);
			services.connect(&textures);
			bool debugWasEnabled = MV::scriptDebugEnabled();
			MV::scriptDebugEnabled(false);
			engine = MV::makeScriptEngine(services);
			MV::scriptDebugEnabled(debugWasEnabled);
			services.connect<jai::engine>(engine.get());
		}

		SceneHost(const SceneHost&) = delete;
		SceneHost& operator=(const SceneHost&) = delete;

		void loadFont(const std::string& a_id) {
			MV::FontDefinition::make(textLibrary, a_id, fontPath(), 12);
		}

		static std::string fontPath() {
			for (const char* candidate : { "../../../Assets/Fonts/Verdana.ttf", "Assets/Fonts/Verdana.ttf" }) {
				if (std::filesystem::exists(candidate)) {
					return std::filesystem::absolute(candidate).string();
				}
			}
			return "";
		}
	};

	struct ScratchScene {
		std::string path;

		ScratchScene() :
			path((std::filesystem::temp_directory_path() / (MV::guid("mv_tests_scene_") + ".scene")).string()) {
		}
		~ScratchScene() {
			if (std::getenv("MV_TESTS_KEEP_SCRATCH")) { return; }
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
		}
		ScratchScene(const ScratchScene&) = delete;
		ScratchScene& operator=(const ScratchScene&) = delete;

		std::string contents() const {
			return MV::fileContents(path);
		}
	};

	inline std::shared_ptr<MV::Scene::Node> saveAndLoad(SceneHost& a_host, const std::shared_ptr<MV::Scene::Node>& a_scene, ScratchScene& a_file, bool a_doPostLoadStep = true) {
		a_scene->saveJai(a_file.path, a_host.services, false);
		return MV::Scene::Node::load(a_file.path, a_host.services, a_doPostLoadStep);
	}

	inline size_t childrenWithPrefix(const std::shared_ptr<MV::Scene::Node>& a_node, const std::string& a_prefix) {
		return static_cast<size_t>(std::count_if(a_node->cbegin(), a_node->cend(), [&](const std::shared_ptr<MV::Scene::Node>& a_child) {
			return a_child->id().rfind(a_prefix, 0) == 0;
		}));
	}

	inline std::shared_ptr<MV::Scene::Node> childWithPrefix(const std::shared_ptr<MV::Scene::Node>& a_node, const std::string& a_prefix) {
		auto found = std::find_if(a_node->cbegin(), a_node->cend(), [&](const std::shared_ptr<MV::Scene::Node>& a_child) {
			return a_child->id().rfind(a_prefix, 0) == 0;
		});
		return found == a_node->cend() ? nullptr : *found;
	}

	inline bool hasSerializedType(const std::string& a_json, const std::string& a_type) {
		return a_json.find("\"$type\": \"" + a_type + "\"") != std::string::npos ||
			a_json.find("\"$type\":\"" + a_type + "\"") != std::string::npos;
	}

}

#endif
