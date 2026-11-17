#include "Game/gameEditor.h"
#include "Game/Instance/arenaInstance.h"
#include "Game/NetworkLayer/synchronizeAction.h"
#include <jaiscript/thread_pool.hpp>
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
#include "fast_json.hpp"
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
				managers.pool.run_posted();
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
// Run: BindstoneClient.exe -workbench   (-workbenchsmoke = 120-frame self-test;
// -workbenchshot = smoke run that saves workbench_shot.bmp for visual inspection;
// -workbenchgizmoshot = shot that loads map.scene and selects a node so the gizmos render).
// -workbenchselftest = drives a load, save, select, BindSnap drop and undo/redo, checking each result.
// The smoke modes exit nonzero when a load fails, a script errors or the frame loop catches an exception.
static void RunWorkbenchSelfTest(Workbench::App& a_app, Managers& a_managers) {
	auto expect = [&a_app](bool a_condition, const std::string& a_what) {
		if (!a_condition) {
			a_app.reportError("self-test: " + a_what);
		}
	};
	auto sameXY = [](const auto& a_left, const auto& a_right) {
		return std::abs(a_left.x - a_right.x) < 0.001f && std::abs(a_left.y - a_right.y) < 0.001f;
	};
	auto pressWithControl = [&a_app](SDL_Keycode a_key) {
		SDL_SetModState(static_cast<SDL_Keymod>(KMOD_CTRL));
		SDL_Event press{};
		press.type = SDL_KEYDOWN;
		press.key.keysym.sym = a_key;
		SDL_PushEvent(&press);
		a_app.runFrames(2);
		SDL_SetModState(KMOD_NONE);
	};

	a_app.newScene();
	expect(a_app.scene()->empty() && !a_app.selected() && a_app.errors() == 0, "File > New Scene leaves an empty scene");

	expect(a_app.loadScene("Creatures/Life_T1/Default/unit.bindsnap"), "the unit BindSnap loads");
	expect(a_app.scene()->id() == "Life_T1", "a loaded root keeps its own id, got " + a_app.scene()->id());
	expect(!a_app.scene()->empty(), "the unit has a child to select");
	if (!a_app.scene()->empty()) {
		a_app.select(*a_app.scene()->begin());
	}
	a_app.runFrames(2);

	expect(a_app.loadScene("Interface/Login/view.scene"), "a second scene loads over a selection");
	expect(!a_app.selected(), "a load clears the selection");
	auto authoredId = a_app.scene()->id();
	auto authoredPosition = a_app.scene()->position();
	auto authoredScale = a_app.scene()->scale();
	auto authoredBounds = a_app.scene()->worldBounds().size();
	expect(authoredBounds.width > 1.0f && authoredBounds.height > 1.0f, "the Login scene has bounds for the camera to frame");
	a_app.runFrames(10);   // the load frames the scene: the camera moves, the document must not
	auto framedBounds = a_app.scene()->worldBounds().size();
	expect(std::abs(framedBounds.width - authoredBounds.width) > 1.0f || !sameXY(a_app.scene()->worldPosition(), authoredPosition), "framing the scene moves the camera");
	expect(sameXY(a_app.scene()->position(), authoredPosition) && sameXY(a_app.scene()->scale(), authoredScale), "the camera leaves the document root's transform alone");

	auto savedFile = (std::filesystem::temp_directory_path() / "workbench_selftest.scene").string();
	expect(a_app.saveScene(savedFile), "the scene saves");
	try {
		auto saved = MV::Scene::Node::load(savedFile, a_managers.services, false);
		expect(saved->id() == authoredId, "the saved root keeps its id, got " + saved->id());
		expect(sameXY(saved->position(), authoredPosition) && sameXY(saved->scale(), authoredScale), "the saved root keeps its authored transform");
	} catch (const std::exception& a_error) {
		expect(false, std::string("the saved scene loads back: ") + a_error.what());
	}
	std::error_code ignored;
	std::filesystem::remove(savedFile, ignored);

	auto childCount = [&a_app]() { return std::distance(a_app.scene()->begin(), a_app.scene()->end()); };
	auto countBeforeDrop = childCount();
	auto windowSize = a_managers.renderer.window().windowSize();
	a_app.dropPrefab("Prefabs/SimpleButton.bindsnap", MV::Point<int>(windowSize.width / 2, windowSize.height / 2));
	expect(childCount() == countBeforeDrop + 1, "a BindSnap dropped on the viewport joins the scene");
	expect(a_app.selected() && a_app.selected()->parent() == a_app.scene(), "the dropped BindSnap is selected");
	a_app.runFrames(2);
	pressWithControl(SDLK_z);
	expect(childCount() == countBeforeDrop, "undo removes the dropped BindSnap");
	expect(!a_app.selected(), "undo deselects a node that left the scene");
	pressWithControl(SDLK_y);
	expect(childCount() == countBeforeDrop + 1, "redo places the BindSnap again");

	// Transform gizmos: each gesture through the calls a handle drag makes, then real drags on a simulated mouse
	auto checkTransformGizmos = [&]() {
		if (a_app.scene()->empty()) {
			return;
		}
		auto& gizmoLayer = a_app.gizmoLayer();
		auto& mouse = *a_managers.services.get<MV::TapDevice>();
		auto& renderer = a_managers.renderer;
		auto exactXY = [](const auto& a_left, const auto& a_right) { return a_left.x == a_right.x && a_left.y == a_right.y; };
		auto pressKey = [&a_app](SDL_Keycode a_key) {
			SDL_Event press{};
			press.type = SDL_KEYDOWN;
			press.key.keysym.sym = a_key;
			SDL_PushEvent(&press);
			a_app.runFrames(2);
		};
		enum class Button { LEFT, MIDDLE, RIGHT };
		auto drag = [&](const MV::Point<int>& a_from, const std::vector<MV::Point<int>>& a_path, Button a_button = Button::LEFT) {
			auto hold = [&](const MV::Point<int>& a_at, bool a_down, int a_frames) {
				mouse.simulate(a_at, a_down && a_button == Button::LEFT, a_down && a_button == Button::RIGHT, a_down && a_button == Button::MIDDLE);
				a_app.runFrames(a_frames);
			};
			hold(a_from, false, 2);
			hold(a_from, true, 2);
			for (auto&& point : a_path) {
				hold(point, true, 1);
			}
			hold(a_path.back(), false, 2);
			mouse.stopSimulating();
		};
		auto insideViewport = [&](const MV::BoxAABB<>& a_worldBox) {
			auto view = a_app.viewport();
			return view.contains(a_worldBox.minPoint) && view.contains(a_worldBox.maxPoint);
		};

		expect(!gizmoLayer.moveSelected(Workbench::GizmoAxis::X, MV::point(5.0f, 0.0f)), "a gesture needs a selection");
		auto subject = *std::prev(a_app.scene()->end());
		auto parent = subject->parent();
		a_app.select(subject);
		a_app.frameSelectionInViewport();
		a_app.runFrames(3);
		auto view = a_app.viewport();
		auto framed = subject->worldBounds();
		expect(insideViewport(framed), "framing the selection puts its bounds inside the viewport");
		float filled = std::max(framed.size().width / view.size.width, framed.size().height / view.size.height);
		expect(filled > 0.65f && filled < 0.75f, "framing the selection fills about 70% of the viewport, got " + std::to_string(filled));

		auto restPosition = subject->position();
		auto restRotation = subject->rotationRad();
		auto restScale = subject->scale();

		expect(a_app.gizmoTool() == Workbench::Tool::MOVE, "the move tool is the default");
		expect(gizmoLayer.moveSelected(Workbench::GizmoAxis::X, MV::point(37.0f, 91.0f)), "a move applies to the selection");
		expect(sameXY(subject->position(), restPosition + MV::point(37.0f, 0.0f)), "a move on X leaves Y alone");
		expect(gizmoLayer.moveSelected(Workbench::GizmoAxis::Y, MV::point(55.0f, -12.0f)), "a second move applies");
		expect(sameXY(subject->position(), restPosition + MV::point(37.0f, -12.0f)), "a move on Y leaves X alone");
		pressWithControl(SDLK_z);
		expect(sameXY(subject->position(), restPosition + MV::point(37.0f, 0.0f)), "one undo takes back one move");
		pressWithControl(SDLK_z);
		expect(exactXY(subject->position(), restPosition), "undo restores the position exactly");
		pressWithControl(SDLK_y);
		expect(sameXY(subject->position(), restPosition + MV::point(37.0f, 0.0f)), "redo moves again");
		pressWithControl(SDLK_z);
		gizmoLayer.moveSelected(Workbench::GizmoAxis::BOTH, MV::point(3.3f, 16.2f), true);
		auto snappedPosition = subject->position();
		expect(std::abs(std::remainder(snappedPosition.x, gizmoLayer.snapSteps.moveX)) < 0.001f && std::abs(std::remainder(snappedPosition.y, gizmoLayer.snapSteps.moveY)) < 0.001f, "a snapped move lands on the grid");
		pressWithControl(SDLK_z);
		expect(exactXY(subject->position(), restPosition), "undo restores the position after a snapped move");

		gizmoLayer.rotateSelected(40.0f);
		expect(std::abs(subject->rotation().z - (MV::toDegrees(restRotation.z) + 40.0f)) < 0.001f, "a rotation turns about Z");
		expect(exactXY(subject->position(), restPosition) && exactXY(subject->scale(), restScale), "a rotation leaves position and scale alone");
		pressWithControl(SDLK_z);
		expect(subject->rotationRad().z == restRotation.z, "undo restores the rotation exactly");
		gizmoLayer.rotateSelected(40.0f, true);
		expect(std::abs(std::remainder(subject->rotation().z, gizmoLayer.snapSteps.rotate)) < 0.001f, "a snapped rotation lands on the rotation step");
		pressWithControl(SDLK_z);
		expect(subject->rotationRad().z == restRotation.z, "undo restores the rotation after a snapped turn");

		gizmoLayer.scaleSelected(Workbench::GizmoAxis::X, 2.0f);
		expect(std::abs(subject->scale().x - restScale.x * 2.0f) < 0.001f && subject->scale().y == restScale.y, "a scale on X leaves Y alone");
		pressWithControl(SDLK_z);
		gizmoLayer.scaleSelected(Workbench::GizmoAxis::BOTH, 1.5f);
		expect(std::abs(subject->scale().x - restScale.x * 1.5f) < 0.001f && std::abs(subject->scale().y - restScale.y * 1.5f) < 0.001f, "the centre handle scales both axes");
		pressWithControl(SDLK_z);
		expect(exactXY(subject->scale(), restScale), "undo restores the scale exactly");
		gizmoLayer.scaleSelected(Workbench::GizmoAxis::Y, 1.234f, true);
		expect(std::abs(std::remainder(subject->scale().y, gizmoLayer.snapSteps.scale)) < 0.001f, "a snapped scale lands on a 0.1 step");
		pressWithControl(SDLK_z);
		expect(exactXY(subject->scale(), restScale), "undo restores the scale after a snapped one");

		pressKey(SDLK_q);
		expect(a_app.gizmoTool() == Workbench::Tool::SELECT && !gizmoLayer.handleScreenPoint("moveX"), "Q shows no transform handles");
		pressKey(SDLK_e);
		expect(a_app.gizmoTool() == Workbench::Tool::ROTATE && gizmoLayer.handleScreenPoint("rotate"), "E shows the rotate ring");
		pressKey(SDLK_r);
		expect(a_app.gizmoTool() == Workbench::Tool::SCALE && gizmoLayer.handleScreenPoint("scaleX") && gizmoLayer.handleScreenPoint("scaleUniform"), "R shows the scale handles");
		pressKey(SDLK_w);
		expect(a_app.gizmoTool() == Workbench::Tool::MOVE && gizmoLayer.handleScreenPoint("moveX") && gizmoLayer.handleScreenPoint("moveY") && gizmoLayer.handleScreenPoint("moveFree"), "W shows the move handles");
		expect(gizmoLayer.handleScreenPoint("moveY").value_or(MV::Point<int>()).y < gizmoLayer.handleScreenPoint("moveFree").value_or(MV::Point<int>()).y, "the vertical move arrow points up");
		if (auto editable = subject->component<MV::Scene::Clickable>(false, false)) {
			gizmoLayer.editComponent(editable.self());
			a_app.runFrames(2);
			expect(gizmoLayer.editedComponent() == editable.self() && !gizmoLayer.handleScreenPoint("moveX"), "a component gizmo that takes input hides the transform handles");
			gizmoLayer.editComponent(nullptr);
			a_app.runFrames(2);
			expect(gizmoLayer.handleScreenPoint("moveX").has_value(), "the transform handles return when the component gizmo is switched off");
		}

		auto grab = [&](const std::string& a_handle) {
			auto found = gizmoLayer.handleScreenPoint(a_handle);
			expect(found && view.contains(renderer.worldFromScreen(found.value_or(MV::Point<int>()))), "the " + a_handle + " handle is inside the viewport");
			return found.value_or(MV::Point<int>());
		};
		auto cameraBefore = a_app.scene()->worldPosition();

		auto moveGrab = grab("moveX");
		auto moveDrop = moveGrab + MV::Point<int>(60, 25);
		auto expectedMove = parent->localFromScreen(moveDrop) - parent->localFromScreen(moveGrab);
		drag(moveGrab, { moveGrab + MV::Point<int>(20, 10), moveDrop });
		expect(std::abs(subject->position().x - (restPosition.x + expectedMove.x)) < 0.01f && subject->position().y == restPosition.y, "dragging the X arrow moves along X by the cursor's travel");
		expect(sameXY(a_app.scene()->worldPosition(), cameraBefore), "a handle drag does not pan the camera");
		pressWithControl(SDLK_z);
		expect(exactXY(subject->position(), restPosition), "undo restores the position after an arrow drag");

		moveGrab = grab("moveY");
		SDL_SetModState(static_cast<SDL_Keymod>(KMOD_CTRL));
		drag(moveGrab, { moveGrab + MV::Point<int>(31, 47) });
		SDL_SetModState(KMOD_NONE);
		expect(subject->position().x == restPosition.x && subject->position().y != restPosition.y && std::abs(std::remainder(subject->position().y, gizmoLayer.snapSteps.moveY)) < 0.001f, "Ctrl snaps an arrow drag to the grid");
		pressWithControl(SDLK_z);

		moveGrab = grab("moveX");
		SDL_SetModState(static_cast<SDL_Keymod>(KMOD_SHIFT));
		drag(moveGrab, { moveGrab + MV::Point<int>(47, 9) });
		SDL_SetModState(KMOD_NONE);
		expect(subject->position().x != restPosition.x && subject->position().y == restPosition.y && std::abs(std::remainder(subject->position().x, gizmoLayer.snapSteps.moveX)) < 0.001f, "Shift snaps an arrow drag to the grid too");
		pressWithControl(SDLK_z);

		moveGrab = grab("moveFree");
		moveDrop = moveGrab + MV::Point<int>(-40, 33);
		expectedMove = parent->localFromScreen(moveDrop) - parent->localFromScreen(moveGrab);
		drag(moveGrab, { moveDrop });
		expect(sameXY(subject->position(), restPosition + expectedMove), "dragging the centre square moves freely");
		pressWithControl(SDLK_z);
		expect(exactXY(subject->position(), restPosition), "undo restores the position after a free drag");

		pressKey(SDLK_e);
		auto pivot = MV::cast<float>(subject->screenPosition());
		auto ringGrab = grab("rotate");
		auto spoke = MV::cast<float>(ringGrab) - pivot;
		std::vector<MV::Point<int>> arc;
		for (int degrees = 10; degrees <= 90; degrees += 10) {
			float turned = MV::toRadians(static_cast<float>(degrees));
			arc.push_back(MV::round<int>(pivot + MV::point(spoke.x * std::cos(turned) - spoke.y * std::sin(turned), spoke.x * std::sin(turned) + spoke.y * std::cos(turned))));
		}
		drag(ringGrab, arc);
		expect(std::abs(subject->rotation().z - (MV::toDegrees(restRotation.z) + 90.0f)) < 1.5f, "dragging a quarter of the ring turns 90 degrees, got " + std::to_string(subject->rotation().z));
		pressWithControl(SDLK_z);
		expect(subject->rotationRad().z == restRotation.z, "undo restores the rotation after a ring drag");

		// the sweep is drawn the way the pointer went: checked while the button is still down
		mouse.simulate(ringGrab, false);
		a_app.runFrames(2);
		mouse.simulate(ringGrab, true);
		a_app.runFrames(2);
		for (int point = 0; point < 4; ++point) {
			mouse.simulate(arc[static_cast<size_t>(point)], true);
			a_app.runFrames(1);
		}
		expect(gizmoLayer.sweepShown() > 25.0f && gizmoLayer.sweepShown() < 55.0f, "the rotate sweep fans out the way the pointer turned, showing " + std::to_string(gizmoLayer.sweepShown()));
		mouse.simulate(arc[3], false);
		a_app.runFrames(2);
		mouse.stopSimulating();
		expect(gizmoLayer.sweepShown() == 0.0f, "and is gone at release");
		pressWithControl(SDLK_z);

		pressKey(SDLK_r);
		auto scaleGrab = grab("scaleX");
		auto alongAxis = MV::cast<float>(scaleGrab) - pivot;
		drag(scaleGrab, { MV::round<int>(MV::cast<float>(scaleGrab) + alongAxis * (0.5f / 0.6f)) });
		expect(std::abs(subject->scale().x - restScale.x * 1.5f) < 0.03f && subject->scale().y == restScale.y, "dragging the X scale handle half its length scales X by 1.5, got " + std::to_string(subject->scale().x));
		pressWithControl(SDLK_z);
		expect(exactXY(subject->scale(), restScale), "undo restores the scale after a handle drag");
		scaleGrab = grab("scaleY");
		expect(scaleGrab.y < MV::round<int>(pivot).y, "the vertical scale handle points up");
		alongAxis = MV::cast<float>(scaleGrab) - pivot;
		drag(scaleGrab, { MV::round<int>(MV::cast<float>(scaleGrab) + alongAxis * (0.5f / 0.6f)) });
		expect(std::abs(subject->scale().y - restScale.y * 1.5f) < 0.03f && subject->scale().x == restScale.x, "dragging the Y scale handle up half its length scales Y by 1.5, got " + std::to_string(subject->scale().y));
		pressWithControl(SDLK_z);
		pressKey(SDLK_w);

		auto viewCenter = renderer.screenFromWorld(MV::point(view.position.x + view.size.width * 0.5f, view.position.y + view.size.height * 0.25f));
		for (auto button : { Button::MIDDLE, Button::RIGHT }) {
			cameraBefore = a_app.scene()->worldPosition();
			drag(viewCenter, { viewCenter + MV::Point<int>(80, -40) }, button);
			auto panned = a_app.scene()->worldPosition() - cameraBefore;
			auto expectedPan = renderer.worldFromScreen(MV::Point<int>(80, -40));
			expect(std::abs(panned.x - expectedPan.x) < 0.5f && std::abs(panned.y - expectedPan.y) < 0.5f, std::string(button == Button::MIDDLE ? "a middle" : "a right") + " drag pans the viewport");
		}
		expect(a_app.selected() == subject && exactXY(subject->position(), restPosition), "panning keeps the selection and the document");

		pressKey(SDLK_f);
		a_app.runFrames(2);
		expect(insideViewport(subject->worldBounds()), "F frames the selection again after a pan");
		a_app.select(nullptr);
		pressKey(SDLK_f);
		a_app.runFrames(2);
		expect(insideViewport(a_app.scene()->worldBounds()), "F with nothing selected frames the whole scene");
	};
	checkTransformGizmos();

	// The history: step labels in the Edit menu, a group as one step, delete and duplicate, the saved mark and the
	// question a modified scene asks before it is replaced
	if (std::distance(a_app.scene()->begin(), a_app.scene()->end()) >= 2) {
		using Choice = Workbench::App::UnsavedChoice;
		auto& history = a_app.commands();
		auto& gizmoLayer = a_app.gizmoLayer();
		auto& mouse = *a_managers.services.get<MV::TapDevice>();
		auto click = [&](const MV::Point<int>& a_at) {
			for (bool down : { false, true, false }) {
				mouse.simulate(a_at, down);
				a_app.runFrames(2);
			}
			mouse.stopSimulating();
		};
		auto pressKey = [&a_app](SDL_Keycode a_key) {
			SDL_Event press{};
			press.type = SDL_KEYDOWN;
			press.key.keysym.sym = a_key;
			SDL_PushEvent(&press);
			a_app.runFrames(2);
		};
		auto first = *a_app.scene()->begin();
		auto last = *std::prev(a_app.scene()->end());
		auto firstRest = first->position();
		auto lastRest = last->position();
		auto moveBy = [&](const std::shared_ptr<MV::Scene::Node>& a_node, const MV::Point<>& a_delta) {
			a_app.select(a_node);
			a_app.runFrames(1);
			return gizmoLayer.moveSelected(Workbench::GizmoAxis::BOTH, a_delta);
		};

		expect(moveBy(last, MV::point(20.0f, 10.0f)) && history.undoLabel() == "move " + last->id(), "a step carries a label for what it did, got " + history.undoLabel());
		click(a_app.menus().titleScreenPoint(1));
		expect(a_app.menus().isOpen() && a_app.menus().rowLabel(0) == "Undo move " + last->id() && a_app.menus().rowEnabled(0), "Edit names the step Undo takes back");
		expect(a_app.menus().isOpen() && a_app.menus().rowLabel(1) == "Redo" && !a_app.menus().rowEnabled(1), "Redo is plain and disabled with nothing to redo");
		if (a_app.menus().isOpen()) {
			click(a_app.menus().rowScreenPoint(1));
			expect(a_app.menus().isOpen() && !sameXY(last->position(), lastRest), "a disabled row does nothing when pressed");
			click(a_app.menus().rowScreenPoint(0));
			expect(!a_app.menus().isOpen() && sameXY(last->position(), lastRest), "Edit > Undo takes the move back");
		}
		expect(history.redoLabel() == "move " + last->id() && history.undoLabel() != history.redoLabel(), "the undone step is the one Redo names");

		{
			auto bothMoves = history.group("move both");
			moveBy(first, MV::point(15.0f, 0.0f));
			moveBy(last, MV::point(0.0f, 25.0f));
			moveBy(last, MV::point(40.0f, 0.0f));
		}
		expect(history.undoLabel() == "move both" && !sameXY(first->position(), firstRest) && !sameXY(last->position(), lastRest), "steps recorded in a group are one step under its label");
		pressWithControl(SDLK_z);
		expect(sameXY(first->position(), firstRest) && sameXY(last->position(), lastRest), "one undo takes back every step of a group, last first");
		pressWithControl(SDLK_y);
		expect(!sameXY(first->position(), firstRest) && !sameXY(last->position(), lastRest), "one redo applies the group again");
		pressWithControl(SDLK_z);
		{
			auto nothingRecorded = history.group("nothing");
		}
		expect(history.redoLabel() == "move both", "a group nothing was recorded in is no step");

		// a step from before a delete still applies to the node undo brings back: it is the same node
		auto countBefore = std::distance(a_app.scene()->begin(), a_app.scene()->end());
		moveBy(last, MV::point(30.0f, 0.0f));
		auto movedPosition = last->position();
		auto depthBefore = last->depth();
		pressKey(SDLK_DELETE);
		expect(std::distance(a_app.scene()->begin(), a_app.scene()->end()) == countBefore - 1 && !last->parent() && !a_app.selected(), "Delete removes the selected node and clears the selection");
		expect(history.undoLabel() == "delete " + last->id(), "the delete is a labelled step, got " + history.undoLabel());
		pressWithControl(SDLK_z);
		expect(last->parent() == a_app.scene() && *std::prev(a_app.scene()->end()) == last && last->depth() == depthBefore, "undo puts the same node back where it was");
		expect(a_app.selected() == last, "undo of a delete selects the node again");
		pressWithControl(SDLK_z);
		expect(sameXY(last->position(), lastRest), "a step recorded before the delete still undoes on the restored node");
		pressWithControl(SDLK_y);
		pressWithControl(SDLK_y);
		expect(!last->parent() && sameXY(last->position(), movedPosition), "redo moves the node and deletes it again");
		pressWithControl(SDLK_z);
		pressWithControl(SDLK_z);
		expect(last->parent() == a_app.scene() && sameXY(last->position(), lastRest), "both steps undo again");

		a_app.select(a_app.scene());
		expect(!a_app.deleteSelected() && !a_app.duplicateSelected(), "the scene's root is neither deleted nor duplicated");

		a_app.select(last);
		pressWithControl(SDLK_d);
		auto copy = a_app.selected();
		expect(copy && copy != last && copy->parent() == a_app.scene() && copy->id() != last->id(), "Ctrl+D adds a copy beside the original and selects it");
		if (copy) {
			expect(std::distance(copy->begin(), copy->end()) == std::distance(last->begin(), last->end()) && sameXY(copy->position(), last->position()), "the copy carries the original's children and transform");
		}
		pressWithControl(SDLK_z);
		expect(std::distance(a_app.scene()->begin(), a_app.scene()->end()) == countBefore && a_app.selected() == last, "undo removes the copy and selects the original");
		pressWithControl(SDLK_y);
		expect(std::distance(a_app.scene()->begin(), a_app.scene()->end()) == countBefore + 1 && a_app.selected() == copy, "redo puts the same copy back, selected");
		pressWithControl(SDLK_z);

		auto historyFile = (std::filesystem::temp_directory_path() / "workbench_selftest_history.scene").string();
		auto starred = [&a_app]() { return a_app.title().find('*') != std::string::npos; };
		expect(a_app.saveScene(historyFile) && !a_app.modified() && !starred(), "a save leaves the scene unmodified and its title plain");
		moveBy(last, MV::point(12.0f, 0.0f));
		expect(a_app.modified() && starred(), "an edit marks the scene modified and stars the title");
		pressWithControl(SDLK_z);
		expect(!a_app.modified() && !starred(), "undoing back to the saved state clears the mark");
		pressWithControl(SDLK_y);
		expect(a_app.modified(), "redoing past the saved state marks it again");
		expect(a_app.saveScene(historyFile) && !a_app.modified(), "a second save moves the saved mark");
		pressWithControl(SDLK_z);
		expect(a_app.modified() && starred(), "undoing behind the saved state is a modification too");

		int asked = 0;
		auto answer = Choice::CANCEL;
		a_app.askAboutUnsavedChanges = [&](const std::string&) {
			++asked;
			return answer;
		};
		a_app.requestNewScene();
		expect(asked == 1 && !a_app.scene()->empty() && a_app.modified(), "Cancel keeps a modified scene open");
		a_app.requestExit();
		expect(asked == 2 && !a_app.exiting(), "Cancel keeps the editor open");
		answer = Choice::SAVE;
		auto loginScene = std::string("Interface/Login/view.scene");
		auto idOfLast = last->id();
		expect(a_app.requestLoadScene(loginScene) && asked == 3 && a_app.document() == loginScene && !a_app.modified(), "Save writes the scene, then the other one loads");
		try {
			auto written = MV::Scene::Node::load(historyFile, a_managers.services, false);
			auto writtenLast = written->get(idOfLast, false);
			expect(writtenLast && sameXY(writtenLast->position(), lastRest), "the scene Save wrote holds the state it was asked about");
		} catch (const std::exception& a_error) {
			expect(false, std::string("the scene Save wrote loads back: ") + a_error.what());
		}
		expect(a_app.requestLoadScene(loginScene) && asked == 3, "an unmodified scene is replaced without a question");
		auto windowSize = a_managers.renderer.window().windowSize();
		a_app.dropPrefab("Prefabs/SimpleButton.bindsnap", MV::Point<int>(windowSize.width / 2, windowSize.height / 2));
		answer = Choice::DISCARD;
		a_app.requestNewScene();
		expect(asked == 4 && a_app.scene()->empty() && a_app.document().empty() && !a_app.modified(), "Don't Save drops the changes and makes the new scene");

		a_app.askAboutUnsavedChanges = [](const std::string&) { return Choice::DISCARD; };
		std::error_code removeIgnored;
		std::filesystem::remove(historyFile, removeIgnored);
		expect(a_app.loadScene(loginScene), "the Login scene loads again");
		a_app.dropPrefab("Prefabs/SimpleButton.bindsnap", MV::Point<int>(windowSize.width / 2, windowSize.height / 2));
		// a node the rest of the scene refers to: the delete clears the reference so the file drops the node, undo restores it
		auto placedButton = *std::prev(a_app.scene()->end());
		auto buttonComponent = placedButton->component<MV::Scene::Button>(false, false);
		auto idleView = buttonComponent ? buttonComponent->idleNode() : nullptr;
		expect(idleView && idleView->parent() == placedButton, "the placed button holds its Idle view as a child");
		if (idleView) {
			auto viewCount = std::distance(placedButton->begin(), placedButton->end());
			a_app.select(idleView);
			expect(a_app.deleteSelected() && !idleView->parent() && !buttonComponent->idleNode(), "deleting a node clears the references the scene holds to it");
			a_app.runFrames(3);
			auto referenceFile = (std::filesystem::temp_directory_path() / "workbench_selftest_reference.scene").string();
			expect(a_app.saveScene(referenceFile), "the scene saves after the delete");
			try {
				auto written = *std::prev(MV::Scene::Node::load(referenceFile, a_managers.services, false)->end());
				auto writtenButton = written->component<MV::Scene::Button>(false, false);
				expect(std::distance(written->begin(), written->end()) == viewCount - 1 && writtenButton && !writtenButton->idleNode() && writtenButton->activeNode(), "the saved button has neither the deleted view nor a reference to it");
			} catch (const std::exception& a_error) {
				expect(false, std::string("the scene saved after a delete loads back: ") + a_error.what());
			}
			std::filesystem::remove(referenceFile, removeIgnored);
			expect(history.undoLabel() == "delete " + idleView->id(), "a delete with cleared references is still one step");
			pressWithControl(SDLK_z);
			expect(idleView->parent() == placedButton && buttonComponent->idleNode() == idleView && std::distance(placedButton->begin(), placedButton->end()) == viewCount, "undo puts the node and the reference back");
			pressWithControl(SDLK_y);
			expect(!idleView->parent() && !buttonComponent->idleNode(), "redo deletes and clears again");
			pressWithControl(SDLK_z);
			a_app.runFrames(3);
		}
		a_app.select(nullptr);
		a_app.runFrames(10);
	} else {
		expect(false, "the scene has two nodes for the history checks");
	}

	// Text measuring against the renderer: a font's measure is what a Text lays out, and a label keeps to its width
	{
		auto& library = *a_managers.services.get<MV::TextLibrary>();
		auto font = library.get("wb-small");
		expect(static_cast<bool>(font), "the label font is loaded");
		if (font) {
			auto probe = MV::Scene::Node::make(a_managers.renderer, "textProbe");
			for (std::string sample : { "Hierarchy", "maximumSpawnRate", "WWWiii 0123.5", "x" }) {
				auto laidOut = probe->make(MV::guid("sample_"))->attach<MV::Scene::Text>(library, "wb-small");
				laidOut->wrapping(MV::TextWrapMethod::NONE, 2000.0f)->text(sample);
				expect(std::abs(laidOut->contentSize().width - font->measure(sample).width) < 0.01f, "measuring \"" + sample + "\" gives the width its Text lays out: " + std::to_string(font->measure(sample).width) + " against " + std::to_string(laidOut->contentSize().width));
			}
			Workbench::Theme probeTheme;
			std::string longName = "aVeryLongPropertyNameThatCannotFit";
			float narrow = font->measure(longName).width * 0.5f;
			auto label = Workbench::makeLabel(probe, a_managers.services, probeTheme, "narrow", MV::size(narrow, 20.0f), longName);
			std::string shown = label->text();
			expect(shown.size() < longName.size() && shown.size() > 3 && shown.substr(shown.size() - 3) == "..." && label->contentSize().width <= narrow, "a label too narrow for its text ends in an ellipsis inside its width, showing " + shown);
			Workbench::setLabelText(label, a_managers.services, probeTheme, "ok");
			expect(std::string(label->text()) == "ok", "text that fits a label is shown whole");
			auto button = Workbench::makeButton(probe, a_managers.services, probeTheme, "narrowButton", MV::size(narrow, 22.0f), longName, nullptr);
			std::string buttonShown = button->text();
			expect(buttonShown.size() < longName.size() && buttonShown.substr(buttonShown.size() - 3) == "...", "a button too narrow for its text ends it in an ellipsis too, showing " + buttonShown);
		}
	}

	// The history strip: opened from the toolbar, a pip per step, its handle dragged back and forward
	{
		auto& history = a_app.commands();
		auto& toolbar = a_app.sceneTools();
		auto& strip = toolbar.history();
		auto& gizmoLayer = a_app.gizmoLayer();
		auto& mouse = *a_managers.services.get<MV::TapDevice>();
		auto hold = [&](const MV::Point<int>& a_at, bool a_down, int a_frames = 2) {
			mouse.simulate(a_at, a_down);
			a_app.runFrames(a_frames);
		};
		auto click = [&](const MV::Point<int>& a_at) {
			hold(a_at, false);
			hold(a_at, true);
			hold(a_at, false);
			mouse.stopSimulating();
		};
		auto dragHandle = [&](size_t a_from, size_t a_to) {
			hold(strip.positionScreenPoint(a_from), false);
			hold(strip.positionScreenPoint(a_from), true);
			for (size_t at = a_from; at != a_to; at += a_to > a_from ? 1 : -1) {
				hold(strip.positionScreenPoint(at), true, 1);
			}
			hold(strip.positionScreenPoint(a_to), true, 1);
			hold(strip.positionScreenPoint(a_to), false);
			mouse.stopSimulating();
		};
		auto captionStarts = [&strip](const std::string& a_text) { return strip.caption().rfind(a_text, 0) == 0; };
		auto pushKey = [](SDL_Keycode a_key) {
			SDL_Event press{};
			press.type = SDL_KEYDOWN;
			press.key.keysym.sym = a_key;
			SDL_PushEvent(&press);
		};
		auto typeInto = [&](const MV::Point<int>& a_field, const std::string& a_text, bool a_enter) {
			click(a_field);
			for (int erased = 0; erased < 8; ++erased) {
				pushKey(SDLK_BACKSPACE);
			}
			for (char character : a_text) {
				SDL_Event typed{};
				typed.type = SDL_TEXTINPUT;
				typed.text.text[0] = character;
				SDL_PushEvent(&typed);
			}
			if (a_enter) {
				pushKey(SDLK_RETURN);
			}
			a_app.runFrames(2);
		};
		// the simulated mouse makes no SDL event, and a field is left on a button-down event
		auto pressElsewhere = [&a_app]() {
			SDL_Event press{};
			press.type = SDL_MOUSEBUTTONDOWN;
			press.button.button = SDL_BUTTON_LEFT;
			SDL_PushEvent(&press);
			a_app.runFrames(2);
		};

		expect(a_app.loadScene("Interface/Login/view.scene") && !a_app.scene()->empty(), "the Login scene loads for the history strip");
		a_app.runFrames(10);
		auto subject = *std::prev(a_app.scene()->end());
		auto rest = subject->position();
		expect(!toolbar.historyShown(), "the history starts closed");
		click(toolbar.buttonScreenPoint(7));
		expect(toolbar.historyShown() && strip.pipCount() == 0 && captionStarts("0 / 0"), "the toolbar's history button opens the strip, empty for a fresh scene");

		a_app.select(subject);
		a_app.runFrames(1);
		for (int step = 0; step < 4; ++step) {
			gizmoLayer.moveSelected(Workbench::GizmoAxis::BOTH, MV::point(10.0f, 0.0f));
		}
		a_app.runFrames(2);
		expect(strip.pipCount() == 4 && captionStarts("4 / 4   move " + subject->id()), "each step adds a bar and the row names the last one, got " + strip.caption());

		hold(toolbar.buttonScreenPoint(0), false);
		expect(toolbar.buttonHovered(0) && !toolbar.buttonHovered(2), "the pointer over a toolbar button lights it alone");
		hold(toolbar.buttonScreenPoint(2), false);
		expect(!toolbar.buttonHovered(0) && toolbar.buttonHovered(2), "the light follows the pointer to the next button");
		hold(strip.barScreenPoint(1), false);
		hold(strip.barScreenPoint(1) + MV::Point<int>(1, 0), false);
		expect(!toolbar.buttonHovered(2) && strip.caption().find("step 2: move " + subject->id()) != std::string::npos, "the pointer over a bar names that step, got " + strip.caption());
		hold(strip.stepButtonScreenPoint(false), false);
		expect(captionStarts("4 / 4   move " + subject->id()), "the row names the step the scene is at once the pointer leaves the bars, got " + strip.caption());
		mouse.stopSimulating();

		hold(strip.positionScreenPoint(4), false);
		hold(strip.positionScreenPoint(4), true);
		hold(strip.positionScreenPoint(3), true, 1);
		expect(history.position() == 3 && sameXY(subject->position(), rest + MV::point(30.0f, 0.0f)), "the handle undoes a step as it is dragged past it");
		hold(strip.positionScreenPoint(1), true, 1);
		hold(strip.positionScreenPoint(1), false);
		mouse.stopSimulating();
		expect(history.position() == 1 && history.length() == 4 && sameXY(subject->position(), rest + MV::point(10.0f, 0.0f)), "a drag back unwinds to where it is released");
		expect(strip.pipCount() == 4 && captionStarts("1 / 4"), "the undone steps keep their pips, got " + strip.caption());
		expect(a_app.selected() == subject, "a drag on the strip does not pick or pan in the scene behind it");
		dragHandle(1, 3);
		expect(history.position() == 3 && sameXY(subject->position(), rest + MV::point(30.0f, 0.0f)), "a drag forward redoes");
		click(strip.positionScreenPoint(0));
		expect(history.position() == 0 && sameXY(subject->position(), rest) && captionStarts("0 / 4   start"), "a press at the start of the row unwinds everything");
		click(strip.positionScreenPoint(4));
		expect(history.position() == 4 && sameXY(subject->position(), rest + MV::point(40.0f, 0.0f)), "a press at the end redoes everything");

		click(strip.stepButtonScreenPoint(false));
		expect(history.position() == 3 && captionStarts("3 / 4"), "the left arrow steps back once, got " + strip.caption());
		click(strip.stepButtonScreenPoint(true));
		expect(history.position() == 4 && captionStarts("4 / 4"), "the right arrow steps forward once, got " + strip.caption());
		typeInto(strip.fieldScreenPoint(), "1", true);
		expect(history.position() == 1 && captionStarts("1 / 4") && sameXY(subject->position(), rest + MV::point(10.0f, 0.0f)), "a position typed and entered moves the history there, got " + strip.caption());
		typeInto(strip.fieldScreenPoint(), "99", true);
		expect(history.position() == 4 && captionStarts("4 / 4"), "a typed position past the end stops at the end, got " + strip.caption());
		typeInto(strip.fieldScreenPoint(), "2", false);
		expect(captionStarts("2 / 4") && history.position() == 4, "typing alone moves nothing");
		pressElsewhere();
		expect(history.position() == 4 && captionStarts("4 / 4"), "a position typed but not entered is dropped when the field is left, got " + strip.caption());

		expect(history.savedPosition() == std::optional<size_t>(0), "the saved mark sits where the scene was loaded");
		auto stripFile = (std::filesystem::temp_directory_path() / "workbench_selftest_strip.scene").string();
		expect(a_app.saveScene(stripFile) && history.savedPosition() == std::optional<size_t>(4), "a save moves the saved mark to the handle");
		std::error_code stripIgnored;
		std::filesystem::remove(stripFile, stripIgnored);

		click(strip.positionScreenPoint(2));
		gizmoLayer.moveSelected(Workbench::GizmoAxis::BOTH, MV::point(0.0f, 5.0f));
		a_app.runFrames(2);
		expect(history.length() == 3 && strip.pipCount() == 3 && !history.savedPosition().has_value(), "a new step drops the undone ones, their pips and a saved mark among them");

		click(a_app.menus().titleScreenPoint(1));
		expect(a_app.menus().isOpen() && a_app.menus().rowLabel(4) == "Hide History", "Edit offers to hide the open history");
		if (a_app.menus().isOpen()) {
			click(a_app.menus().rowScreenPoint(4));
		}
		expect(!toolbar.historyShown(), "Edit > Hide History closes the strip");
		click(toolbar.buttonScreenPoint(7));
		expect(toolbar.historyShown(), "the button opens it again");
		click(toolbar.buttonScreenPoint(7));
		expect(!toolbar.historyShown(), "and closes it");

		// Settings: the undo-step limit applies on Enter, asks before it drops history, and is kept in the settings file
		auto& settings = a_app.settings();
		a_app.showSettings();
		a_app.runFrames(3);
		int confirms = 0;
		bool agree = false;
		a_app.askToConfirm = [&](const std::string&) {
			++confirms;
			return agree;
		};
		expect(a_app.undoSteps() == 200 && settings.shown("Undo steps") == "200" && history.length() == 3, "the Settings tab shows the undo steps in use");
		typeInto(settings.fieldScreenPoint("Undo steps"), "50", false);
		pressElsewhere();
		expect(a_app.undoSteps() == 200 && settings.shown("Undo steps") == "200" && confirms == 0, "a number typed but not entered changes nothing");
		typeInto(settings.fieldScreenPoint("Undo steps"), "50", true);
		expect(a_app.undoSteps() == 50 && settings.shown("Undo steps") == "50" && confirms == 0 && history.length() == 3, "a limit the history fits under applies on Enter without a question");
		typeInto(settings.fieldScreenPoint("Undo steps"), "2", true);
		expect(confirms == 1 && a_app.undoSteps() == 50 && settings.shown("Undo steps") == "50" && history.length() == 3, "Cancel keeps the limit and the history");
		agree = true;
		auto beforeTrim = subject->position();
		typeInto(settings.fieldScreenPoint("Undo steps"), "2", true);
		expect(confirms == 2 && a_app.undoSteps() == 2 && settings.shown("Undo steps") == "2" && history.length() == 2 && history.position() == 2, "Okay drops the oldest step");
		expect(sameXY(subject->position(), beforeTrim), "dropping steps leaves the scene as it is");
		a_app.select(subject);
		a_app.runFrames(1);
		gizmoLayer.moveSelected(Workbench::GizmoAxis::BOTH, MV::point(3.0f, 0.0f));
		expect(history.length() == 2 && history.position() == 2, "a new step past the limit pushes the oldest out");
		typeInto(settings.fieldScreenPoint("Undo steps"), "lots", true);
		expect(a_app.undoSteps() == 2 && settings.shown("Undo steps") == "2", "text that is not a number is refused");

		expect(a_app.gridSize().width == 100.0f && gizmoLayer.snapSteps.moveX == 10.0f && settings.shown("Grid X") == "100" && settings.shown("Rotation snap") == "5", "the grid cell is 100 with a snap a tenth of it, and rotation snaps by 5 degrees");
		typeInto(settings.fieldScreenPoint("Grid X"), "50", true);
		expect(a_app.gridSize().width == 50.0f && a_app.gridSize().height == 100.0f && gizmoLayer.snapSteps.moveX == 5.0f && gizmoLayer.snapSteps.moveY == 10.0f && a_app.grid().spacing().width == 5.0f, "Grid X sets the cell's width: lines and snapping follow at a tenth of it");
		typeInto(settings.fieldScreenPoint("Grid Y"), "20", true);
		expect(a_app.gridSize().height == 20.0f && gizmoLayer.snapSteps.moveY == 2.0f && a_app.grid().spacing().height == 2.0f, "Grid Y sets the cell's height on its own");
		gizmoLayer.moveSelected(Workbench::GizmoAxis::BOTH, MV::point(3.3f, 16.3f), true);
		expect(std::abs(std::remainder(subject->position().x, 5.0f)) < 0.001f && std::abs(std::remainder(subject->position().y, 2.0f)) < 0.001f, "a snapped move lands on each axis's own step");
		typeInto(settings.fieldScreenPoint("Grid X"), "0", true);
		expect(a_app.gridSize().width == 50.0f && settings.shown("Grid X") == "50", "a grid of zero is refused");
		typeInto(settings.fieldScreenPoint("Rotation snap"), "30", true);
		gizmoLayer.rotateSelected(40.0f, true);
		expect(a_app.rotationSnap() == 30.0f && std::abs(std::remainder(subject->rotation().z, 30.0f)) < 0.001f, "Rotation snap sets the degrees a snapped turn lands on");

		auto settingsFile = (std::filesystem::temp_directory_path() / "workbench_selftest_settings.txt").string();
		std::filesystem::remove(settingsFile, stripIgnored);
		a_app.useSettingsFile(settingsFile);
		expect(a_app.undoSteps() == 2 && a_app.gridSize().width == 50.0f, "a missing settings file changes nothing");
		typeInto(settings.fieldScreenPoint("Undo steps"), "75", true);
		a_app.useSettingsFile("");
		a_app.commands().limit(200);
		a_app.gridSize(MV::size(100.0f, 100.0f));
		a_app.rotationSnap(5.0f);
		a_app.useSettingsFile(settingsFile);
		expect(a_app.undoSteps() == 75 && settings.shown("Undo steps") == "75", "a changed setting is written to the file and read back");
		expect(a_app.gridSize().width == 50.0f && a_app.gridSize().height == 20.0f && a_app.rotationSnap() == 30.0f && gizmoLayer.snapSteps.moveX == 5.0f && settings.shown("Grid Y") == "20", "the grid and the rotation snap come back from the file too");
		expect(a_app.requestUndoSteps(200) && a_app.undoSteps() == 200, "the limit goes back to its default");
		a_app.gridSize(MV::size(100.0f, 100.0f));
		a_app.rotationSnap(5.0f);
		a_app.useSettingsFile("");
		std::filesystem::remove(settingsFile, stripIgnored);
		// more steps than bars fit: the bars keep their width and the row scrolls to where the handle is
		a_app.askToConfirm = [](const std::string&) { return true; };
		click(toolbar.buttonScreenPoint(7));
		expect(a_app.requestUndoSteps(2000), "the limit rises for a long history");
		a_app.select(subject);
		a_app.runFrames(1);
		for (int step = 0; step < 900; ++step) {
			gizmoLayer.moveSelected(Workbench::GizmoAxis::BOTH, MV::point(step % 2 == 0 ? 1.0f : -1.0f, 0.0f));
		}
		a_app.runFrames(2);
		expect(history.length() > 900 && strip.scrolling() && strip.pipCount() < history.length() && strip.pipCount() > 50, "a long history keeps its bars drawable and scrolls, showing " + std::to_string(strip.pipCount()));
		history.moveTo(5);
		a_app.runFrames(2);
		click(strip.positionScreenPoint(9));
		expect(history.position() == 9, "the row scrolled to the handle, and a press there still lands on a step, at " + std::to_string(history.position()));
		history.moveTo(history.length());
		a_app.runFrames(2);
		click(strip.positionScreenPoint(history.length() - 3));
		expect(history.position() == history.length() - 3, "the far end of a long history is reachable the same way");
		expect(a_app.requestUndoSteps(200) && history.length() == 200, "the limit goes back and the oldest steps go with it");
		click(toolbar.buttonScreenPoint(7));
		a_app.docking().closePanel(a_app.docking().panelTitled("Settings"));
		a_app.showInspector();
		a_app.select(nullptr);
		a_app.runFrames(3);
	}

	// Keyboard: Tab between a panel's fields, a focused field's text selected, and named focus neighbours in the inspector
	{
		auto& mouse = *a_managers.services.get<MV::TapDevice>();
		auto& settings = a_app.settings();
		auto& docking = a_app.docking();
		auto click = [&](const MV::Point<int>& a_at) {
			for (bool down : { false, true, false }) {
				mouse.simulate(a_at, down);
				a_app.runFrames(2);
			}
			mouse.stopSimulating();
		};
		auto key = [&a_app](SDL_Keycode a_key, int a_modifiers = KMOD_NONE) {
			SDL_SetModState(static_cast<SDL_Keymod>(a_modifiers));
			SDL_Event press{};
			press.type = SDL_KEYDOWN;
			press.key.keysym.sym = a_key;
			SDL_PushEvent(&press);
			a_app.runFrames(2);
			SDL_SetModState(KMOD_NONE);
		};
		auto type = [&a_app](const std::string& a_text) {
			for (char character : a_text) {
				SDL_Event typed{};
				typed.type = SDL_TEXTINPUT;
				typed.text.text[0] = character;
				SDL_PushEvent(&typed);
			}
			a_app.runFrames(2);
		};
		auto editingShows = [&a_app](const std::string& a_text) {
			auto editing = a_app.editingText();
			return editing && std::string(editing->text()) == a_text;
		};

		a_app.showSettings();
		a_app.runFrames(3);
		click(settings.fieldScreenPoint("Undo steps"));
		expect(editingShows("200") && a_app.editingText()->hasSelection() && std::string(a_app.editingText()->selectedText()) == "200", "a field that starts taking keys has its text selected");
		key(SDLK_TAB);
		expect(editingShows("100") && a_app.editingText()->hasSelection() && settings.shown("Undo steps") == "200", "Tab moves to the next field of the panel, and its text is selected");
		type("40");
		key(SDLK_RETURN);
		expect(a_app.gridSize().width == 40.0f && !a_app.editingText(), "typing over the selection replaces it, got " + std::to_string(a_app.gridSize().width));
		click(settings.fieldScreenPoint("Grid X"));
		key(SDLK_END);
		key(SDLK_LEFT, KMOD_SHIFT);
		type("5");
		key(SDLK_RETURN);
		expect(a_app.gridSize().width == 45.0f, "Shift with an arrow selects from the cursor, and typing replaces that much: got " + std::to_string(a_app.gridSize().width));
		click(settings.fieldScreenPoint("Grid X"));
		key(SDLK_TAB, KMOD_SHIFT);
		expect(editingShows("200"), "Shift+Tab moves back a field");
		key(SDLK_TAB, KMOD_SHIFT);
		expect(editingShows("off"), "and from the first field wraps to the last");
		key(SDLK_TAB);
		expect(editingShows("200"), "as Tab from the last wraps to the first");
		key(SDLK_ESCAPE);
		expect(!a_app.editingText() && a_app.undoSteps() == 200 && a_app.rotationSnap() == 5.0f, "Escape leaves the field and nothing Tab passed through was changed");
		a_app.gridSize(MV::size(100.0f, 100.0f));
		docking.closePanel(docking.panelTitled("Settings"));
		a_app.showInspector();

		std::function<std::shared_ptr<MV::Scene::Node>(const std::shared_ptr<MV::Scene::Node>&, const std::string&)> findNode = [&](const std::shared_ptr<MV::Scene::Node>& a_node, const std::string& a_id) -> std::shared_ptr<MV::Scene::Node> {
			if (a_node->id() == a_id) {
				return a_node;
			}
			for (auto&& child : *a_node) {
				if (auto found = findNode(child, a_id)) {
					return found;
				}
			}
			return nullptr;
		};
		auto loginButton = findNode(a_app.scene(), "LoginButton");
		auto emailField = findNode(a_app.scene(), "EmailBackground");
		expect(loginButton && emailField, "the Login scene has its button and its email field");
		if (loginButton && emailField) {
			auto button = loginButton->component<MV::Scene::Clickable>(false, false);
			expect(static_cast<bool>(button) && button->focusable(), "a Button in a loaded scene is focusable");
			a_app.select(loginButton);
			a_app.runFrames(3);
			auto& inspector = a_app.inspector();
			auto header = inspector.rowScreenPoint("focus links", false);
			expect(header.has_value(), "the inspector shows a Clickable's focus links as a group");
			if (header && button) {
				click(*header);
				auto down = inspector.rowScreenPoint("down");
				expect(down.has_value(), "opened, it has a row for each link");
				if (down) {
					expect(inspector.dropNode(emailField, *down), "a node dropped on a link is taken");
					a_app.runFrames(2);
					expect(button->focusLink(MV::FocusLink::DOWN) == "EmailBackground", "and named as that neighbour: " + button->focusLink(MV::FocusLink::DOWN));
					pressWithControl(SDLK_z);
					expect(button->focusLink(MV::FocusLink::DOWN).empty(), "undo clears the link");
				}
			}
			a_app.select(nullptr);
		}
	}

	// A game controller moves the focus, presses what holds it and lets it go
	{
		auto& mouse = *a_managers.services.get<MV::TapDevice>();
		auto& settings = a_app.settings();
		auto& docking = a_app.docking();
		auto& pad = a_app.pad();
		auto click = [&](const MV::Point<int>& a_at) {
			for (bool down : { false, true, false }) {
				mouse.simulate(a_at, down);
				a_app.runFrames(2);
			}
			mouse.stopSimulating();
		};
		auto press = [&](MV::ControllerButton a_button) {
			pad.simulate(MV::ControllerState().press(a_button));
			a_app.runFrames(2);
			pad.simulate(MV::ControllerState());
			a_app.runFrames(2);
		};
		auto editingShows = [&a_app](const std::string& a_text) {
			auto editing = a_app.editingText();
			return editing && std::string(editing->text()) == a_text;
		};

		a_app.showSettings();
		a_app.runFrames(3);
		click(settings.fieldScreenPoint("Undo steps"));
		expect(editingShows("200"), "the first field is being typed in");
		press(MV::ControllerButton::DPAD_DOWN);
		expect(editingShows("100") && a_app.editingText()->hasSelection(), "the d-pad moves the focus to the field below, which takes the typing");
		pad.simulate(MV::ControllerState().axis(MV::ControllerAxis::LEFT_Y, -1.0f));
		a_app.runFrames(2);
		pad.simulate(MV::ControllerState());
		a_app.runFrames(2);
		expect(editingShows("200"), "and the left stick moves it back up");
		press(MV::ControllerButton::EAST);
		expect(!a_app.editingText() && mouse.hasFocus() && a_app.undoSteps() == 200, "the east button leaves the field without changing it, the focus still on it");
		press(MV::ControllerButton::SOUTH);
		expect(editingShows("200"), "the south button presses what holds the focus: the field is typed in again");
		press(MV::ControllerButton::RIGHT_SHOULDER);
		expect(mouse.hasFocus() && !editingShows("200"), "the right shoulder steps to the next control");
		press(MV::ControllerButton::EAST);
		press(MV::ControllerButton::EAST);
		expect(!a_app.editingText() && !mouse.hasFocus(), "east once more lets the focus go");

		click(settings.fieldScreenPoint("Undo steps"));
		press(MV::ControllerButton::BACK);
		expect(a_app.controllerCursor() && settings.shown("Controller cursor") == "on", "select swaps to the free pointer, and the setting follows");
		expect(pad.cursorShown() && a_app.controllerCursorVisible() && mouse.simulating() && !mouse.hasFocus() && !a_app.editingText(), "the pointer shows where the focus was, and the field is no longer typed in");
		auto cursorStart = pad.cursorPosition();
		pad.simulate(MV::ControllerState().axis(MV::ControllerAxis::LEFT_X, 1.0f).axis(MV::ControllerAxis::LEFT_Y, 0.4f));
		a_app.runFrames(8);
		expect(pad.cursorShown() && a_app.controllerCursorVisible() && pad.cursorPosition().x > cursorStart.x && !mouse.hasFocus(), "the left stick moves the drawn pointer and no focus");
		pad.simulate(MV::ControllerState());
		a_app.runFrames(2);
		press(MV::ControllerButton::BACK);
		expect(!a_app.controllerCursor() && settings.shown("Controller cursor") == "off" && !pad.cursorShown() && !a_app.controllerCursorVisible() && !mouse.simulating(), "select again goes back to snapping, and the mouse is read again");
		expect(mouse.hasFocus(), "with the focus on the control nearest where the pointer was");
		a_app.controllerCursor(true);
		a_app.runFrames(2);
		expect(pad.cursorShown() && settings.shown("Controller cursor") == "on", "the setting turns the pointer on as well");
		a_app.controllerCursor(false);
		a_app.runFrames(2);
		expect(!pad.cursorShown() && !mouse.simulating(), "and off");
		press(MV::ControllerButton::EAST);
		press(MV::ControllerButton::EAST);
		pad.simulate(MV::ControllerState());
		a_app.runFrames(2);
		mouse.focusClear();
		pad.stopSimulating();
		a_app.runFrames(2);
		expect(a_app.undoSteps() == 200 && a_app.gridSize().width == 100.0f && a_app.rotationSnap() == 5.0f, "and none of it changed a setting");
		docking.closePanel(docking.panelTitled("Settings"));
		a_app.showInspector();
		a_app.runFrames(2);
	}

	// A right press opens the menu of what is under it: a Hierarchy row, the scene, a dock tab, a text field
	{
		auto& mouse = *a_managers.services.get<MV::TapDevice>();
		auto& menus = a_app.menus();
		auto& docking = a_app.docking();
		auto& settings = a_app.settings();
		auto hold = [&](const MV::Point<int>& a_at, bool a_left, bool a_right) {
			mouse.simulate(a_at, a_left, a_right);
			a_app.runFrames(2);
		};
		auto click = [&](const MV::Point<int>& a_at) {
			hold(a_at, false, false);
			hold(a_at, true, false);
			hold(a_at, false, false);
			mouse.stopSimulating();
		};
		auto rightClick = [&](const MV::Point<int>& a_at) {
			hold(a_at, false, false);
			hold(a_at, false, true);
			hold(a_at, false, false);
			mouse.stopSimulating();
		};
		auto key = [&a_app](SDL_Keycode a_key) {
			SDL_Event press{};
			press.type = SDL_KEYDOWN;
			press.key.keysym.sym = a_key;
			SDL_PushEvent(&press);
			a_app.runFrames(2);
		};
		auto childCount = [](const std::shared_ptr<MV::Scene::Node>& a_node) {
			return static_cast<size_t>(std::distance(a_node->begin(), a_node->end()));
		};

		auto scene = a_app.scene();
		auto rowNode = scene && scene->begin() != scene->end() ? *scene->begin() : nullptr;
		expect(static_cast<bool>(rowNode), "the scene has a node for the menus to work on");
		if (rowNode) {
			a_app.select(rowNode);
			a_app.runFrames(3);
			auto row = a_app.hierarchy().rowScreenPoint(rowNode);
			a_app.select(nullptr);
			a_app.runFrames(2);
			expect(row.has_value(), "the node has a Hierarchy row");
			if (row) {
				rightClick(*row);
				expect(menus.contextOpen() && a_app.selected() == rowNode, "a right press on a Hierarchy row selects it and opens a menu");
				expect(menus.rowCount() == 3 && menus.rowLabel(0) == "Frame" && menus.rowLabel(1) == "Duplicate" && menus.rowLabel(2) == "Delete", "with Frame, Duplicate and Delete");
				if (menus.contextOpen() && menus.rowCount() == 3) {
					size_t before = childCount(scene);
					click(menus.rowScreenPoint(1));
					expect(!menus.isOpen() && childCount(scene) == before + 1, "Duplicate from the menu duplicates the node");
					pressWithControl(SDLK_z);
					expect(childCount(scene) == before, "and undoes as one step");
				}
			}

			a_app.select(nullptr);
			auto inScene = rowNode->screenBounds().centerPoint();
			expect(a_app.gizmoLayer().pointerOverScene(inScene), "the node is in view in the Scene panel");
			rightClick(inScene);
			expect(menus.contextOpen() && static_cast<bool>(a_app.selected()) && menus.rowCount() == 3 && menus.rowLabel(2) == "Delete", "a right press in the scene selects what is under it and opens the same menu");
			key(SDLK_ESCAPE);
			expect(!menus.isOpen(), "Escape closes it");
			MV::Point<int> dragged(inScene.x + 40, inScene.y);
			hold(inScene, false, false);
			hold(inScene, false, true);
			hold(dragged, false, true);
			hold(dragged, false, false);
			expect(!menus.isOpen(), "a right drag pans the scene and opens no menu");
			hold(dragged, false, true);
			hold(inScene, false, true);
			hold(inScene, false, false);
			mouse.stopSimulating();
			a_app.select(nullptr);
		}

		auto layoutBefore = docking.describeLayout();
		rightClick(docking.tabScreenPoint("Console"));
		expect(menus.contextOpen() && menus.rowCount() == 2 && menus.rowLabel(0) == "Close" && menus.rowLabel(1) == "Float", "a right press on a dock tab offers Close and Float");
		if (menus.contextOpen() && menus.rowCount() == 2) {
			click(menus.rowScreenPoint(1));
			expect(docking.floating(docking.panelTitled("Console")), "Float lifts the panel out of the dock");
			rightClick(docking.tabScreenPoint("Console"));
			expect(menus.contextOpen() && !menus.rowEnabled(1) && menus.rowEnabled(0), "a panel floating by itself cannot float again, and can close");
			if (menus.contextOpen()) {
				click(menus.rowScreenPoint(0));
				expect(!docking.open(docking.panelTitled("Console")), "Close from the menu closes the panel");
			}
		}
		expect(docking.applyLayout(layoutBefore) && docking.describeLayout() == layoutBefore, "the layout from before goes back");
		a_app.runFrames(3);

		a_app.showSettings();
		a_app.runFrames(3);
		click(settings.fieldScreenPoint("Undo steps"));
		key(SDLK_END);
		auto editing = a_app.editingText();
		expect(editing && !editing->hasSelection(), "End drops the selection a field starts with");
		rightClick(settings.fieldScreenPoint("Undo steps"));
		expect(menus.contextOpen() && menus.rowCount() == 4 && menus.rowLabel(0) == "Cut" && menus.rowLabel(3) == "Select All", "a right press on a field offers Cut, Copy, Paste and Select All");
		expect(a_app.editingText() == editing && editing && !editing->hasSelection() && menus.contextOpen() && !menus.rowEnabled(0), "the field being typed in keeps its cursor, and Cut waits for a selection");
		if (menus.contextOpen() && menus.rowCount() == 4) {
			click(menus.rowScreenPoint(3));
			expect(a_app.editingText() == editing && editing && std::string(editing->selectedText()) == "200", "Select All from the menu selects the field's text and the field stays open");
		}
		rightClick(settings.fieldScreenPoint("Grid X"));
		auto other = a_app.editingText();
		expect(menus.contextOpen() && other && other != editing && std::string(other->selectedText()) == "100" && menus.rowEnabled(0), "a right press on another field moves the typing there, with its text selected");
		key(SDLK_ESCAPE);
		expect(!menus.isOpen() && !a_app.editingText() && a_app.undoSteps() == 200, "Escape closes the menu and leaves the field unchanged");
		docking.closePanel(docking.panelTitled("Settings"));
		a_app.showInspector();
		a_app.runFrames(2);
	}

	// Tooltips on resting controls, and the title bar's transport as icon buttons
	{
		auto& mouse = *a_managers.services.get<MV::TapDevice>();
		auto& toolbar = a_app.sceneTools();
		auto& tips = a_app.tips();
		auto hold = [&](const MV::Point<int>& a_at, bool a_down, int a_frames = 2) {
			mouse.simulate(a_at, a_down);
			a_app.runFrames(a_frames);
		};
		auto restUntil = [&](const std::string& a_tip) {
			// each call restarts the frame clock, so a wait is several frames long
			for (int wait = 0; wait < 100 && tips.shown() != a_tip; ++wait) {
				a_app.runFrames(6);
			}
			return tips.shown() == a_tip;
		};
		tips.restSeconds = 0.05;
		hold(toolbar.buttonScreenPoint(1), false);
		expect(restUntil("Move (W)"), "the pointer resting on the Move tool shows its tip, got \"" + tips.shown() + "\"");
		hold(toolbar.buttonScreenPoint(2), false);
		expect(tips.shown() != "Move (W)", "moving on to another control drops the tip");
		expect(restUntil("Rotate (E)"), "and resting there shows that control's tip, got \"" + tips.shown() + "\"");
		hold(toolbar.buttonScreenPoint(2), true);
		expect(tips.shown().empty(), "a press hides the tip");
		hold(toolbar.buttonScreenPoint(2), false);
		auto view = a_app.viewport();
		hold(a_managers.renderer.screenFromWorld(MV::point(view.position.x + view.size.width * 0.5f, view.position.y + view.size.height * 0.6f)), false, 12);
		expect(tips.shown().empty(), "no tip shows over the open scene");
		mouse.stopSimulating();
		a_app.gizmoTool(Workbench::Tool::MOVE);
		tips.restSeconds = 0.45;

		auto click = [&](const MV::Point<int>& a_at) {
			hold(a_at, false);
			hold(a_at, true);
			hold(a_at, false);
			mouse.stopSimulating();
		};
		expect(!a_app.paused(), "the scene's clock starts running");
		click(a_app.transportScreenPoint(0));
		expect(a_app.paused(), "the pause button stops it");
		click(a_app.transportScreenPoint(0));
		expect(!a_app.paused(), "and starts it again");
	}

	// The message log: what the editor and the engine report shows in the status bar and the Console
	{
		using Level = Workbench::MessageLevel;
		auto& mouse = *a_managers.services.get<MV::TapDevice>();
		auto& log = a_app.messages();
		auto& console = a_app.console();
		auto click = [&](const MV::Point<int>& a_at) {
			for (bool down : { false, true, false }) {
				mouse.simulate(a_at, down);
				a_app.runFrames(2);
			}
			mouse.stopSimulating();
		};
		log.clear();
		a_app.runFrames(2);
		expect(console.rowCount() == 0 && a_app.statusLine().empty() && a_app.statusCounts().empty(), "a cleared log leaves the Console and the status bar empty");
		log.add(Level::INFO, "selftest note");
		log.add(Level::WARNING, "selftest warning");
		log.add(Level::FAILURE, "selftest failure");
		log.add(Level::FAILURE, "selftest failure");
		a_app.runFrames(2);
		expect(a_app.statusLine() == "selftest failure (x2)", "the status bar shows the latest message, counting a repeat: " + a_app.statusLine());
		expect(a_app.statusCounts() == "2 errors   1 warning", "and how many errors and warnings there have been: " + a_app.statusCounts());
		expect(console.rowCount() == 3 && console.rowText(0) == "selftest failure (x2)" && console.rowText(2) == "selftest note", "the Console lists them newest first, a repeat on one row");

		MV::warning("selftest engine warning");
		a_app.runFrames(2);
		expect(console.rowCount() == 4 && console.rowText(0).find("selftest engine warning") != std::string::npos && a_app.statusCounts() == "2 errors   2 warnings", "what the engine logs reaches the Console too: " + (console.rowCount() > 0 ? console.rowText(0) : std::string("nothing")));

		auto* consoleTab = a_app.docking().panelTitled("Console");
		expect(a_app.dockLayout().find("(L 0 [Project,Console])") != std::string::npos, "the Console sits behind the Project panel to begin with");
		click(a_app.statusBarScreenPoint());
		expect(a_app.docking().open(consoleTab) && a_app.dockLayout().find("(L 1 [Project,Console])") != std::string::npos, "a press on the status bar brings the Console to the front: " + a_app.dockLayout());
		a_app.runFrames(3);
		click(console.clearButtonScreenPoint());
		a_app.runFrames(2);
		expect(console.rowCount() == 0 && a_app.statusLine().empty() && a_app.statusCounts().empty() && log.count(Level::FAILURE) == 0, "Clear empties the log, the Console and the status bar");
		a_app.resetDockLayout();
		a_app.runFrames(3);
	}

	// Scene toolbar and menu bar, pressed on the simulated mouse
	{
		auto& mouse = *a_managers.services.get<MV::TapDevice>();
		auto click = [&](const MV::Point<int>& a_at) {
			for (bool down : { false, true, false }) {
				mouse.simulate(a_at, down);
				a_app.runFrames(2);
			}
			mouse.stopSimulating();
		};
		auto selectedBefore = a_app.selected();
		click(a_app.sceneTools().buttonScreenPoint(2));
		expect(a_app.gizmoTool() == Workbench::Tool::ROTATE, "the toolbar's rotate button picks the rotate tool");
		click(a_app.sceneTools().buttonScreenPoint(1));
		expect(a_app.gizmoTool() == Workbench::Tool::MOVE, "the toolbar's move button picks the move tool");
		expect(a_app.selected() == selectedBefore, "a press on the toolbar does not pick in the scene behind it");
		click(a_app.sceneTools().buttonScreenPoint(4));
		expect(a_app.gizmoLayer().snapAlways, "the magnet turns snapping on");
		click(a_app.sceneTools().buttonScreenPoint(4));
		expect(!a_app.gizmoLayer().snapAlways, "the magnet turns snapping off again");
		bool gridShown = a_app.grid().visible();
		click(a_app.sceneTools().buttonScreenPoint(5));
		expect(a_app.grid().visible() != gridShown, "the grid button toggles the grid");
		click(a_app.sceneTools().buttonScreenPoint(5));

		click(a_app.menus().titleScreenPoint(0));
		expect(a_app.menus().isOpen(), "a press on File opens its menu");
		SDL_Event escape{};
		escape.type = SDL_KEYDOWN;
		escape.key.keysym.sym = SDLK_ESCAPE;
		SDL_PushEvent(&escape);
		a_app.runFrames(2);
		expect(!a_app.menus().isOpen(), "Escape closes the menu");
		click(a_app.menus().titleScreenPoint(0));
		click(a_app.menus().rowScreenPoint(0));
		expect(!a_app.menus().isOpen() && a_app.scene()->empty() && a_app.document().empty(), "File > New Scene from the menu leaves an empty, untitled scene");
	}

	// Parallax: at rest while editing, displaced only by the preview, saved at rest either way
	expect(a_app.loadScene("Scenes/map.scene"), "the map loads");
	std::shared_ptr<MV::Scene::Parallax> layer;
	std::function<void(const std::shared_ptr<MV::Scene::Node>&)> findLayer = [&](const std::shared_ptr<MV::Scene::Node>& a_node) {
		auto parallax = a_node->component<MV::Scene::Parallax>(true, false);
		if (!layer && parallax && parallax->enabled() && parallax->translateRatio().x != 1.0f && !a_node->id().empty()) {
			layer = parallax.self();
		}
		for (auto&& child : *a_node) {
			findLayer(child);
		}
	};
	findLayer(a_app.scene());
	expect(layer != nullptr, "the map has an enabled Parallax layer");
	if (!layer) {
		return;
	}
	auto layerNode = layer->owner();
	auto layerRest = layer->localOffset();
	a_app.runFrames(10);
	expect(sameXY(layerNode->position(), layerRest) && sameXY(layer->localOffset(), layerRest), "a Parallax layer rests at its localOffset while the camera frames the map");
	a_app.select(layerNode->parent());
	a_app.parallaxPreview(true);
	a_app.runFrames(5);
	expect(a_app.parallaxPreviewing() && !sameXY(layerNode->position(), layerRest), "the preview displaces the layer");
	expect(sameXY(layer->localOffset(), layerRest), "the preview leaves localOffset alone");

	// The inspector's editors on the map's Sky sprite and an emitter, pressed and dragged on the simulated mouse
	{
		auto& mouse = *a_managers.services.get<MV::TapDevice>();
		auto& inspector = a_app.inspector();
		auto hold = [&](const MV::Point<int>& a_at, bool a_down, int a_frames = 2) {
			mouse.simulate(a_at, a_down);
			a_app.runFrames(a_frames);
		};
		auto click = [&](const MV::Point<int>& a_at) {
			hold(a_at, false);
			hold(a_at, true);
			hold(a_at, false);
			mouse.stopSimulating();
		};
		auto dragRight = [&](const MV::Point<int>& a_from, int a_pixels) {
			hold(a_from, false);
			hold(a_from, true);
			for (int step = 1; step <= 4; ++step) {
				hold(MV::Point<int>(a_from.x + a_pixels * step / 4, a_from.y), true, 1);
			}
			hold(MV::Point<int>(a_from.x + a_pixels, a_from.y), false);
			mouse.stopSimulating();
		};
		auto find = [&](const std::function<bool(const std::shared_ptr<MV::Scene::Node>&)>& a_matches) {
			std::shared_ptr<MV::Scene::Node> result;
			std::function<void(const std::shared_ptr<MV::Scene::Node>&)> visit = [&](const std::shared_ptr<MV::Scene::Node>& a_node) {
				if (!result && a_matches(a_node)) {
					result = a_node;
				}
				for (auto&& child : *a_node) {
					visit(child);
				}
			};
			visit(a_app.scene());
			return result;
		};

		a_app.parallaxPreview(false);
		a_app.select(layerNode);
		a_app.runFrames(3);
		auto sprite = layerNode->component<MV::Scene::Sprite>(false, false);
		expect(static_cast<bool>(sprite), "the layer has a Sprite to inspect");
		if (sprite) {
			expect(inspector.rowScreenPoint("owner").has_value(), "componentOwner shows as the owner reference");

			auto subdivisionsField = inspector.rowScreenPoint("subdivisions");
			expect(subdivisionsField.has_value(), "the inspector shows a subdivisions field");
			if (subdivisionsField) {
				auto pointsBefore = sprite->pointSize();
				dragRight(*subdivisionsField, 40);
				expect(sprite->subdivisions() > 0 && sprite->pointSize() > pointsBefore, "scrubbing subdivisions subdivides the sprite");
				pressWithControl(SDLK_z);
				expect(sprite->subdivisions() == 0 && sprite->pointSize() == pointsBefore, "undo restores the sprite's grid");
			}

			auto textureBefore = sprite->texture(size_t{0});
			auto textureField = inspector.rowScreenPoint("texture");
			expect(textureField.has_value(), "the inspector shows the texture slot");
			if (textureField) {
				click(*textureField);
				expect(inspector.texturePickerOpen(), "a press on the texture opens the picker");
				auto tile = inspector.texturePickerTileScreenPoint(1);
				expect(tile.has_value(), "the picker lists textures");
				if (tile) {
					click(*tile);
					expect(!inspector.texturePickerOpen() && sprite->texture(size_t{0}) != textureBefore, "choosing a tile sets the texture and closes the picker");
					pressWithControl(SDLK_z);
					expect(sprite->texture(size_t{0}) == textureBefore, "undo restores the texture");
				}
			}

			auto anchorsHeader = inspector.rowScreenPoint("anchors", false);
			expect(anchorsHeader.has_value() && !inspector.rowScreenPoint("anchor min").has_value(), "anchors is a closed group");
			if (anchorsHeader) {
				click(*anchorsHeader);
				auto anchorField = inspector.rowScreenPoint("anchor max");
				expect(anchorField.has_value(), "a press opens the anchors group");
				if (anchorField) {
					auto anchorBefore = sprite->anchors().anchor();
					dragRight(*anchorField, 30);
					expect(!(sprite->anchors().anchor() == anchorBefore), "scrubbing an anchor field moves the anchor");
					pressWithControl(SDLK_z);
					expect(sprite->anchors().anchor() == anchorBefore, "undo restores the anchor");
				}
				auto parentField = inspector.rowScreenPoint("parent");
				auto other = find([&](const std::shared_ptr<MV::Scene::Node>& a_node) {
					return a_node != layerNode && a_node->component<MV::Scene::Drawable>(false, false);
				});
				if (parentField && other) {
					expect(inspector.dropNode(other, *parentField), "a node dropped on the anchor's parent field is taken");
					a_app.runFrames(2);
					expect(sprite->anchors().hasParent() && sprite->anchors().parent()->owner() == other, "the dropped node's drawable becomes the anchor parent");
					pressWithControl(SDLK_z);
					expect(!sprite->anchors().hasParent(), "undo clears the anchor parent");
				}
			}
		}

		auto emitterNode = find([](const std::shared_ptr<MV::Scene::Node>& a_node) {
			return static_cast<bool>(a_node->component<MV::Scene::Emitter>(false, false));
		});
		expect(emitterNode != nullptr, "the map has an emitter");
		if (emitterNode) {
			auto emitter = emitterNode->component<MV::Scene::Emitter>(false, false);
			a_app.select(emitterNode);
			a_app.runFrames(3);
			auto groupHeader = inspector.rowScreenPoint("spawnProperties", false);
			expect(groupHeader.has_value(), "the emitter's spawn properties show as a group");
			if (groupHeader) {
				click(*groupHeader);
				auto rateField = inspector.rowScreenPoint("maximumSpawnRate");
				expect(rateField.has_value(), "the group opens onto the nested properties");
				if (rateField) {
					float rateBefore = emitter->properties().maximumSpawnRate;
					dragRight(*rateField, 30);
					expect(emitter->properties().maximumSpawnRate != rateBefore, "scrubbing a nested field changes the emitter");
					pressWithControl(SDLK_z);
					expect(emitter->properties().maximumSpawnRate == rateBefore, "undo restores the nested value");
				}
			}
		}
		a_app.select(layerNode->parent());
		a_app.parallaxPreview(true);
		a_app.runFrames(5);
	}

	auto savedMap = (std::filesystem::temp_directory_path() / "workbench_selftest_map.scene").string();
	expect(a_app.saveScene(savedMap), "the map saves during a preview");
	try {
		auto saved = MV::Scene::Node::load(savedMap, a_managers.services, false);
		std::shared_ptr<MV::Scene::Node> savedLayer;
		std::function<void(const std::shared_ptr<MV::Scene::Node>&)> findSaved = [&](const std::shared_ptr<MV::Scene::Node>& a_node) {
			if (!savedLayer && a_node->id() == layerNode->id() && a_node->component<MV::Scene::Parallax>(true, false)) {
				savedLayer = a_node;
			}
			for (auto&& child : *a_node) {
				findSaved(child);
			}
		};
		findSaved(saved);
		expect(savedLayer != nullptr, "the saved map still has the layer");
		if (savedLayer) {
			expect(sameXY(savedLayer->position(), layerRest), "a save during a preview writes the layer at rest");
			expect(sameXY(savedLayer->component<MV::Scene::Parallax>()->localOffset(), layerRest), "the saved localOffset is the authored one");
		}
	} catch (const std::exception& a_error) {
		expect(false, std::string("the saved map loads back: ") + a_error.what());
	}
	std::filesystem::remove(savedMap, ignored);
	a_app.runFrames(3);
	expect(!sameXY(layerNode->position(), layerRest), "the preview resumes after the save");
	a_app.select(nullptr);
	expect(!a_app.parallaxPreviewing() && sameXY(layerNode->position(), layerRest), "deselecting ends the preview and puts the layer back at rest");

	// The dock's layout: read as text, changed by dragging a tab and a splitter, saved, loaded, reset and kept in the settings file
	{
		using Zone = Workbench::DockSpace::DropZone;
		auto& docking = a_app.docking();
		auto& mouse = *a_managers.services.get<MV::TapDevice>();
		auto drag = [&](const MV::Point<int>& a_from, const MV::Point<int>& a_to) {
			auto hold = [&](const MV::Point<int>& a_at, bool a_down) {
				mouse.simulate(a_at, a_down);
				a_app.runFrames(2);
			};
			hold(a_from, false);
			hold(a_from, true);
			for (int step = 1; step <= 4; ++step) {
				hold(MV::Point<int>(a_from.x + (a_to.x - a_from.x) * step / 4, a_from.y + (a_to.y - a_from.y) * step / 4), true);
			}
			hold(a_to, false);
			mouse.stopSimulating();
		};
		const std::string starting = "H 0.18 (L 0 [Hierarchy]) (H 0.72 (V 0.7 (L 0 [Scene]) (L 0 [Project,Console])) (L 0 [Inspector])) X [File,Settings]";
		expect(a_app.dockLayout() == starting, "the starting layout reads as text, with File and Settings closed: " + a_app.dockLayout());
		const std::string allDocked = "H 0.18 (L 0 [Hierarchy,File]) (H 0.72 (V 0.7 (L 0 [Scene]) (L 0 [Project,Console])) (L 0 [Inspector,Settings]))";
		expect(a_app.dockLayout(allDocked) && a_app.dockLayout() == allDocked, "a layout with every panel docked applies from text");
		a_app.runFrames(3);

		auto layoutSettings = (std::filesystem::temp_directory_path() / "workbench_selftest_layout_settings.txt").string();
		std::filesystem::remove(layoutSettings, ignored);
		a_app.useSettingsFile(layoutSettings);

		drag(docking.tabScreenPoint("Settings"), docking.dropScreenPoint("Project", Zone::Right));
		const std::string moved = "H 0.18 (L 0 [Hierarchy,File]) (H 0.72 (V 0.7 (L 0 [Scene]) (H 0.7 (L 0 [Project,Console]) (L 0 [Settings]))) (L 0 [Inspector]))";
		expect(a_app.dockLayout() == moved, "a tab dragged onto a panel's right edge docks beside it: " + a_app.dockLayout());
		expect(MV::fileContents(layoutSettings).find("layout = " + moved) != std::string::npos, "the new layout is written to the settings file as it changes");

		drag(docking.tabScreenPoint("Settings"), docking.dropScreenPoint("Inspector", Zone::Center));
		expect(a_app.dockLayout() == "H 0.18 (L 0 [Hierarchy,File]) (H 0.72 (V 0.7 (L 0 [Scene]) (L 0 [Project,Console])) (L 1 [Inspector,Settings]))", "dragged back onto the Inspector it is a tab there again, in front: " + a_app.dockLayout());

		auto splitter = docking.rootSplitterScreenPoint();
		drag(splitter, splitter + MV::Point<int>(160, 0));
		auto widened = a_app.dockLayout();
		expect(widened.rfind("H 0.18 ", 0) != 0 && widened.rfind("H 0.", 0) == 0 && widened.find("[Hierarchy,File]") != std::string::npos, "dragging the outer splitter changes its ratio: " + widened);
		expect(MV::fileContents(layoutSettings).find("layout = " + widened) != std::string::npos, "and letting go writes it");

		auto layoutFile = (std::filesystem::temp_directory_path() / "workbench_selftest.bindlayout").string();
		expect(a_app.saveDockLayout(layoutFile), "the layout saves to a file");
		a_app.resetDockLayout();
		expect(a_app.dockLayout() == starting, "Reset Layout returns to the starting layout: " + a_app.dockLayout());
		expect(a_app.loadDockLayout(layoutFile) && a_app.dockLayout() == widened, "a saved layout loads back exactly: " + a_app.dockLayout());
		a_app.runFrames(3);

		expect(!a_app.dockLayout("H 0.5 (L 0 [Scene]") && a_app.dockLayout() == widened, "text that is not a layout is refused and changes nothing");
		expect(a_app.dockLayout("V 0.6 (L 0 [Scene,NoSuchPanel]) (L 0 [Project,Console])"), "a layout naming only some panels applies");
		auto partial = a_app.dockLayout();
		for (std::string title : { "Hierarchy", "File", "Scene", "Project", "Inspector", "Settings" }) {
			expect(partial.find(title) != std::string::npos, "every panel is still somewhere after a partial layout, missing " + title + " in " + partial);
		}
		expect(partial.find("NoSuchPanel") == std::string::npos, "and a title no panel has is dropped");
		a_app.runFrames(3);

		a_app.resetDockLayout();
		a_app.useSettingsFile("");
		expect(a_app.dockLayout(moved), "a layout applies from text");
		a_app.useSettingsFile(layoutSettings);
		expect(a_app.dockLayout() == starting, "the layout the settings file holds is restored when the file is read: " + a_app.dockLayout());
		a_app.useSettingsFile("");
		std::filesystem::remove(layoutSettings, ignored);
		std::filesystem::remove(layoutFile, ignored);
		a_app.runFrames(5);
		expect(a_app.viewport().size.width > 0.0f && a_app.errors() == 0, "the editor keeps running on the restored layout");

		// Settings starts closed and opens floating from its menu
		auto* settingsTab = docking.panelTitled("Settings");
		auto clickAt = [&](const MV::Point<int>& a_at) {
			for (bool down : { false, true, false }) {
				mouse.simulate(a_at, down);
				a_app.runFrames(2);
			}
			mouse.stopSimulating();
		};
		expect(!docking.open(settingsTab) && !docking.open(docking.panelTitled("File")), "File and Settings start closed");
		clickAt(a_app.menus().titleScreenPoint(2));
		if (a_app.menus().isOpen()) {
			clickAt(a_app.menus().rowScreenPoint(0));
		}
		expect(docking.open(settingsTab) && docking.floating(settingsTab), "Settings > Editor Settings opens the Settings panel floating");
		clickAt(docking.closeButtonScreenPoint("Settings"));
		expect(!docking.open(settingsTab) && a_app.dockLayout() == starting, "its x closes it again");

		// closing a panel and bringing it back
		expect(a_app.dockLayout(allDocked), "every panel docks again");
		a_app.runFrames(3);
		auto* inspectorTab = docking.panelTitled("Inspector");
		auto click = [&](const MV::Point<int>& a_at) {
			for (bool down : { false, true, false }) {
				mouse.simulate(a_at, down);
				a_app.runFrames(2);
			}
			mouse.stopSimulating();
		};
		a_app.showSettings();
		a_app.runFrames(2);
		click(docking.closeButtonScreenPoint("Settings"));
		expect(!docking.open(settingsTab) && a_app.dockLayout() == "H 0.18 (L 0 [Hierarchy,File]) (H 0.72 (V 0.7 (L 0 [Scene]) (L 0 [Project,Console])) (L 0 [Inspector])) X [Settings]", "the x on a tab closes its panel and the layout says so: " + a_app.dockLayout());
		click(a_app.menus().titleScreenPoint(3));
		expect(a_app.menus().isOpen() && a_app.menus().rowLabel(5) == "Settings (closed)" && a_app.menus().rowLabel(4) == "Inspector", "Window marks the closed panel");
		if (a_app.menus().isOpen()) {
			click(a_app.menus().rowScreenPoint(5));
		}
		expect(docking.open(settingsTab) && !docking.floating(settingsTab) && a_app.dockLayout() == "H 0.18 (L 0 [Hierarchy,File]) (H 0.72 (V 0.7 (L 0 [Scene]) (L 0 [Project,Console])) (L 1 [Inspector,Settings]))", "choosing it in Window brings it back beside the panel it was closed next to: " + a_app.dockLayout());

		// floating: a tab dropped on the middle of a panel floats there
		drag(docking.tabScreenPoint("Settings"), docking.dropScreenPoint("Scene", Zone::Float));
		expect(docking.floating(settingsTab) && !docking.floating(inspectorTab) && a_app.dockLayout().find(" F ") != std::string::npos, "a tab dropped on the middle of a panel floats: " + a_app.dockLayout());
		auto floated = docking.floatRect("Settings");
		expect(floated.size.width > 100.0f && floated.size.height > 100.0f, "the floating panel has a size");
		auto insideFloat = a_managers.renderer.screenFromWorld(MV::point(floated.position.x + floated.size.width * 0.5f, floated.position.y + floated.size.height * 0.7f));
		expect(!docking.viewportAt(a_managers.renderer.worldFromScreen(insideFloat)), "the scene beneath a floating panel does not take its presses");
		auto selectedBeforeFloatPress = a_app.selected();
		click(insideFloat);
		expect(a_app.selected() == selectedBeforeFloatPress, "a press on the floating panel does not pick in the scene");

		auto bar = docking.floatBarScreenPoint("Settings");
		drag(bar, bar + MV::Point<int>(-140, 90));
		auto movedFloat = docking.floatRect("Settings");
		expect(std::abs((movedFloat.position.x - floated.position.x) - a_managers.renderer.worldFromScreen(MV::Point<int>(-140, 0)).x) < 3.0f && movedFloat.position.y > floated.position.y + 20.0f && std::abs(movedFloat.size.width - floated.size.width) < 1.0f, "dragging its tab bar moves it and keeps its size");
		auto corner = docking.floatCornerScreenPoint("Settings");
		drag(corner, corner + MV::Point<int>(120, 80));
		auto sizedFloat = docking.floatRect("Settings");
		expect(sizedFloat.size.width > movedFloat.size.width + 40.0f && sizedFloat.size.height > movedFloat.size.height + 30.0f && std::abs(sizedFloat.position.x - movedFloat.position.x) < 1.0f, "dragging its corner resizes it and keeps its place");

		auto withFloat = a_app.dockLayout();
		a_app.resetDockLayout();
		expect(!docking.open(settingsTab) && a_app.dockLayout() == starting, "Reset Layout returns a floating panel to where the editor starts it");
		expect(a_app.dockLayout(withFloat) && docking.floating(settingsTab), "a layout with a floating panel applies from text");
		a_app.runFrames(3);
		auto restoredFloat = docking.floatRect("Settings");
		expect(std::abs(restoredFloat.position.x - sizedFloat.position.x) < 2.0f && std::abs(restoredFloat.size.height - sizedFloat.size.height) < 2.0f, "and puts it back where it was, at its size");

		drag(docking.tabScreenPoint("Settings"), docking.dropScreenPoint("Inspector", Zone::Center));
		expect(!docking.floating(settingsTab) && a_app.dockLayout() == "H 0.18 (L 0 [Hierarchy,File]) (H 0.72 (V 0.7 (L 0 [Scene]) (L 0 [Project,Console])) (L 1 [Inspector,Settings]))", "a floating panel's tab dropped on a tab bar docks there: " + a_app.dockLayout());

		expect(a_app.dockLayout("L 0 [Scene] X [Hierarchy,File,Project,Inspector,Settings,Console]") && !docking.closePanel(docking.panelTitled("Scene")) && docking.open(docking.panelTitled("Scene")), "the last panel docked in the main area cannot be closed");
		a_app.resetDockLayout();
		a_app.runFrames(5);
		expect(a_app.dockLayout() == starting && a_app.errors() == 0, "the layout ends where it started");
	}
}

static int RunWorkbench(bool a_smoke, const std::string& a_screenshotPath = "", bool a_loadDefaultScene = false, bool a_selectForGizmo = false, bool a_selfTest = false) {
	MV::editMode(true);   // before engine creation: registers InEditMode + hot reload
	Managers managers({ "", "" });
	int scriptCallbackErrors = 0;
	auto jaiEngine = MV::makeScriptEngine(managers.services);
	jaiEngine->set_script_error_handler([raw = jaiEngine.get(), &scriptCallbackErrors](const std::string& a_message) {
		++scriptCallbackErrors;
		auto trace = raw->format_stack_trace();
		MV::error("Script callback failed: ", a_message, trace.empty() ? "" : "\n", trace);
	});
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
	if (a_smoke) {
		// the smoke modes keep one logical size on every display, so their shots compare
		if (workbenchUiScale > 1.0f) {
			// the world first: the window's resize rebuilds the projection from it
			MV::Size<int> launchSize(static_cast<int>(1600 * workbenchUiScale), static_cast<int>(900 * workbenchUiScale));
			managers.renderer.world().resize(MV::cast<MV::PointPrecision>(launchSize));
			managers.renderer.window().resize(launchSize);
			if (auto* sdlWindow = SDL_GL_GetCurrentWindow()) {
				SDL_SetWindowPosition(sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
			}
		}
	} else {
		managers.renderer.window().maximize();
	}
	Workbench::Theme workbenchTheme(workbenchUiScale);
	MV::FontDefinition::make(managers.textLibrary, "default", "Fonts/Verdana.ttf", 14);
	MV::FontDefinition::make(managers.textLibrary, "small", "Fonts/Verdana.ttf", 9);
	MV::FontDefinition::make(managers.textLibrary, "big", "Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);
	MV::FontDefinition::make(managers.textLibrary, "wb-small", "Fonts/Verdana.ttf", static_cast<int>(12 * workbenchUiScale));

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
	// the app reports how much of the title bar's left takes presses once it has built its menus
	auto workbenchTitleInteractiveWidth = std::make_shared<int>(0);
	auto& workbenchWindow = managers.renderer.window();
	workbenchWindow.hitTest([&workbenchWindow, workbenchResizeBorder, workbenchTitleBarHeight, workbenchTitleButtonStrip, workbenchTitleInteractiveWidth](const MV::Point<int>& a_point, const MV::Size<int>& a_size) -> SDL_HitTestResult {
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
			if (a_point.x < *workbenchTitleInteractiveWidth) {
				return SDL_HITTEST_NORMAL;   // menus and transport buttons
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
	*workbenchTitleInteractiveWidth = static_cast<int>(app.titleBarInteractiveWidth());
	if (a_loadDefaultScene) {
		app.loadScene("Scenes/map.scene");
	}
	if (a_selectForGizmo && app.scene()) {
		auto mapCenter = app.scene()->bounds().centerPoint();
		auto center = MV::point(mapCenter.x, mapCenter.y);
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
		} else {
			app.reportError("the gizmo shot found no node to select");
		}
	}
	if (!a_smoke) {
		app.useSettingsFile(MV::playerPreferencesPath() + "workbench_settings.txt");
		return app.run();
	}
	// no run without a person at it may stop on a message box
	app.askAboutUnsavedChanges = [](const std::string&) { return Workbench::App::UnsavedChoice::DISCARD; };
	app.askToConfirm = [](const std::string&) { return true; };
	if (a_selfTest) {
		RunWorkbenchSelfTest(app, managers);
	}
	int result = app.runFrames(a_loadDefaultScene ? 240 : 120, a_screenshotPath);
	if (scriptCallbackErrors > 0) {
		std::cout << "Workbench smoke: " << scriptCallbackErrors << " script callback error(s)" << std::endl;
		result = 1;
	}
	return result;
}

// Loads every shipped scene/prefab/catalog headless and reports pass/fail. Guards the
// jai asset format end to end; scripts run under the strict static checker, so a typed
// mistake in one script is that script's [FAIL]. Run: BindstoneClient.exe -verifyassets

// Headless balance sample: Bindstone.exe -arena life,decay void,earth [seconds=150] [seed=1] [budget=8]
// budget = spawns per building (-1 unlimited): bounded waves let the stronger side actually leak.
// Loadouts alternate the two elements across the eight slots; prints one JSON line with the leaks
// each well took (a leak = a creature reaching the enemy well), the winner and the spawn counts.
static int RunArena(const std::string& a_left, const std::string& a_right, double a_seconds, uint32_t a_seed, int a_budget) {
	MV::scriptDebugEnabled(false);
	Managers managers({ "", "" });
	auto jaiEngine = MV::makeScriptEngine(managers.services);
	managers.services.connect<jai::engine>(jaiEngine.get());
	static MV::TapDevice arenaMouse;
	managers.services.connect(&arenaMouse);
	managers.renderer.makeHeadless();
	if (!managers.renderer.initialize(MV::Size<int>(1280, 720))) {
		std::cout << "[arena] renderer init failed" << std::endl;
		return 1;
	}
	MV::FontDefinition::make(managers.textLibrary, "default", "Fonts/Verdana.ttf", 14);
	MV::initializeSpineBindings();
	GameData data(managers, true);
	auto root = MV::Scene::Node::make(managers.renderer);
	auto makePlayer = [](int64_t a_id, const std::string& a_handle, const std::string& a_elements) {
		auto player = std::make_shared<InGamePlayer>();
		player->id = a_id;
		player->handle = a_handle;
		std::vector<std::string> elements;
		std::stringstream parts(a_elements);
		std::string element;
		while (std::getline(parts, element, ',')) { if (!element.empty()) { elements.push_back(element); } }
		for (int slot = 0; slot < 8; ++slot) {
			player->loadout.buildings.push_back(elements[slot % elements.size()]);
			player->loadout.skins.push_back("");
		}
		return player;
	};
	auto left = makePlayer(1, "left", a_left);
	auto right = makePlayer(2, "right", a_right);
	int scriptErrors = 0;
	jaiEngine->set_script_error_handler([&](const std::string& a_message) { ++scriptErrors; MV::error("Script callback failed: ", a_message); });
	jaiEngine->add_global("SpawnBudget", jai::script_value(static_cast<int64_t>(a_budget), jaiEngine.get()));
	auto instance = ArenaGameInstance::make(root, data, arenaMouse, left, right);
	if (const char* budgetOverride = std::getenv("MV_ARENA_SEARCH_BUDGET")) { instance->path()->searchBudget(std::atoll(budgetOverride)); }
	instance->scheduleOpeningUpgrades(a_seed, 4.0);
	const double step = 1.0 / 10.0;
	while (instance->elapsed < a_seconds && instance->step(step)) {}
	auto spawnList = [&](int a_side) {
		std::string result;
		for (auto& kv : instance->spawned[a_side]) { result += (result.empty() ? "" : ",") + ("\"" + kv.first + "\":" + std::to_string(kv.second)); }
		return "{" + result + "}";
	};
	std::cout << "{\"arena\":1,\"left\":\"" << a_left << "\",\"right\":\"" << a_right << "\",\"seed\":" << a_seed
		<< ",\"seconds\":" << instance->elapsed
		<< ",\"leaksOnLeft\":" << instance->leaksAgainst(TeamSide::LEFT) << ",\"leaksOnRight\":" << instance->leaksAgainst(TeamSide::RIGHT)
		<< ",\"winner\":\"" << (instance->winner == TeamSide::LEFT ? "left" : instance->winner == TeamSide::RIGHT ? "right" : "none") << "\""
		<< ",\"spawnedLeft\":" << spawnList(0) << ",\"spawnedRight\":" << spawnList(1)
		<< ",\"budget\":" << a_budget << ",\"scriptErrors\":" << scriptErrors << "}" << std::endl;
	return 0;
}

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
		"Assets/Creatures/Void_T1/Default/unit.bindsnap",
		"Assets/Creatures/Water_DropletHeal/Default/unit.bindsnap",
		"Assets/Creatures/Water_DropletHurt/Default/unit.bindsnap",
		"Assets/Creatures/Water_T1/Default/unit.bindsnap",
		"Assets/Prefabs/Button.bindsnap",
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
		if (strcmp(argv[i], "-arena") == 0 && i + 2 < argc) {
			double seconds = i + 3 < argc ? std::atof(argv[i + 3]) : 150.0;
			uint32_t seed = i + 4 < argc ? static_cast<uint32_t>(std::atoi(argv[i + 4])) : 1u;
			int budget = i + 5 < argc ? std::atoi(argv[i + 5]) : 8;
			return RunArena(argv[i + 1], argv[i + 2], seconds > 0.0 ? seconds : 150.0, seed, budget);
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
		if (strcmp(argv[i], "-workbenchselftest") == 0) {
			return RunWorkbench(true, "", false, false, true);
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
