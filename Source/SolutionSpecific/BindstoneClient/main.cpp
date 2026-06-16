#if 1

#include "Game/gameEditor.h"
#include "MV/Utility/threadPool.hpp"
#include "MV/Utility/services.hpp"

#include "MV/ArtificialIntelligence/pathfinding.h"
#include "MV/Serialization/serialize.h"
//#include "vld.h"


#include "MV/Utility/scopeGuard.hpp"
#include "Game/NetworkLayer/synchronizeAction.h"
#include "MV/Network/networkObject.h"

#include "JaiScript/stdlib/stdlib.hpp"

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
	auto jaiEngine = jai::engine::make();
	jai::stdlib::register_all(*jaiEngine);
	jai::bind_registrar<MV::Services>(*jaiEngine, managers.services);
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

// Loads every shipped scene/prefab/catalog headless and reports pass/fail. Guards the
// jai asset format end to end. Run: BindstoneClient.exe -verifyassets
static void RunAssetVerification() {
	Managers managers({ "", "" });
	auto jaiEngine = jai::engine::make();
	jai::stdlib::register_all(*jaiEngine);
	jai::bind_registrar<MV::Services>(*jaiEngine, managers.services);
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
			std::cout << "[OK]   " << path << " (root=" << root->id() << ")" << std::endl;
			root->removeFromParent();
			++passed;
		} catch (std::exception& e) {
			std::cout << "[FAIL] " << path << ": " << e.what() << std::endl;
			++failed;
		}
	}
	try {
		GameData catalogCheck(managers, false);
		std::cout << "[OK]   Catalogs (creatures/buildings/battleEffects)" << std::endl;
		++passed;
	} catch (std::exception& e) {
		std::cout << "[FAIL] Catalogs: " << e.what() << std::endl;
		++failed;
	}
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

int main(int argc, char *argv[]) {
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

	auto testEngine = jai::engine::make();
	jai::stdlib::register_all(*testEngine);
	jai::bind_registrar<MV::Services>(*testEngine, MV::Services::instance());
	MV::Services::instance().connect<jai::engine>(testEngine.get());

	for (int i = 0; i < argc; ++i) {
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

	GameEditor menu(name, pass);

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

#else
/*
 *  rectangles.c
 *  written by Holmes Futrell
 *  use however you want
 */

//Grabbed from here: https://gist.github.com/Khaledgarbaya/86ac0b3cf9e5fc89cdcb
#include "SDL.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#include "MV/Script/script.h"
#include "MV/Utility/log.h"

#include "MV/Network/dynamicVariable.h"


using namespace std;

/*
 Produces a random int x, min <= x <= max
 following a uniform distribution
 */
int randomInt(int min, int max) {
    return min + rand() % (max - min + 1);
}

/*
 Produces a random float x, min <= x <= max
 following a uniform distribution
 */
float randomFloat(float min, float max) {
    return rand() / (float) RAND_MAX *(max - min) + min;
}

void fatalError(const char *string) {
    printf("%s: %s\n", string, SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, string, SDL_GetError(), NULL);
    exit(1);
}

static Uint64 prevTime = 0;

double updateDeltaTime() {
    Uint64 curTime;
    double deltaTime;
    
    if (prevTime == 0) {
        prevTime = SDL_GetPerformanceCounter();
    }
    
    curTime = SDL_GetPerformanceCounter();
    deltaTime = (double) (curTime - prevTime) / (double) SDL_GetPerformanceFrequency();
    prevTime = curTime;
    
    return deltaTime;
}

void render(SDL_Renderer *renderer) {
    Uint8 r, g, b;
    int renderW;
    int renderH;
    
    SDL_RenderGetLogicalSize(renderer, &renderW, &renderH);
    
    /*  Come up with a random rectangle */
    SDL_Rect rect;
    rect.w = randomInt(64, 128);
    rect.h = randomInt(64, 128);
    rect.x = randomInt(0, renderW);
    rect.y = randomInt(0, renderH);
    
    /* Come up with a random color */
    r = randomInt(50, 255);
    g = randomInt(50, 255);
    b = randomInt(50, 255);
    
    /*  Fill the rectangle in the color */
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_RenderFillRect(renderer, &rect);
    
    /* update screen */
    SDL_RenderPresent(renderer);
}

struct PointTest {
	int x;
	void display() {
		MV::info("Bindstone: PointTest Display");
	}
};

void customPrint(const std::string& string) {
	MV::info("Bindstone: " + string);
}

void hookDynamicVariable2(chaiscript::ChaiScript& a_script) {
	a_script.add(chaiscript::user_type<MV::DynamicVariable>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable()>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable(bool)>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable(int64_t)>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable(int)>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable(size_t)>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable(double)>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable(const std::string&)>(), "DynamicVariable");

	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, bool a_value) -> decltype(auto) {
		return a_self = a_value;
		}), "=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, int64_t a_value) -> decltype(auto) {
		return a_self = a_value;
		}), "=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, size_t a_value) -> decltype(auto) {
		return a_self = a_value;
		}), "=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, int a_value) -> decltype(auto) {
		return a_self = a_value;
		}), "=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, double a_value) -> decltype(auto) {
		return a_self = a_value;
		}), "=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, std::string a_value) -> decltype(auto) {
		return a_self = a_value;
		}), "=");

	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, bool a_value) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, int64_t a_value) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, size_t a_value) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, int a_value) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, double a_value) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, std::string a_value) -> decltype(auto) {
		return a_self == a_value;
		}), "==");

	a_script.add(chaiscript::fun([&](bool a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](int64_t a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](size_t a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](int a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](double a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](std::string a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self == a_value;
		}), "==");

	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, bool a_value) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, int64_t a_value) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, size_t a_value) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, int a_value) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, double a_value) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, std::string a_value) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");

	a_script.add(chaiscript::fun([&](bool a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](int64_t a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](size_t a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](int a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](double a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](std::string a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");

	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self) {
		return a_self.getBool();
		}), "bool");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self) {
		return a_self.getInt();
		}), "int");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self) {
		return a_self.getDouble();
		}), "double");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self) {
		return a_self.getString();
		}), "string");

	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self) {
		return a_self.clear();
		}), "clear");

	a_script.add(chaiscript::bootstrap::standard_library::map_type<std::map<std::string, MV::DynamicVariable>>("DynamicVariableMap"));
	a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<MV::DynamicVariable>>("DynamicVariableVector"));
}

int main(int argc, char *argv[]) {
    
    SDL_Window *window;
    SDL_Renderer *renderer;
    int done;
    SDL_Event event;
    int windowW;
    int windowH;
    
    /* initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fatalError("Could not initialize SDL");
    }
    
    /* seed random number generator */
    srand(time(NULL));
    
    /* create window and renderer */
    window = SDL_CreateWindow(NULL, 0, 0, 480, 320, SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == 0) {
        fatalError("Could not initialize Window");
    }
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        fatalError("Could not create renderer");
    }
    
    SDL_GetWindowSize(window, &windowW, &windowH);
    SDL_RenderSetLogicalSize(renderer, windowW, windowH);
    
    /* Fill screen with black */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
	chaiscript::ChaiScript script;
	script.add(chaiscript::fun([](const std::string& a_output) {
		MV::info("CHAISCRIPT: ", a_output);
	}), "log_chaiscript_output");
	script.eval("global print = fun(x){ log_chaiscript_output(to_string(x)); };");
	script.add(chaiscript::user_type<PointTest>(), "PointTest");
	script.add(chaiscript::constructor<PointTest()>(), "PointTest");
	script.add(chaiscript::fun(&PointTest::display), "display");
	script.add(chaiscript::fun(&PointTest::x), "x");
	script.add(chaiscript::fun(&customPrint), "log_chaiscript_output");
	script.add(chaiscript::bootstrap::standard_library::map_type<std::map<std::string, PointTest>>("PointTestMap"));
	
	script.eval(R"(
		print("Trying PointTest");
		var testStuff = PointTest();
		print("Trying PrintVar");
		print(testStuff.x);
		testStuff.x = 5;
		print(testStuff.x);
		testStuff.display();
	)");

	script.eval(R"(
		print("TRYING DYNAMIC VARIABLE");
		var v = DynamicVariable(10);
		print(v.int);
		var v2 = DynamicVariable("Test");
		print(v2.string);
		print("TEST SUCCESS");

		var testStuff2 = DynamicVariableMap();
		print("f1");
		testStuff2["v1"] = false;
		print("f2");
		print(testStuff2["v1"].bool);
		testStuff2["v2"] = 0;
		print("f3");
		print(testStuff2["v2"].int);
		)");
    /* Enter render loop, waiting for user to quit */
    done = 0;
    while (!done) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                done = 1;
            }
        }
        render(renderer);
        SDL_Delay(1);
    }
    
    /* shutdown SDL */
    SDL_Quit();
    
    return 0;
}
#endif