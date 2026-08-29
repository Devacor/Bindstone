#include "Game/gameEditor.h"
#include "Game/NetworkLayer/synchronizeAction.h"
#include "MV/Utility/threadPool.hpp"
#include "MV/Utility/services.hpp"

#include "MV/Serialization/serialize.h"
#include "MV/Render/Scene/clickable.h"
//#include "vld.h"

#include "Game/NetworkLayer/clientActions.h"

#include "MV/Script/script.h"
#include "MV/Utility/generalUtility.h"
#include "Game/standardScriptMethods.h"

#include <filesystem>
#include <fstream>
#include <cstdlib>

#include <jaiscript/serialization/json_archive.hpp>
#include "../../JaiScript/bench/fast_json.hpp"
#include "MV/Render/Scene/emitter.h"
#include <chrono>
#include <algorithm>
#include <vector>
#include <map>
#include <sstream>

// Manual benchmark: parse the real scene JSON into the production script_value DOM
// (std::map<script_value,script_value>) vs the flat arena parser, same input, same run.
// This quantifies, in the real engine, how much of a JaiScript scene load is the DOM build
// and how much a flat DOM would save. Run: BindstoneClient.exe -bench
static void RunJsonParseBenchmark(jai::engine* eng) {
	const char* candidates[] = {
		"Assets/Scenes/map.scene",
		"Scenes/map.scene",
	};
	std::string text;
	std::string used;
	for (const char* path : candidates) {
		std::ifstream f(path, std::ios::binary);
		if (f) {
			text.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			used = path;
			break;
		}
	}
	if (text.empty()) {
		std::cout << "\n[bench] could not open map.scene (cwd-relative or absolute)\n";
		return;
	}

	const double mib = static_cast<double>(text.size()) / (1024.0 * 1024.0);
	std::cout << "\n=== JSON DOM parse: real json_archive_reader vs fast_json (flat) ===\n";
	std::cout << "file: " << used << "  size: " << text.size() << " bytes (" << mib << " MiB)\n";

	const int iters = 10;
	auto median = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
	volatile uint64_t sink = 0;

	std::vector<double> realT;
	for (int i = 0; i < iters; ++i) {
		auto a = std::chrono::steady_clock::now();
		jai::serialization::json_archive_reader reader(text, eng);   // parses into the std::map DOM in its ctor
		auto b = std::chrono::steady_clock::now();
		sink += reader.peek_null() ? 1u : 2u;                        // touch the parsed reader
		realT.push_back(std::chrono::duration<double, std::milli>(b - a).count());
	}

	std::vector<double> fastT;
	for (int i = 0; i < iters; ++i) {
		fastjson::Document doc;
		fastjson::Parser p;
		auto a = std::chrono::steady_clock::now();
		p.parse(text.data(), text.size(), doc);
		auto b = std::chrono::steady_clock::now();
		sink += doc.nodes.size();
		fastT.push_back(std::chrono::duration<double, std::milli>(b - a).count());
	}

	const double rm = median(realT);
	const double fm = median(fastT);
	std::cout << "  json_archive_reader (flat arena DOM): " << rm << " ms  (" << (mib / (rm / 1000.0)) << " MiB/s)\n";
	std::cout << "  fast_json           (flat arena):   " << fm << " ms  (" << (mib / (fm / 1000.0)) << " MiB/s)\n";
	std::cout << "  speedup (real / fast): " << (rm / fm) << "x   [sink=" << sink << "]\n\n";
}

// Headless full-scene load benchmark. Mirrors the proven headless setup the game server
// uses (gameServer.cpp: renderer.makeHeadless().initialize(...); Game ctor: engine ->
// bind_registrar(managers.services) -> services.connect<engine>), then loads the scene
// exactly like the editor's Load button. Run: BindstoneClient.exe -loadbench
static void RunSceneLoadBenchmark(bool a_headless) {
	Managers managers({ "", "" });
	auto jaiEngine = MV::makeScriptEngine(managers.services);
	managers.services.connect<jai::engine>(jaiEngine.get());
	if (a_headless) {
		managers.renderer.makeHeadless();
	}
	if (!managers.renderer.initialize(MV::Size<int>(1280, 720))) {
		std::cout << "[loadbench] renderer init failed\n";
		return;
	}

	std::cout << "\n=== Full-scene load benchmark (" << (a_headless ? "HEADLESS: serialization+construction only, GL skipped"
		: "REAL GL: includes texture load/upload") << ") ===\n";

	auto timeLoad = [&](const char* label, auto loadFn) {
		MV::Scene::Node::recalculateLocalBoundsCalls = 0;
		MV::Scene::Node::recalculateChildBoundsCalls = 0;
		MV::Scene::Node::recalculateMatrixCalls = 0;
		MV::textureLoadProfile().reset();
		try {
			auto t0 = std::chrono::steady_clock::now();
			auto root = loadFn();
			// Textures now stream in (decode on the pool, GL upload on the main thread via
			// pool.run()). Drain them so the measured time is a full load — in the game the
			// loop does one run() per frame, streaming the scene in over a few frames.
			while (managers.textures.pendingTextureLoads() > 0) {
				managers.pool.run();
			}
			auto t1 = std::chrono::steady_clock::now();
			double ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
			const auto& tp = MV::textureLoadProfile();
			std::cout << label << " total: " << ms << " ms  (root id=" << (root ? root->id() : std::string("<null>")) << ")"
				<< "  bounds[local=" << MV::Scene::Node::recalculateLocalBoundsCalls
				<< " child=" << MV::Scene::Node::recalculateChildBoundsCalls
				<< " matrix=" << MV::Scene::Node::recalculateMatrixCalls << "]\n";
			std::cout << "    textures: loadFile=" << tp.loadFileCalls.load() << " uploads=" << tp.uploadCalls.load()
				<< " uploadMiB=" << (tp.uploadBytes.load() / (1024.0 * 1024.0))
				<< "  decode(cpu-sum)=" << (tp.decodeMicros.load() / 1000.0) << " ms  upload=" << (tp.uploadMicros.load() / 1000.0) << " ms\n";
		} catch (std::exception& e) {
			std::cout << label << " EXCEPTION: " << e.what() << "\n";
		}
	};

	timeLoad("[JaiScript map.scene]", [&] { return MV::Scene::Node::load("Scenes/map.scene", managers.services, true); });
}

// Save a NetworkAction as shared_ptr<NetworkAction> (the wire form) and reload it through the base
// — exercises the polymorphic save + the B5 is_assignable load check the way the matchmaking wire does.
template<typename T>
static bool roundtripNetworkAction(MV::Services& a_services) {
	try {
		std::shared_ptr<NetworkAction> original = std::make_shared<T>();
		std::string wire = MV::toBinaryStringCast<NetworkAction>(original, a_services);
		auto back = MV::fromBinaryString<std::shared_ptr<NetworkAction>>(wire, a_services);
		return back != nullptr && std::dynamic_pointer_cast<T>(back) != nullptr;
	} catch (const std::exception&) {
		return false;
	}
}

// One-time restoration of the button onAccept eval_file receivers that the Cereal->jai scene
// conversion dropped (the receivers were serialized on the Clickable signal; the conversion
// lost them). Connects an owned script receiver to each button and re-saves the scene now that
// Clickable::onAccept serializes again. Run: BindstoneClient.exe -wirebuttons
static void RunButtonWiring() {
	Managers managers({ "", "" });
	auto jaiEngine = MV::makeScriptEngine(managers.services);
	managers.services.connect<jai::engine>(jaiEngine.get());
	static MV::TapDevice wireMouse;
	managers.services.connect(&wireMouse);
	managers.renderer.makeHeadless();
	if (!managers.renderer.initialize(MV::Size<int>(1280, 720))) {
		std::cout << "[wire] renderer init failed\n";
		return;
	}
	MV::FontDefinition::make(managers.textLibrary, "default", "Fonts/Verdana.ttf", 14);
	MV::FontDefinition::make(managers.textLibrary, "small", "Fonts/Verdana.ttf", 9);
	MV::FontDefinition::make(managers.textLibrary, "big", "Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);
	MV::initializeSpineBindings();

	auto fakeRoot = MV::Scene::Node::make(managers.renderer);
	fakeRoot->attach<MV::Scene::Sprite>()->hide()->id("ScreenScaler");

	struct Wire { const char* scene; const char* button; const char* script; };
	Wire wires[] = {
		{ "Assets/Interface/Login/view.scene", "LoginButton", "eval_file(\"Login/loginButton.script\");" },
		{ "Assets/Interface/Main/view.scene", "Play", "eval_file(\"Main/playButton.script\");" },
	};
	for (auto& w : wires) {
		try {
			auto root = MV::Scene::Node::load(w.scene, managers.services, false);
			fakeRoot->add(root);
			root->postLoadStep();
			auto clickable = root->get(w.button)->componentInChildren<MV::Scene::Clickable>(false, true, true).self();
			clickable->onAccept.connect("onAccept", w.script);
			root->saveJai(w.scene, managers.services, false);
			std::cout << "[wired] " << w.button << " -> " << w.script << " in " << w.scene << std::endl;
			root->removeFromParent();
		} catch (std::exception& e) {
			std::cout << "[FAIL] wiring " << w.button << ": " << e.what() << std::endl;
		}
	}
}

// One-time repair of the Login page art the Cereal->jai asset conversion dropped: every
// UI-pack texture handle (button.png on EmailBackground/PasswordBackground and the
// LoginButton state views) was lost because PackedTextureDefinition serialization didn't
// exist yet when the assets were converted. Reassigns handles from the assembled UI atlas
// and re-saves. Run: BindstoneClient.exe -fixloginui (saveJai writes via the %APPDATA%
// redirect - copy the result back to the repo Assets).
static void RunLoginUiRepair(const char* a_onlyPath = nullptr) {
	Managers managers({ "", "" });
	auto jaiEngine = MV::makeScriptEngine(managers.services);
	managers.services.connect<jai::engine>(jaiEngine.get());
	static MV::TapDevice repairMouse;
	managers.services.connect(&repairMouse);
	// Real GL (not headless): texture sizes must be loaded for slice geometry and UV
	// baking - headless leaves size()==0 and the 9-slice mesh degenerates.
	if (!managers.renderer.initialize(MV::Size<int>(1280, 720))) {
		std::cout << "[fixlogin] renderer init failed\n";
		return;
	}
	MV::FontDefinition::make(managers.textLibrary, "default", "Fonts/Verdana.ttf", 14);
	MV::FontDefinition::make(managers.textLibrary, "small", "Fonts/Verdana.ttf", 9);
	MV::FontDefinition::make(managers.textLibrary, "big", "Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);
	MV::initializeSpineBindings();

	managers.textures.assemblePacks("Assets/Atlases", &managers.renderer);
	auto uiPack = managers.textures.pack("UI");
	if (!uiPack) {
		std::cout << "[fixlogin] UI texture pack not found\n";
		return;
	}

	auto fakeRoot = MV::Scene::Node::make(managers.renderer);
	fakeRoot->attach<MV::Scene::Sprite>()->hide()->id("ScreenScaler");

	auto retexture = [&](const std::shared_ptr<MV::Scene::Node>& a_node, const char* a_textureName) {
		auto sprite = a_node->component<MV::Scene::Sprite>().self();
		sprite->texture(uiPack->handle(a_textureName));
		std::cout << "[fixlogin] " << a_node->id() << " -> " << a_textureName << std::endl;
	};
	// Baked glyph containers from the conversion era load as plain serializable nodes with
	// dead SurfaceTextureDefinitions; the live Text regenerates its own (now non-serializable)
	// containers, so anything serializable with these prefixes is stale.
	std::function<void(const std::shared_ptr<MV::Scene::Node>&, std::vector<std::shared_ptr<MV::Scene::Node>>&)> collectStaleGlyphNodes =
		[&](const std::shared_ptr<MV::Scene::Node>& a_node, std::vector<std::shared_ptr<MV::Scene::Node>>& a_out) {
			for (auto&& child : *a_node) {
				collectStaleGlyphNodes(child, a_out);
			}
			const auto& nodeId = a_node->id();
			if (a_node->serializable() && (nodeId.rfind("TEXT_", 0) == 0 || nodeId.rfind("CURSOR_", 0) == 0)) {
				a_out.push_back(a_node);
			}
		};
	// Restores bounds the conversion zeroed (Clickable hit areas, Button state views) then
	// re-slices by assigning the pack texture AFTER geometry so the 9-slice mesh rebuilds
	// on real bounds. Values recovered from the pre-conversion Cereal files.
	auto retextureViews = [&](const std::shared_ptr<MV::Scene::Button>& a_button, const MV::BoxAABB<>& a_viewBox) {
		for (auto& viewNode : { a_button->activeNode(), a_button->idleNode(), a_button->disabledNode() }) {
			viewNode->component<MV::Scene::Sprite>().self()->bounds(a_viewBox);
			viewNode->component<MV::Scene::Text>().self()->bounds(a_viewBox);
			retexture(viewNode, "button.png");
		}
	};
	using SceneFixup = std::function<void(const std::shared_ptr<MV::Scene::Node>&, const std::shared_ptr<MV::Scene::Button>&)>;
	auto repairScene = [&](const char* a_path, const SceneFixup& a_fixup) {
		if (a_onlyPath && std::strcmp(a_path, a_onlyPath) != 0) { return; }
		try {
			auto root = MV::Scene::Node::load(a_path, managers.services, false);
			fakeRoot->add(root);
			root->postLoadStep();
			auto button = root->componentInChildren<MV::Scene::Button>(false, false, true).self();
			std::vector<std::shared_ptr<MV::Scene::Node>> stale;
			collectStaleGlyphNodes(root, stale);
			if (button) {
				// Button state views are component-held nodes, not scene children - walk them too
				for (auto& viewNode : { button->activeNode(), button->idleNode(), button->disabledNode() }) {
					if (viewNode) {
						collectStaleGlyphNodes(viewNode, stale);
					}
				}
			}
			for (auto& staleNode : stale) {
				std::cout << "[fixlogin] removing stale glyph node " << staleNode->id() << std::endl;
				staleNode->removeFromParent();
			}
			if (a_fixup) {
				a_fixup(root, button);
			}
			root->saveJai(a_path, managers.services, false);
			std::cout << "[fixlogin] saved " << a_path << std::endl;
			root->removeFromParent();
		} catch (std::exception& e) {
			std::cout << "[fixlogin] FAILED " << a_path << ": " << e.what() << std::endl;
		}
	};
	repairScene("Assets/Interface/Login/view.scene", [&](const std::shared_ptr<MV::Scene::Node>& root, const std::shared_ptr<MV::Scene::Button>& button) {
		MV::BoxAABB<> inputBox(MV::point(99.0f, 9.0f), MV::point(299.0f, 44.0f));
		for (auto& inputId : { "EmailBackground", "PasswordBackground" }) {
			auto clickable = root->get(inputId)->component<MV::Scene::Clickable>().self();
			clickable->bounds(inputBox);
			// The conversion also dropped the focus-on-click receivers (legacy audit)
			clickable->onAccept.connect("script", "game.gui().page(\"Login\").focus(arg_0.owner().componentText());");
			retexture(root->get(inputId), "button.png");
		}
		retextureViews(button, MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(104.0f, 36.0f)));
	});
	repairScene("Assets/Prefabs/Button.bindsnap", [&](const std::shared_ptr<MV::Scene::Node>&, const std::shared_ptr<MV::Scene::Button>& button) {
		retextureViews(button, MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(104.0f, 47.0f)));
	});
	repairScene("Assets/Prefabs/SimpleButton.bindsnap", [&](const std::shared_ptr<MV::Scene::Node>&, const std::shared_ptr<MV::Scene::Button>& button) {
		button->bounds(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(100.0f, 30.0f)));
	});
	// The Play button's own rectangle is centred on its node (-75..75 x -20..20, anchored to the
	// ScreenScaler's centre); the conversion left its state views unanchored, untextured and unlabelled.
	repairScene("Assets/Interface/Main/view.scene", [&](const std::shared_ptr<MV::Scene::Node>&, const std::shared_ptr<MV::Scene::Button>& button) {
		MV::BoxAABB<> viewBox(MV::point(-75.0f, -20.0f), MV::point(75.0f, 20.0f));
		MV::BoxAABB<> fill(MV::point(0.0f, 0.0f), MV::point(1.0f, 1.0f));
		if (button->id().empty()) { button->id("Play"); }   // anchors persist their parent by component id
		for (auto& viewNode : { button->activeNode(), button->idleNode(), button->disabledNode() }) {
			viewNode->component<MV::Scene::Sprite>().self()->anchors().parent(button).anchor(fill).offset(MV::BoxAABB<>());
			auto label = viewNode->component<MV::Scene::Text>().self();
			label->anchors().parent(button).anchor(fill).offset(MV::BoxAABB<>());
			label->text(MV::UtfString("Play"));
		}
		retextureViews(button, viewBox);
	});
	repairScene("Assets/Prefabs/LoginName.bindsnap", nullptr);
}

// Evals every <a_assetType>/<id>/main(.script|Client.script) through the same
// StandardScriptMethods path the game uses; passes when the eval succeeds and the script
// bound at least one spawn/update/death hook — end-to-end proof on the configured backend.
template <typename DataType>
static void verifyFamilyScripts(MV::Script& a_script, const std::string& a_assetType, int& a_passed, int& a_failed) {
	std::error_code errorCode;
	for (std::filesystem::directory_iterator it("Assets/" + a_assetType, errorCode), end; !errorCode && it != end; it.increment(errorCode)) {
		if (!it->is_directory()) { continue; }
		auto id = it->path().filename().string();
		for (bool isServer : { true, false }) {
			std::string file = a_assetType + "/" + id + (isServer ? "/main.script" : "/mainClient.script");
			if (!MV::fileExistsInSearchPaths(file)) { continue; }
			StandardScriptMethods<DataType> methods;
			auto self = a_script.engine().make_object(
				std::shared_ptr<StandardScriptMethods<DataType>>(&methods, [](StandardScriptMethods<DataType>*) {}));
			bool ok = a_script.eval(file, MV::fileContents(file), { { "self", self } })
				&& (methods.scriptSpawn || methods.scriptUpdate || methods.scriptDeath);
			std::cout << (ok ? "[OK]   script " : "[FAIL] script ") << file << std::endl;
			if (ok) { ++a_passed; } else { ++a_failed; }
		}
	}
}

// Interface scripts read game/messages/self, so they are parsed + static-checked, never run;
// the host-provided names are registered the way Game does so the checker sees the same surface.
static void verifyInterfaceScripts(jai::engine& a_engine, Managers& a_managers, int& a_passed, int& a_failed) {
	a_engine.add_global("game", jai::script_value(std::monostate{}, &a_engine));
	a_engine.add_global("self", jai::script_value(std::monostate{}, &a_engine));
	a_engine.add_global("messages", a_engine.make_object(std::shared_ptr<StandardMessages>(&a_managers.messages, [](StandardMessages*) {})));
	a_engine.add_global("DefaultLoginId", jai::script_value(std::string(), &a_engine));
	a_engine.add_global("DefaultPassword", jai::script_value(std::string(), &a_engine));
	a_engine.add_global("AutoMatch", jai::script_value(false, &a_engine));
	std::error_code errorCode;
	for (std::filesystem::recursive_directory_iterator it("Assets/Interface", errorCode), end; !errorCode && it != end; it.increment(errorCode)) {
		if (!it->is_regular_file() || it->path().extension() != ".script") { continue; }
		auto file = it->path().generic_string();
		std::string detail;
		bool ok = false;
		try {
			auto source = MV::fileContents(file, true);
			auto report = a_engine.check(source);
			ok = !report.has_errors();
			if (!ok) { detail = report.format(source, file); }
		} catch (std::exception& e) {
			detail = e.what();
		}
		std::cout << (ok ? "[OK]   check " : "[FAIL] check ") << file << std::endl;
		if (!ok) { std::cout << detail << std::endl; }
		if (ok) { ++a_passed; } else { ++a_failed; }
	}
}

static std::map<std::string, int> typeTagCounts(const std::string& a_json) {
	std::map<std::string, int> counts;
	for (size_t at = a_json.find("\"$type\""); at != std::string::npos; at = a_json.find("\"$type\"", at + 7)) {
		size_t colon = a_json.find(':', at + 7);
		if (colon == std::string::npos) { break; }
		size_t open = a_json.find('"', colon + 1);
		size_t close = open == std::string::npos ? open : a_json.find('"', open + 1);
		if (close == std::string::npos) { break; }
		++counts[a_json.substr(open + 1, close - open - 1)];
	}
	return counts;
}

static std::string describeTypeTagDrift(const std::map<std::string, int>& a_original, const std::map<std::string, int>& a_saved) {
	std::string drift;
	auto note = [&](const std::string& a_type, int a_before, int a_after) {
		if (a_before == a_after) { return; }
		drift += (drift.empty() ? "" : ", ") + a_type + " " + std::to_string(a_before) + "->" + std::to_string(a_after);
	};
	for (auto& [type, count] : a_original) {
		auto saved = a_saved.find(type);
		note(type, count, saved == a_saved.end() ? 0 : saved->second);
	}
	for (auto& [type, count] : a_saved) {
		if (!a_original.count(type)) { note(type, 0, count); }
	}
	return drift;
}

static std::string describeJsonDifference(const std::string& a_first, const std::string& a_second) {
	size_t offset = 0;
	size_t shared = std::min(a_first.size(), a_second.size());
	while (offset < shared && a_first[offset] == a_second[offset]) { ++offset; }
	if (offset == shared && a_first.size() == a_second.size()) { return ""; }
	std::string key = "<root>";
	size_t keyEnd = a_first.rfind("\":", offset);
	if (keyEnd != std::string::npos && keyEnd > 0) {
		size_t keyStart = a_first.rfind('"', keyEnd - 1);
		if (keyStart != std::string::npos) { key = a_first.substr(keyStart + 1, keyEnd - keyStart - 1); }
	}
	auto context = [&](const std::string& a_text) { return a_text.substr(offset > 40 ? offset - 40 : 0, 80); };
	return "first difference at offset " + std::to_string(offset) + " near key [" + key + "] (sizes " + std::to_string(a_first.size()) + " vs " + std::to_string(a_second.size()) + ")"
		"\n    first:  ..." + context(a_first) + "...\n    second: ..." + context(a_second) + "...";
}

#include "Workbench/workbench.h"

// Launches the Workbench editor (Source/Workbench): dockable, dark-mode, reflection-driven.
// Run: BindstoneClient.exe -workbench   (-workbenchsmoke = 120-frame self-test, exits 0;
// -workbenchshot = smoke run that saves workbench_shot.bmp for visual inspection;
// -workbenchgizmoshot = shot that loads map.scene and selects a node so the gizmos render)
static int RunWorkbench(bool a_smoke, const std::string& a_screenshotPath = "", bool a_loadDefaultScene = false, bool a_selectForGizmo = false) {
	MV::editMode(true);   // before engine creation: registers InEditMode + hot reload
	Managers managers({ "", "" });
	auto jaiEngine = MV::makeScriptEngine(managers.services);
	// Editor tolerance: the sim's first spawn burst evals every creature script inside one
	// budget window in Debug; a mid-script budget kill leaves game state (path semaphores)
	// inconsistent. The shipped game keeps the tight default.
	jaiEngine->execution_budget(10.0);
	managers.services.connect<jai::engine>(jaiEngine.get());
	SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "1");
	managers.renderer.window().windowedMode()
		.borderless()
		.allowUserResize(false, MV::Size<int>(800, 600))
		.resizeWorldWithWindow(true);
	if (!managers.renderer.initialize(MV::Size<int>(1600, 900))) {
		std::cout << "[workbench] renderer init failed\n";
		return 1;
	}
	managers.renderer.window().setTitle("Bindstone Workbench");

	float workbenchUiScale = managers.renderer.window().uiScale();
	Workbench::Theme workbenchTheme(2.0f * workbenchUiScale);
	MV::FontDefinition::make(managers.textLibrary, "default", "Fonts/Verdana.ttf", 14);
	MV::FontDefinition::make(managers.textLibrary, "small", "Fonts/Verdana.ttf", 9);
	MV::FontDefinition::make(managers.textLibrary, "big", "Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);
	MV::FontDefinition::make(managers.textLibrary, "wb-small", "Fonts/Verdana.ttf", static_cast<int>(14 * workbenchUiScale));
	MV::FontDefinition::make(managers.textLibrary, "wb", "Fonts/Verdana.ttf", static_cast<int>(22 * workbenchUiScale));

	// The game's animated-material shaders; without them scene materials fall back to default.
	managers.renderer.loadShader("vortex", "Shaders/default.vert", "Shaders/vortex.frag");
	managers.renderer.loadShader("lillypad", "Shaders/lillypad.vert", "Shaders/default.frag");
	managers.renderer.loadShader("wave", "Shaders/wave.vert", "Shaders/wave.frag");
	managers.renderer.loadShader("waterfall", "Shaders/default.vert", "Shaders/waterfall.frag");
	managers.renderer.loadShader("pool", "Shaders/default.vert", "Shaders/pool.frag");
	managers.renderer.loadShader("shimmer", "Shaders/default.vert", "Shaders/shimmer.frag");

	int workbenchResizeBorder = std::max(6, static_cast<int>(8 * workbenchUiScale));
	int workbenchTitleBarHeight = static_cast<int>(workbenchTheme.titleBarHeight);
	int workbenchTitleButtonStrip = static_cast<int>(workbenchTheme.titleButtonWidth * 3.0f);
	int workbenchTransportStart = static_cast<int>(workbenchTheme.titleBarHeight * 8.0f);
	int workbenchTransportWidth = static_cast<int>(3.0f * (workbenchTheme.titleButtonWidth * 1.4f + 2.0f) + 8.0f);
	auto& workbenchWindow = managers.renderer.window();
	workbenchWindow.hitTest([&workbenchWindow, workbenchResizeBorder, workbenchTitleBarHeight, workbenchTitleButtonStrip, workbenchTransportStart, workbenchTransportWidth](const MV::Point<int>& a_point, const MV::Size<int>& a_size) -> SDL_HitTestResult {
		if (!workbenchWindow.maximized()) {
			bool onLeft = a_point.x < workbenchResizeBorder;
			bool onRight = a_point.x >= a_size.width - workbenchResizeBorder;
			bool onTop = a_point.y < workbenchResizeBorder;
			bool onBottom = a_point.y >= a_size.height - workbenchResizeBorder;
			if (onTop && onLeft) { return SDL_HITTEST_RESIZE_TOPLEFT; }
			if (onTop && onRight) { return SDL_HITTEST_RESIZE_TOPRIGHT; }
			if (onBottom && onLeft) { return SDL_HITTEST_RESIZE_BOTTOMLEFT; }
			if (onBottom && onRight) { return SDL_HITTEST_RESIZE_BOTTOMRIGHT; }
			if (onTop) { return SDL_HITTEST_RESIZE_TOP; }
			if (onBottom) { return SDL_HITTEST_RESIZE_BOTTOM; }
			if (onLeft) { return SDL_HITTEST_RESIZE_LEFT; }
			if (onRight) { return SDL_HITTEST_RESIZE_RIGHT; }
		}
		if (a_point.y < workbenchTitleBarHeight) {
			if (a_point.x >= a_size.width - workbenchTitleButtonStrip) {
				return SDL_HITTEST_NORMAL;
			}
			if (a_point.x >= workbenchTransportStart && a_point.x < workbenchTransportStart + workbenchTransportWidth) {
				return SDL_HITTEST_NORMAL;   // sim transport buttons (pause/step/speed/sim)
			}
			return SDL_HITTEST_DRAGGABLE;
		}
		return SDL_HITTEST_NORMAL;
	});

	MV::initializeSpineBindings();
	managers.textures.assemblePacks("Assets/Atlases", &managers.renderer);
	managers.textures.files("Assets/Map");
	managers.textures.files("Assets/Images");

	MV::TapDevice workbenchMouse;
	managers.services.connect(&workbenchMouse);
	Workbench::App app(managers, workbenchMouse, workbenchTheme);
	if (a_loadDefaultScene) {
		app.loadScene("Scenes/map.scene");
	}
	if (a_selectForGizmo && app.scene()) {
		// The sim auto-frame centers the map, so the node nearest the map center is the
		// one guaranteed on-screen for the gizmo to render against.
		auto mapBounds = app.scene()->bounds();
		auto center = MV::point(mapBounds.minPoint.x + mapBounds.size().width * 0.5f, mapBounds.minPoint.y + mapBounds.size().height * 0.5f);
		std::shared_ptr<MV::Scene::Node> best;
		float bestDistance = 0.0f;
		std::function<void(const std::shared_ptr<MV::Scene::Node>&)> visit = [&](const std::shared_ptr<MV::Scene::Node>& a_node) {
			auto box = a_node->screenBounds(false);
			if (box.size().width > 20.0f && box.size().height > 20.0f) {
				auto at = a_node->worldPosition();
				float distance = MV::distance(MV::point(at.x, at.y), center);
				if (!best || distance < bestDistance) {
					best = a_node;
					bestDistance = distance;
				}
			}
			for (auto&& child : *a_node) {
				visit(child);
			}
		};
		visit(app.scene());
		if (best) {
			app.select(best);
			app.frameSelectionInViewport();
		}
	}
	return a_smoke ? app.runFrames(a_loadDefaultScene ? 240 : 120, a_screenshotPath) : app.run();
}

// Loads every shipped scene/prefab/catalog headless and reports pass/fail. Guards the
// jai asset format end to end. Run: BindstoneClient.exe -verifyassets
static int RunAssetVerification() {
	Managers managers({ "", "" });
	auto jaiEngine = MV::makeScriptEngine(managers.services);
	managers.services.connect<jai::engine>(jaiEngine.get());
	static MV::TapDevice verifyMouse;
	managers.services.connect(&verifyMouse);
	managers.renderer.makeHeadless();
	if (!managers.renderer.initialize(MV::Size<int>(1280, 720))) {
		std::cout << "[verify] renderer init failed\n";
		return 1;
	}
	MV::FontDefinition::make(managers.textLibrary, "default", "Fonts/Verdana.ttf", 14);
	MV::FontDefinition::make(managers.textLibrary, "small", "Fonts/Verdana.ttf", 9);
	MV::FontDefinition::make(managers.textLibrary, "big", "Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);
	MV::initializeSpineBindings();

	auto fakeRoot = MV::Scene::Node::make(managers.renderer);
	fakeRoot->attach<MV::Scene::Sprite>()->hide()->id("ScreenScaler");

	const char* nodeFiles[] = {
		"Scenes/map.scene",
		"Assets/Interface/Login/view.scene",
		"Assets/Interface/Main/view.scene",
		"Assets/BattleEffects/Missile/Default/unit.bindsnap",
		"Assets/Buildings/Life/Default/building.bindsnap",
		"Assets/Buildings/Void/Default/building.bindsnap",
		"Assets/Creatures/Air_T1/Default/unit.bindsnap",
		"Assets/Creatures/Decay_T1/Default/unit.bindsnap",
		"Assets/Creatures/Earth_Pebble/Default/unit.bindsnap",
		"Assets/Creatures/Earth_T1/Default/unit.bindsnap",
		"Assets/Creatures/Fire_T1/Default/unit.bindsnap",
		"Assets/Creatures/Life_T1/Default/unit.bindsnap",
		"Assets/Creatures/Lightning_T1/Default/unit.bindsnap",
		"Assets/Creatures/VoidElemental/Default/unit.bindsnap",
		"Assets/Creatures/Void_T1/Default/unit.bindsnap",
		"Assets/Creatures/Water_DropletHeal/Default/unit.bindsnap",
		"Assets/Creatures/Water_DropletHurt/Default/unit.bindsnap",
		"Assets/Creatures/Water_T1/Default/unit.bindsnap",
		"Assets/Prefabs/Button.bindsnap",
		"Assets/Prefabs/Creatures/voidElemental/voidElemental.bindsnap",
		"Assets/Prefabs/Life_T1.bindsnap",
		"Assets/Prefabs/LoginName.bindsnap",
		"Assets/Prefabs/missile.bindsnap",
		"Assets/Prefabs/Missiles/missile.bindsnap",
		"Assets/Prefabs/SimpleButton.bindsnap",
	};
	int passed = 0, failed = 0;
	const auto scratchDirectory = std::filesystem::temp_directory_path() / "bindstone_verify";
	auto saveJsonToScratch = [&](const std::shared_ptr<MV::Scene::Node>& a_root, const char* a_name) {
		auto file = (scratchDirectory / a_name).string();
		a_root->saveJai(file, managers.services, false);
		return std::make_pair(file, MV::fileContents(file, true));
	};
	for (const char* path : nodeFiles) {
		try {
			auto original = MV::fileContents(path, true);
			auto root = MV::Scene::Node::load(path, managers.services, false);
			fakeRoot->add(root);
			root->postLoadStep();
			// Save and reload to exercise the save side too (catches unregistered-on-save
			// polymorphic types that a load-only check misses).
			auto blob = MV::toBinaryString(root, managers.services);
			auto reloaded = MV::fromBinaryString<std::shared_ptr<MV::Scene::Node>>(blob, managers.services);
			if (!reloaded) { throw std::runtime_error("save/reload round-trip returned null"); }
			auto [firstFile, firstJson] = saveJsonToScratch(root, "first.scene");
			root->removeFromParent();
			auto again = MV::Scene::Node::load(firstFile, managers.services, false);
			fakeRoot->add(again);
			again->postLoadStep();
			auto secondJson = saveJsonToScratch(again, "second.scene").second;
			again->removeFromParent();
			auto drift = describeJsonDifference(firstJson, secondJson);
			if (!drift.empty()) { throw std::runtime_error("json round-trip drift: " + drift); }
			auto typeDrift = describeTypeTagDrift(typeTagCounts(original), typeTagCounts(firstJson));
			if (!typeDrift.empty()) { throw std::runtime_error("$type multiset drift original->resave (sliced component?): " + typeDrift); }
			std::cout << "[OK]   " << path << " (root=" << root->id() << ", round-trip ok, json stable, " << typeTagCounts(original).size() << " $type kinds)" << std::endl;
			++passed;
		} catch (std::exception& e) {
			std::cout << "[FAIL] " << path << ": " << e.what() << std::endl;
			++failed;
		}
	}

	try {
		auto login = MV::Scene::Node::load("Assets/Interface/Login/view.scene", managers.services, false);
		fakeRoot->add(login);
		login->postLoadStep();
		auto button = login->get("LoginButton")->component<MV::Scene::Clickable>(false, true).self();
		auto receiver = button->onAccept.connection("onAccept");
		if (!receiver) { throw std::runtime_error("LoginButton onAccept has no owned receiver named [onAccept]"); }
		if (!receiver->script_engine()) { throw std::runtime_error("LoginButton onAccept receiver loaded without an engine (its script can never run)"); }
		login->removeFromParent();
		std::cout << "[OK]   LoginButton onAccept receiver armed: " << receiver->script() << std::endl;
		++passed;
	} catch (std::exception& e) {
		std::cout << "[FAIL] LoginButton onAccept receiver: " << e.what() << std::endl;
		++failed;
	}

	try {
		auto probeFile = (scratchDirectory / "clickable_receiver.scene").string();
		{
			auto probe = MV::Scene::Node::make(managers.renderer, "ReceiverProbe");
			probe->attach<MV::Scene::Clickable>(verifyMouse).self()->onAccept.connect("marker", "print(\"VERIFY_RECEIVER_FIRED\");");
			probe->saveJai(probeFile, managers.services, false);
		}
		auto loaded = MV::Scene::Node::load(probeFile, managers.services, false);
		fakeRoot->add(loaded);
		loaded->postLoadStep();
		auto capture = std::make_shared<std::ostringstream>();
		jaiEngine->set_output_stream(capture);
		loaded->component<MV::Scene::Clickable>(false, true).self()->press();
		jaiEngine->set_output_stream(nullptr);
		loaded->removeFromParent();
		if (capture->str().find("VERIFY_RECEIVER_FIRED") == std::string::npos) { throw std::runtime_error("owned script receiver loaded from JSON did not run on press()"); }
		std::cout << "[OK]   loaded Clickable script receiver fires on press()" << std::endl;
		++passed;
	} catch (std::exception& e) {
		jaiEngine->set_output_stream(nullptr);
		std::cout << "[FAIL] loaded Clickable script receiver: " << e.what() << std::endl;
		++failed;
	}

	// Matchmaking-wire round-trip: each NetworkAction is serialized through shared_ptr<NetworkAction>.
	auto checkAction = [&](const char* name, bool ok) {
		std::cout << (ok ? "[OK]   NetworkAction " : "[FAIL] NetworkAction ") << name << std::endl;
		if (ok) { ++passed; } else { ++failed; }
	};
	checkAction("MessageAction", roundtripNetworkAction<MessageAction>(managers.services));
	checkAction("LoginResponse", roundtripNetworkAction<LoginResponse>(managers.services));
	try {
		GameData catalogCheck(managers, false);
		std::cout << "[OK]   Catalogs (creatures/buildings/battleEffects)" << std::endl;
		++passed;
	} catch (std::exception& e) {
		std::cout << "[FAIL] Catalogs: " << e.what() << std::endl;
		++failed;
	}

	// Battle wire: the pool variants a live match streams (creature/building states) must survive the binary trip.
	try {
		GameData wireData(managers, false);
		BindstoneNetworkObjectPool pool;
		auto creatureState = std::make_shared<CreatureNetworkState>(wireData.creatures().data("Life_T1"), 0);
		pool.spawn(creatureState);
		pool.spawn(std::make_shared<BuildingNetworkState>(0));
		auto creatureBack = MV::fromBinaryString<std::shared_ptr<CreatureNetworkState>>(MV::toBinaryString(creatureState, managers.services), managers.services);
		if (!creatureBack || *creatureBack->creatureTypeId != *creatureState->creatureTypeId) { throw std::runtime_error("CreatureNetworkState did not round-trip"); }
		std::shared_ptr<NetworkAction> sync = std::make_shared<SynchronizeAction>(pool.all());
		auto wire = MV::toBinaryStringCast<NetworkAction>(sync, managers.services);
		auto back = MV::fromBinaryString<std::shared_ptr<NetworkAction>>(wire, managers.services);
		if (!std::dynamic_pointer_cast<SynchronizeAction>(back)) { throw std::runtime_error("SynchronizeAction did not round-trip"); }
		auto again = MV::toBinaryStringCast<NetworkAction>(back, managers.services);
		if (again.size() != wire.size()) { throw std::runtime_error("SynchronizeAction re-serialized to a different size (" + std::to_string(wire.size()) + " vs " + std::to_string(again.size()) + ")"); }
		std::cout << "[OK]   Battle wire (SynchronizeAction with creature+building states, " << wire.size() << " bytes)" << std::endl;
		++passed;
	} catch (std::exception& e) {
		std::cout << "[FAIL] Battle wire: " << e.what() << std::endl;
		++failed;
	}

	MV::Script script(*jaiEngine);
	verifyFamilyScripts<Creature>(script, "Creatures", passed, failed);
	verifyFamilyScripts<Building>(script, "Buildings", passed, failed);
	verifyFamilyScripts<BattleEffect>(script, "BattleEffects", passed, failed);
	verifyInterfaceScripts(*jaiEngine, managers, passed, failed);

	std::cout << "\n[verify] " << passed << " passed, " << failed << " failed" << std::endl;
	return failed == 0 ? 0 : 1;
}

// CPU baseline for the particle hot loops (single-threaded, no GL): Particle::update (the per-frame
// per-particle simulation, incl. the per-particle TransformMatrix) and the vertex-generation loop
// from loadParticlesToPoints. Establishes a "before" number for perf work. Run: BindstoneClient.exe -emitterbench
static void RunEmitterBenchmark() {
	using namespace MV;
	std::cout << "\n=== Emitter CPU baseline (single-thread; Particle::update + vertex gen, no GL) ===\n";
	const int frames = 120;
	const size_t counts[] = { 1000, 10000, 100000 };
	for (size_t n : counts) {
		std::vector<Scene::Particle> particles(n);
		for (auto& p : particles) {
			p.change.maxLifespan = 1000.0f;             // stay alive across the whole run
			p.change.beginSpeed = 120.0f; p.change.endSpeed = 30.0f;
			p.change.beginScale = Scale(8.0f, 8.0f); p.change.endScale = Scale(2.0f, 2.0f);
			p.change.beginColor = Color(1.0f, 1.0f, 1.0f, 1.0f); p.change.endColor = Color(1.0f, 0.0f, 0.0f, 0.0f);
			p.change.rateOfChange = AxisAngles(0.0f, 0.0f, 0.1f);
			p.change.rotationalChange = AxisAngles(0.0f, 0.0f, 0.5f);
			p.change.directionalChangeRad(AxisAngles(0.0f, 0.0f, 0.2f));
			p.direction = AxisAngles(0.0f, 0.0f, randomNumber(0.0f, 6.28318f));
			p.scale = Scale(6.0f, 6.0f);
			p.rotation = AxisAngles(0.0f, 0.0f, randomNumber(0.0f, 6.28318f));
			p.setGravity(40.0f);
		}

		volatile double sink = 0.0;

		auto u0 = std::chrono::steady_clock::now();
		for (int f = 0; f < frames; ++f) {
			for (auto& p : particles) { p.update(1.0 / 60.0); }
		}
		auto u1 = std::chrono::steady_clock::now();
		sink += particles[0].position.x;
		double updNs = std::chrono::duration<double, std::nano>(u1 - u0).count() / (double(n) * frames);

		std::vector<DrawPoint> pts; pts.reserve(n * 4);
		std::vector<GLuint> idx; idx.reserve(n * 6);
		TexturePoint tp[4] = { {0.0f,0.0f}, {0.0f,1.0f}, {1.0f,1.0f}, {1.0f,0.0f} };
		auto v0 = std::chrono::steady_clock::now();
		pts.clear(); idx.clear();
		for (auto& particle : particles) {
			BoxAABB<> bounds(Point<>(particle.scale.x / -2.0f, particle.scale.y / -2.0f, 0.0f), Point<>(particle.scale.x / 2.0f, particle.scale.y / 2.0f, 0.0f));
			bounds.sanitize();
			Scene::appendQuadVertexIndices(idx, static_cast<GLuint>(pts.size()));
			auto c = cos(particle.rotation.z);
			auto s = sin(particle.rotation.z);
			for (size_t i = 0; i < 4; ++i) {
				auto corner = bounds[i];
				rotatePoint2DRad(corner.x, corner.y, c, s);
				corner += particle.position;
				pts.emplace_back(corner, particle.color, tp[i]);
			}
		}
		auto v1 = std::chrono::steady_clock::now();
		sink += pts.empty() ? 0.0 : pts[0].x;
		double vtxNs = std::chrono::duration<double, std::nano>(v1 - v0).count() / double(n);

		std::printf("  %7zu particles:  update %6.1f ns/particle  (=%6.3f ms/frame)   vertexgen %5.1f ns/particle  (=%6.3f ms)   [sink=%.1f]\n",
			n, updNs, updNs * n / 1e6, vtxNs, vtxNs * n / 1e6, (double)sink);
	}
}

#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
static void InstallTerminateTracer() {
	std::set_terminate([] {
		fprintf(stderr, "[terminate] std::terminate reached\n");
		if (auto ex = std::current_exception()) {
			try { std::rethrow_exception(ex); }
			catch (std::exception& e) { fprintf(stderr, "[terminate] active exception: %s\n", e.what()); }
			catch (...) { fprintf(stderr, "[terminate] active non-std exception\n"); }
		} else {
			fprintf(stderr, "[terminate] no active exception\n");
		}
		void* frames[62];
		USHORT count = CaptureStackBackTrace(0, 62, frames, nullptr);
		HANDLE proc = GetCurrentProcess();
		SymInitialize(proc, nullptr, TRUE);
		char buf[sizeof(SYMBOL_INFO) + 256] = {};
		for (USHORT i = 0; i < count; ++i) {
			auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
			sym->MaxNameLen = 255;
			sym->SizeOfStruct = sizeof(SYMBOL_INFO);
			DWORD64 disp = 0;
			if (SymFromAddr(proc, reinterpret_cast<DWORD64>(frames[i]), &disp, sym)) {
				fprintf(stderr, "  %02d %s +0x%llx\n", i, sym->Name, static_cast<unsigned long long>(disp));
			} else {
				fprintf(stderr, "  %02d %p\n", i, frames[i]);
			}
		}
		fflush(stderr);
		abort();
	});
}

int main(int argc, char *argv[]) {
	InstallTerminateTracer();
	std::string name;
	std::string pass;
	bool autoStart = false;
	for (int i = 0; i < argc-1; ++i) {
		if (strcmp(argv[i], "-n") == 0) {
			name = argv[i + 1];
			std::cout << "Got name: " << name << std::endl;
		} else if (strcmp(argv[i], "-p") == 0) {
			pass = argv[i + 1];
			std::cout << "Got pass: " << pass << std::endl;
		}
	}

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--no-script-debug") == 0) {
			MV::scriptDebugEnabled(false);
		}
	}
	auto servicesEngine = MV::makeScriptEngine(MV::Services::instance());
	MV::Services::instance().connect<jai::engine>(servicesEngine.get());

	for (int i = 0; i < argc; ++i) {
		if (strcmp(argv[i], "-auto") == 0) {
			autoStart = true;
		}
		if (strcmp(argv[i], "-bench") == 0) {
			RunJsonParseBenchmark(servicesEngine.get());
			return 0;
		}
		if (strcmp(argv[i], "-loadbench") == 0) {
			RunSceneLoadBenchmark(true);
			return 0;
		}
		if (strcmp(argv[i], "-loadbenchgl") == 0) {
			RunSceneLoadBenchmark(false);
			return 0;
		}
		if (strcmp(argv[i], "-verifyassets") == 0) {
			return RunAssetVerification();
		}
		if (strcmp(argv[i], "-wirebuttons") == 0) {
			RunButtonWiring();
			return 0;
		}
		if (strcmp(argv[i], "-fixloginui") == 0) {
			RunLoginUiRepair();
			return 0;
		}
		if (strcmp(argv[i], "-fixmainui") == 0) {
			RunLoginUiRepair("Assets/Interface/Main/view.scene");
			return 0;
		}
		if (strcmp(argv[i], "-emitterbench") == 0) {
			RunEmitterBenchmark();
			return 0;
		}
		if (strcmp(argv[i], "-workbench") == 0) {
			return RunWorkbench(false);
		}
		if (strcmp(argv[i], "-workbenchsmoke") == 0) {
			return RunWorkbench(true);
		}
		if (strcmp(argv[i], "-workbenchshot") == 0) {
			return RunWorkbench(true, "workbench_shot.bmp");
		}
		if (strcmp(argv[i], "-workbenchgizmoshot") == 0) {
			return RunWorkbench(true, "workbench_shot.bmp", true, true);
		}
		if (strcmp(argv[i], "-drive") == 0 && i + 1 < argc) {
			GameEditor menu(name, pass, autoStart);
			return menu.runDriver(argv[i + 1]);
		}
		if (strcmp(argv[i], "-drivelive") == 0 && i + 1 < argc) {
			GameEditor menu(name, pass, autoStart);
			return menu.runDriver(argv[i + 1], true);
		}
		if (strcmp(argv[i], "-loginshot") == 0) {
			int frames = 120;
			if (i + 1 < argc) {
				if (int parsed = std::atoi(argv[i + 1]); parsed > 0) {
					frames = parsed;
				}
			}
			GameEditor menu(name, pass, true);
			return menu.runFrames(frames, "login_shot.bmp");
		}
	}

	GameEditor menu(name, pass, autoStart);

	menu.start();
	
	return 0;
}
