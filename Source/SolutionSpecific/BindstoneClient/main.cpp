#include "Game/gameEditor.h"
#include "MV/Utility/threadPool.hpp"
#include "MV/Utility/services.hpp"

#include "MV/ArtificialIntelligence/pathfinding.h"
#include "MV/Serialization/serialize.h"
#include "MV/Render/Scene/clickable.h"
//#include "vld.h"


#include "MV/Utility/scopeGuard.hpp"
#include "Game/NetworkLayer/synchronizeAction.h"
#include "Game/NetworkLayer/clientActions.h"
#include "MV/Network/networkObject.h"

#include "MV/Script/script.h"
#include "MV/Utility/generalUtility.h"
#include "Game/standardScriptMethods.h"

#include <filesystem>
#include <fstream>

#include "glm/mat4x4.hpp"

#include <jaiscript/serialization/json_archive.hpp>
#include "../../JaiScript/bench/fast_json.hpp"
#include "MV/Render/Scene/emitter.h"
#include <chrono>
#include <algorithm>
#include <vector>

struct Base {
	virtual ~Base() {}

	template <class Archive>
	void save(Archive & archive, std::uint32_t const) const {
		archive("baseMember", baseMember);
	}
	template <class Archive>
	void load(Archive & archive, std::uint32_t const /*version*/) {
		archive("baseMember", baseMember);
	}

	int baseMember = 1;
};

struct Derived1 : public Base {
	template <class Archive>
	void save(Archive & archive, std::uint32_t const) const {
		archive("derived1Member", derived1Member);
		Base::save(archive, 0);
	}
	template <class Archive>
	void load(Archive & archive, std::uint32_t const /*version*/) {
		archive("derived1Member", derived1Member);
		Base::load(archive, 0);
	}

	int derived1Member = 1;
};

class NetTypeA {
public:
	void synchronize(std::shared_ptr<NetTypeA> a_other) {
		std::cout << "A: " << name << " syncing with: " << a_other->name << "\n";
		name = a_other->name;
	}

	void destroy(std::shared_ptr<NetTypeA> a_other){
		std::cout << "A: DESTROY " << name << "\n";
	}

	template<class Archive> requires jai::serialization::jai_archive<Archive>
	void serialize(Archive& archive) { serialize(archive, 0); }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(JAI_NVP(name));
	}

	std::string name;
};

class NetTypeB {
public:
	void synchronize(std::shared_ptr<NetTypeB> a_other) {
		std::cout << "B: " << id << " syncing with: " << a_other->id << "\n";
		id = a_other->id;
	}

	void destroy(std::shared_ptr<NetTypeB> a_other) {
		std::cout << "B: DESTROY " << id << "\n";
		id = a_other->id;
	}

	template<class Archive> requires jai::serialization::jai_archive<Archive>
	void serialize(Archive& archive) { serialize(archive, 0); }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(JAI_NVP(id));
	}

	int id;
};

void PathfindingTest();


template<size_t SizeX, size_t SizeY, size_t Common>
MV::Matrix<SizeX, Common> M_1(const MV::Matrix<SizeX, SizeY>& a_lhs, const MV::Matrix<SizeY, Common>& a_rhs) {
	MV::Matrix<SizeX, Common> result;
	for (size_t x = 0; x != SizeX; ++x) {
		for (size_t c = 0; c != Common; c++) {
			for (size_t y = 0; y != SizeY; y++) {
				result(x, c) += a_lhs(x, y) * a_rhs(y, c);
			}
		}
	}
	return result;
}

template<size_t sizeAX, size_t sizeAY, size_t sizeBY>
MV::Matrix<sizeAX, sizeBY> M_2(const MV::Matrix<sizeAX, sizeAY>& A, const MV::Matrix<sizeAY, sizeBY>& B) {
	MV::Matrix<sizeAX, sizeBY> result;
	for (int i = 0; i < sizeAX; i++) {
		for (int j = 0; j < sizeBY; j++) {
			for (int k = 0; k < sizeAY; k++) {
				result(i, j) += A(i, k) * B(k, j);
			}
		}
	}
	return result;
}


// Manual benchmark: parse the real scene JSON into the production script_value DOM
// (std::map<script_value,script_value>) vs the flat arena parser, same input, same run.
// This quantifies, in the real engine, how much of a JaiScript scene load is the DOM build
// and how much a flat DOM would save. Run: BindstoneClient.exe -bench
static void RunJsonParseBenchmark(jai::engine* eng) {
	const char* candidates[] = {
		"Scenes/map.scene",
		"D:/git/Bindstone/Scenes/map.scene",
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
static void RunLoginUiRepair() {
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
			clickable->onAccept.connect("script", "auto pageId = \"Login\"; eval_file(\"Shared/selectText.script\");");
			retexture(root->get(inputId), "button.png");
		}
		retextureViews(button, MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(104.0f, 36.0f)));
	});
	repairScene("Assets/Prefabs/Button.prefab", [&](const std::shared_ptr<MV::Scene::Node>&, const std::shared_ptr<MV::Scene::Button>& button) {
		retextureViews(button, MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(104.0f, 47.0f)));
	});
	repairScene("Assets/Prefabs/SimpleButton.prefab", [&](const std::shared_ptr<MV::Scene::Node>&, const std::shared_ptr<MV::Scene::Button>& button) {
		button->bounds(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(100.0f, 30.0f)));
	});
	repairScene("Assets/Interface/Main/view.scene", nullptr);
	repairScene("Assets/Prefabs/LoginName.prefab", nullptr);
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

// Loads every shipped scene/prefab/catalog headless and reports pass/fail. Guards the
// jai asset format end to end. Run: BindstoneClient.exe -verifyassets
static void RunAssetVerification() {
	Managers managers({ "", "" });
	auto jaiEngine = MV::makeScriptEngine(managers.services);
	managers.services.connect<jai::engine>(jaiEngine.get());
	static MV::TapDevice verifyMouse;
	managers.services.connect(&verifyMouse);
	managers.renderer.makeHeadless();
	if (!managers.renderer.initialize(MV::Size<int>(1280, 720))) {
		std::cout << "[verify] renderer init failed\n";
		return;
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
		"Assets/BattleEffects/Missile/Default/unit.prefab",
		"Assets/Buildings/Life/Default/building.prefab",
		"Assets/Buildings/Void/Default/building.prefab",
		"Assets/Creatures/Air_T1/Default/unit.prefab",
		"Assets/Creatures/Decay_T1/Default/unit.prefab",
		"Assets/Creatures/Earth_Pebble/Default/unit.prefab",
		"Assets/Creatures/Earth_T1/Default/unit.prefab",
		"Assets/Creatures/Fire_T1/Default/unit.prefab",
		"Assets/Creatures/Life_T1/Default/unit.prefab",
		"Assets/Creatures/Lightning_T1/Default/unit.prefab",
		"Assets/Creatures/VoidElemental/Default/unit.prefab",
		"Assets/Creatures/Void_T1/Default/unit.prefab",
		"Assets/Creatures/Water_DropletHeal/Default/unit.prefab",
		"Assets/Creatures/Water_DropletHurt/Default/unit.prefab",
		"Assets/Creatures/Water_T1/Default/unit.prefab",
		"Assets/Prefabs/Button.prefab",
		"Assets/Prefabs/Creatures/voidElemental/voidElemental.prefab",
		"Assets/Prefabs/Life_T1.prefab",
		"Assets/Prefabs/LoginName.prefab",
		"Assets/Prefabs/missile.prefab",
		"Assets/Prefabs/Missiles/missile.prefab",
		"Assets/Prefabs/SimpleButton.prefab",
	};
	int passed = 0, failed = 0;
	for (const char* path : nodeFiles) {
		try {
			auto root = MV::Scene::Node::load(path, managers.services, false);
			fakeRoot->add(root);
			root->postLoadStep();
			// Save and reload to exercise the save side too (catches unregistered-on-save
			// polymorphic types that a load-only check misses).
			auto blob = MV::toBinaryString(root, managers.services);
			auto reloaded = MV::fromBinaryString<std::shared_ptr<MV::Scene::Node>>(blob, managers.services);
			if (!reloaded) { throw std::runtime_error("save/reload round-trip returned null"); }
			std::cout << "[OK]   " << path << " (root=" << root->id() << ", round-trip ok)" << std::endl;
			root->removeFromParent();
			++passed;
		} catch (std::exception& e) {
			std::cout << "[FAIL] " << path << ": " << e.what() << std::endl;
			++failed;
		}
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

	MV::Script script(*jaiEngine);
	verifyFamilyScripts<Creature>(script, "Creatures", passed, failed);
	verifyFamilyScripts<Building>(script, "Buildings", passed, failed);
	verifyFamilyScripts<BattleEffect>(script, "BattleEffects", passed, failed);

	std::cout << "\n[verify] " << passed << " passed, " << failed << " failed" << std::endl;
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
	MV::info("Hello world!");
	MV::debug(":D :D :D");
	MV::warning(":C :C :C");
	MV::error("Whoopse!");

	MV::Matrix<3, 1> m1;
	MV::Matrix<1, 3> m2;
	for (int i = 0; i < 3; ++i) {
		m1(i, 0) = (float)i + 3;
		m2(0, i) = (float)i;
	}

	auto ra = M_1(m2, m1);
	auto rb = M_2(m2, m1);
	//auto rc = M_3(m2, m1);
	MV::info("RESULT 1 a: ", ra);
	MV::info("RESULT 1 b: ", ra);
	//MV::info("RESULT 1 b: ", rc);

	auto r2a = M_1(m1, m2);
	MV::info("RESULT 2 a: ", r2a);
	auto r2b = M_2(m1, m2);
	MV::info("RESULT 2 b: ", r2b);
	//auto r2c = M_3(m1, m2);
	//MV::info("RESULT 2 c: ", r2c);

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
	auto derived = std::make_shared<Derived1>();
	derived->baseMember = 5;
	derived->derived1Member = 10;

	auto testEngine = MV::makeScriptEngine(MV::Services::instance());
	MV::Services::instance().connect<jai::engine>(testEngine.get());

	for (int i = 0; i < argc; ++i) {
		if (strcmp(argv[i], "-auto") == 0) {
			autoStart = true;
		}
		if (strcmp(argv[i], "-bench") == 0) {
			RunJsonParseBenchmark(testEngine.get());
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
			RunAssetVerification();
			return 0;
		}
		if (strcmp(argv[i], "-wirebuttons") == 0) {
			RunButtonWiring();
			return 0;
		}
		if (strcmp(argv[i], "-fixloginui") == 0) {
			RunLoginUiRepair();
			return 0;
		}
		if (strcmp(argv[i], "-emitterbench") == 0) {
			RunEmitterBenchmark();
			return 0;
		}
	}

	auto saved = MV::toJson(derived, MV::Services::instance());
	auto loaded = MV::fromJson<std::shared_ptr<Derived1>>(saved, MV::Services::instance());

	CreatureNetworkState stateSizeTest;
	stateSizeTest.animationName = "idle";
	stateSizeTest.creatureTypeId = "Life_T1";
	stateSizeTest.position = MV::Point<>(0, 0, 0);
	MV::info("Creature NetworkA DELTA SIZE: ", MV::toBinaryString(stateSizeTest, MV::Services::instance()).size());

	stateSizeTest.animationTime = 10.0;
	stateSizeTest.position = MV::Point<>(0, 0, 0);
	MV::info("Creature NetworkA DELTA SIZE POS: ", MV::toBinaryString(stateSizeTest, MV::Services::instance()).size());

	stateSizeTest.animationTime = 10.0;
	MV::info("Creature NetworkA DELTA SIZE POS: ", MV::toBinaryString(stateSizeTest, MV::Services::instance()).size());

	stateSizeTest.animationLoops = true;
	MV::info("Creature NetworkA DELTA SIZE NONE: ", MV::toBinaryString(stateSizeTest, MV::Services::instance()).size());

	std::cout << "done saveload test.";

	//PathfindingTest();
	//return 0;

	// 	pqxx::connection c("host=mutedvision.cqki4syebn0a.us-west-2.rds.amazonaws.com port=5432 dbname=bindstone user=m2tm password=Tinker123");
	// 	pqxx::work txn(c);
	// 
	// 
	// 	txn.exec(
	// 		"CREATE EXTENSION IF NOT EXISTS citext WITH SCHEMA public;"
	// 		"CREATE TABLE Instances ("
	// 		"	Id SERIAL primary key,"
	// 		"	Available boolean			default false,"
	// 		"	Host text						default '',"
	// 		"	Port integer				default 0,"
	// 		"	PlayerLeft	int				default 0,"
	// 		"	PlayerRight	int				default 0,"
	// 		"	LastUpdate timestamp without time zone default (now() at time zone 'utc'),"
	// 		"	Result JSON"
	// 		");");
	// 	txn.commit();

	// 	pqxx::result r = txn.exec(
	// 		"SELECT state "
	// 		"FROM players "
	// 		"WHERE email = " + txn.quote("maxmike@gmail.com"));
	// 
	// 	if (r.size() != 1)
	// 	{
	// 		std::cerr
	// 			<< "Expected 1 player with email " << txn.quote("maxmike@gmail.com") << ", "
	// 			<< "but found " << r.size() << std::endl;
	// 		return 1;
	// 	}
	// 
	// 	std::string status = r[0][0].c_str();
	// 	std::cout << "Updating employee #" << status << std::endl;


	//auto emailer = MV::Email::make("email-smtp.us-west-2.amazonaws.com", "587", { "AKIAIVINRAMKWEVUT6UQ", "AiUjj1lS/k3g9r0REJ1eCoy/xeYZgLXmB8Nrep36pUVw" });
	//emailer->send({ "jai", "jackaldurante@gmail.com", "Derv", "maxmike@gmail.com" }, "Testing new Interface", "Does this work too?");
// 	{
// 		std::ofstream codes("codes.txt");
// 		std::set<std::string> generated;
// 		while (generated.size() < 1800) {
// 			generated.insert("VF18_" + MV::randomString("23456789ABCDEFGHJKLMPQRSTUVWXYZ", 4) + "-" + MV::randomString("23456789ABCDEFGHJKLMPQRSTUVWXYZ", 4) + "-" + MV::randomString("23456789ABCDEFGHJKLMPQRSTUVWXYZ", 4));
// 		}
// 		for(auto&& codeString : generated)
// 		{
// 			codes << codeString << "\n";
// 		}
// 	}
	
	MV::NetworkObjectPool<NetTypeA, NetTypeB> pool, pool2;

	pool.onSpawn<NetTypeA>([](std::shared_ptr<MV::NetworkObject<NetTypeA>> a_newItem) {
		std::cout << "1A:" << a_newItem->self()->name << "\n";
	});
	pool2.onSpawn<NetTypeA>([&](std::shared_ptr<MV::NetworkObject<NetTypeA>> a_newItem) {
		std::cout << "2A:" << a_newItem->self()->name << "\n";
	});

	pool.onSpawn<NetTypeB>([](std::shared_ptr<MV::NetworkObject<NetTypeB>> a_newItem) {
		std::cout << "1B:" << a_newItem->self()->id << "\n";
	});
	pool2.onSpawn<NetTypeB>([&](std::shared_ptr<MV::NetworkObject<NetTypeB>> a_newItem) {
		std::cout << "2B:" << a_newItem->self()->id << "\n";
	});

	auto newItem = std::make_shared<NetTypeA>();
	newItem->name = "Happy!";

	auto newItem2 = std::make_shared<NetTypeB>();
	newItem2->id = 5;

	auto newObject = pool.spawn(newItem);
	auto newObject2 = pool.spawn(newItem2);
	auto testShared = newObject->shared_from_this();
	

	pool2.synchronize(MV::fromJson<decltype(pool.updated())>(MV::toJson(pool.updated(), MV::Services::instance()), MV::Services::instance()));

	newObject->modify()->name = "Unhappy!";

	pool2.synchronize(pool.updated());

	newObject->destroy();

	pool2.synchronize(pool.updated());

	GameEditor menu(name, pass, autoStart);

	menu.start();
	
	return 0;
}

void PathfindingTest() {
	auto world = MV::Map::make({ 20, 20 }, false);

	for (int i = 0; i < 20; ++i) {
		world->get({ 8, i }).staticBlock();
	}

	world->get({ 8, 6 }).staticUnblock();
	world->get({ 8, 7 }).staticUnblock();


	std::vector<std::shared_ptr<MV::NavigationAgent>> agents{
		MV::NavigationAgent::make(world, MV::Point<int>(2, 2), 2),
		MV::NavigationAgent::make(world, MV::Point<int>(0, 2), 2),
		MV::NavigationAgent::make(world, MV::Point<int>(2, 0), 2),
		MV::NavigationAgent::make(world, MV::Point<int>(0, 0), 2) };

	for (int i = 0; i < agents.size(); ++i) {
		agents[i]->debugId(i);
	}
	MV::Stopwatch timer;

	for (int i = 0; i < agents.size(); ++i) {
		agents[i]->onStart.connect("start", [=](std::shared_ptr<MV::NavigationAgent> agent) {
			std::cout << (i + 1) << ": START" << std::endl;
		});
		agents[i]->onStop.connect("start", [=](std::shared_ptr<MV::NavigationAgent> agent) {
			std::cout << (i + 1) << ": STOP" << std::endl;
		});
		agents[i]->onBlocked.connect("start", [=](std::shared_ptr<MV::NavigationAgent> agent) {
			std::cout << (i + 1) << ": BLOCKED" << std::endl;
		});
		agents[i]->onArrive.connect("start", [=](std::shared_ptr<MV::NavigationAgent> agent) {
			std::cout << (i + 1) << ": ARRIVED" << std::endl;
		});
		agents[i]->goal(MV::Point<int>(10, 17), 0);
	}


	int i = 0;

	/*
	for (int i = 0; i < 23; ++i) {
		for (auto&& agent : agents) {
			agent->update(1.0f);
			for (int y = 0; y < world->size().height; ++y) {
				for (int x = 0; x < world->size().width; ++x) {
					bool wasAgent = false;
					for (int i = 0; i < agents.size(); ++i) {
						if (agents[i]->overlaps({ x, y })) {
							wasAgent = true;
							break;
						}
					}
					if (!wasAgent) {
						if (world->blocked({ x, y })) {
						} else {
							world->get({ x, y }).clearance();
						}
					}
				}
			}
		}
	}
	*/

	while (std::find_if(agents.begin(), agents.end(), [](auto&& agent) {
		return agent->pathfinding();
	}) != agents.end()) {
		char a;
		while (true) {
			for (auto&& agent : agents) {
				agent->update(1.0f);
			}
			std::vector<MV::PathNode> pathNodes;
			for (int y = 0; y < world->size().height; ++y) {
				for (int x = 0; x < world->size().width; ++x) {
					bool wasAgent = false;
					for (int k = 0; k < agents.size(); ++k) {
						if (agents[k]->overlaps({ x, y })) {
							std::cout << "[" << (char)(k + 65) << "]";
							wasAgent = true;
							break;
						}
					}
					if (!wasAgent) {
						if (world->blocked({ x, y })) {
							std::cout << " " << "X" << " ";
						}
						else {
							std::cout << " " << world->get({ x, y }).clearance() << " ";
						}
					}
				}
				std::cout << std::endl;
			}
			std::cout << "\n\n\n" << std::endl;
			++i;
			std::cin >> a;
		}
	}
	std::cout << "YO" << std::endl;
}

