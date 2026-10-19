// MV::Scene::Emitter: the particle pipeline runs on a jai::thread_pool against a snapshot the main thread takes, so
// edits, texture swaps, a throwing stage and destruction mid-flight are all safe. Foundry, built into mv_tests.exe.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/thread_pool.hpp>
#include "MV/Render/render.h"
#include "MV/Render/Scene/node.h"
#include "MV/Render/Scene/emitter.h"
#include "MV/Render/textures.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace jai::foundry;
using MV::Scene::Node;
using MV::Scene::Emitter;

namespace mv_tests {

namespace {
	void steadySpawn(MV::Scene::EmitterSpawnProperties &a_result, float a_secondsBetweenParticles, float a_lifespan) {
		a_result.maximumParticles = 2000;
		a_result.minimumSpawnRate = a_secondsBetweenParticles;
		a_result.maximumSpawnRate = a_secondsBetweenParticles;
		a_result.minimumPosition = MV::Point<>(-10.0f, -10.0f, 0.0f);
		a_result.maximumPosition = MV::Point<>(10.0f, 10.0f, 0.0f);
		a_result.minimum->maxLifespan = a_lifespan;
		a_result.maximum->maxLifespan = a_lifespan;
		a_result.minimum->beginScale = MV::Scale(4.0f, 4.0f);
		a_result.maximum->beginScale = MV::Scale(8.0f, 8.0f);
		a_result.minimum->endScale = MV::Scale(1.0f, 1.0f);
		a_result.maximum->endScale = MV::Scale(2.0f, 2.0f);
		a_result.minimum->beginSpeed = 10.0f;
		a_result.maximum->beginSpeed = 40.0f;
		a_result.minimum->beginColor = MV::Color(1.0f, 1.0f, 1.0f, 1.0f);
		a_result.maximum->beginColor = MV::Color(0.0f, 0.5f, 1.0f, 1.0f);
	}

	MV::Scene::EmitterSpawnProperties& steadySpawn(const std::shared_ptr<Emitter> &a_emitter, float a_secondsBetweenParticles, float a_lifespan) {
		MV::Scene::EmitterSpawnProperties properties;
		steadySpawn(properties, a_secondsBetweenParticles, a_lifespan);
		a_emitter->properties(properties);
		return a_emitter->properties();
	}

	bool meshIsConsistent(const std::shared_ptr<Emitter> &a_emitter) {
		const size_t points = a_emitter->pointSize();
		auto& indices = a_emitter->pointIndices();
		if (points % 4 != 0 || indices.size() != points / 4 * 6) { return false; }
		for (auto index : indices) {
			if (index >= points) { return false; }
		}
		return true;
	}

	struct EmitterScene {
		explicit EmitterScene(size_t a_workers) :
			pool(a_workers) {

			renderer.makeHeadless();
			root = Node::make(renderer, "root");
			emitter = root->make("emitter")->attach<Emitter>(pool)->simulateWhileHeadless(true);
		}

		void settledFrame(double a_dt = 1.0 / 60.0) {
			root->update(a_dt);
			pool.wait_idle();
			pool.run_posted();
		}

		//Applies a_change before the gate and again while the frame's stages run (they hold until it is applied).
		template<typename Change>
		bool frameChangingMidFlight(Change &&a_change, double a_dt = 1.0 / 60.0) {
			a_change(false);
			const int startedBefore = stagesStarted.load();
			root->update(a_dt);
			bool result = false;
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
			while (stagesStarted.load() == startedBefore && std::chrono::steady_clock::now() < deadline) {
				std::this_thread::yield();
			}
			if (stagesStarted.load() != startedBefore) {
				a_change(true);
				result = true;
			}
			changeApplied = true;
			pool.wait_idle();
			changeApplied = false;
			pool.run_posted();
			return result;
		}

		void holdStagesForMidFlightChanges() {
			emitter->pipelineProbe([this](size_t) {
				++stagesStarted;
				while (!changeApplied.load()) { std::this_thread::yield(); }
			});
		}

		std::atomic<int> stagesStarted{0};
		std::atomic<bool> changeApplied{false};

		jai::thread_pool pool;
		MV::Draw2D renderer;
		std::shared_ptr<Node> root;
		std::shared_ptr<Emitter> emitter;
	};
}

class emitter_suite : public suite {
public:
	emitter_suite() : suite("MV Emitter") {}

	void forge_tests() override {
		test("particles spawn, age and die, and the mesh always has four points and six indices per particle", [this]() {
			EmitterScene scene(4);
			steadySpawn(scene.emitter, 0.005f, 0.5f);
			size_t peak = 0;
			for (int frame = 0; frame < 90; ++frame) {
				scene.settledFrame();
				check(meshIsConsistent(scene.emitter), "mesh consistent while spawning");
				peak = std::max(peak, scene.emitter->pointSize() / 4);
			}
			check(peak >= 75, "three spawns a frame build up a population, saw " + std::to_string(peak));
			check(peak <= 96, "a half-second lifespan caps it near thirty frames of spawns, saw " + std::to_string(peak));
			scene.emitter->disable();
			for (int frame = 0; frame < 60; ++frame) {
				scene.settledFrame();
				check(meshIsConsistent(scene.emitter), "mesh consistent while dying out");
			}
			check_eq(size_t{0}, scene.emitter->pointSize(), "every particle dies after its lifespan once spawning stops");
			scene.emitter->enable();
			for (int frame = 0; frame < 10; ++frame) {
				scene.settledFrame();
			}
			check(scene.emitter->pointSize() > 0, "re-enabled, it spawns again");
		});

		test("maximumParticles caps the population", [this]() {
			EmitterScene scene(3);
			steadySpawn(scene.emitter, 0.0001f, 10.0f).maximumParticles = 37;
			for (int frame = 0; frame < 30; ++frame) {
				scene.settledFrame();
				check(scene.emitter->pointSize() / 4 <= 37, "never more than maximumParticles");
			}
			check_eq(size_t{37 * 4}, scene.emitter->pointSize());
		});

		test("a headless renderer does not simulate unless the emitter asks to", [this]() {
			EmitterScene scene(2);
			scene.emitter->simulateWhileHeadless(false);
			std::atomic<int> stages{0};
			steadySpawn(scene.emitter, 0.001f, 1.0f);
			scene.emitter->pipelineProbe([&](size_t) { ++stages; });
			for (int frame = 0; frame < 20; ++frame) {
				scene.settledFrame();
			}
			check_eq(0, stages.load());
		});

		test("editing the properties every frame while pipelines run", [this]() {
			EmitterScene scene(4);
			steadySpawn(scene.emitter, 0.001f, 0.3f);
			scene.holdStagesForMidFlightChanges();
			int midFlight = 0;
			for (int frame = 0; frame < 300; ++frame) {
				midFlight += scene.frameChangingMidFlight([&](bool a_inFlight) {
					float wobble = static_cast<float>((frame * 2 + (a_inFlight ? 1 : 0)) % 17);
					if (frame % 3 == 0) {
						MV::Scene::EmitterSpawnProperties replacement;
						steadySpawn(replacement, 0.0005f + wobble * 0.0001f, 0.1f + wobble * 0.02f);
						scene.emitter->properties(replacement);
					} else if (frame % 3 == 1) {
						auto& edited = scene.emitter->properties();
						edited.minimumPosition = MV::Point<>(-wobble, -wobble, 0.0f);
						edited.maximumPosition = MV::Point<>(wobble, wobble, 0.0f);
						edited.maximum->maxLifespan = 0.2f + wobble * 0.01f;
						edited.maximum->beginColor = MV::Color(wobble / 17.0f, 0.0f, 1.0f, 1.0f);
						edited.minimumDirectionDeg(MV::AxisAngles(0.0f, 0.0f, wobble * 20.0f));
					} else {
						scene.emitter->properties(MV::Scene::EmitterSpawnProperties{});
					}
				}) ? 1 : 0;
				check(meshIsConsistent(scene.emitter), "mesh consistent under edits");
			}
			check(midFlight >= 250, "most frames edit while their stages run, saw " + std::to_string(midFlight));
			scene.emitter->pipelineProbe({});
			steadySpawn(scene.emitter, 0.001f, 0.3f);
			for (int frame = 0; frame < 10; ++frame) {
				scene.settledFrame();
			}
			check(scene.emitter->pointSize() > 0, "still spawning after the edits");
		});

		test("swapping and clearing textures every frame while pipelines run", [this]() {
			EmitterScene scene(4);
			steadySpawn(scene.emitter, 0.001f, 0.3f);
			auto first = MV::DynamicTextureDefinition::make("emitterA", MV::Size<int>(64, 64))->makeHandle();
			auto second = MV::DynamicTextureDefinition::make("emitterB", MV::Size<int>(32, 32))->makeHandle();
			scene.holdStagesForMidFlightChanges();
			int midFlight = 0;
			int change = 0;
			for (int frame = 0; frame < 300; ++frame) {
				midFlight += scene.frameChangingMidFlight([&](bool) {
					switch (change++ % 4) {
						case 0:
							scene.emitter->texture(first);
						break;
						case 1:
							second->rawPercent(MV::BoxAABB<>(MV::point(0.25f, 0.25f), MV::point(0.5f + static_cast<float>(frame % 7) * 0.05f, 0.75f)));
							scene.emitter->texture(second);
						break;
						case 2:
							scene.emitter->clearTextures();
						break;
						default:
							scene.emitter->texture(MV::DynamicTextureDefinition::make("emitterC", MV::Size<int>(16, 16))->makeHandle());
						break;
					}
				}) ? 1 : 0;
				check(meshIsConsistent(scene.emitter), "mesh consistent under texture swaps");
			}
			check(midFlight >= 250, "most frames swap textures while their stages run, saw " + std::to_string(midFlight));
			scene.emitter->pipelineProbe({});
			second->rawPercent(MV::BoxAABB<>(MV::point(0.25f, 0.125f), MV::point(0.75f, 0.5f)));
			scene.emitter->texture(second);
			for (int frame = 0; frame < 3; ++frame) {
				scene.settledFrame();
			}
			check(scene.emitter->pointSize() > 0, "particles alive");
			check_eq(0.25f, scene.emitter->point(0).textureX, "the snapshot carries the handle's corners");
			check_eq(0.125f, scene.emitter->point(0).textureY);
			check_eq(0.75f, scene.emitter->point(2).textureX);
			check_eq(0.5f, scene.emitter->point(2).textureY);
		});

		test("replacing the properties while the pipeline waits for a worker leaves the emitter updating and destructible", [this]() {
			auto* scene = new EmitterScene(4);
			steadySpawn(scene->emitter, 0.01f, 1.0f);
			std::atomic<int> parked{0};
			std::atomic<bool> release{false};
			for (size_t i = 0; i < scene->pool.worker_count(); ++i) {
				scene->pool.submit([&]() {
					++parked;
					while (!release.load()) { std::this_thread::yield(); }
				});
			}
			while (parked.load() < static_cast<int>(scene->pool.worker_count())) { std::this_thread::yield(); }
			scene->root->update(0.02);
			MV::Scene::EmitterSpawnProperties replacement;
			steadySpawn(replacement, 0.01f, 1.0f);
			scene->emitter->properties(replacement);
			release = true;
			scene->pool.wait_idle();
			scene->pool.run_posted();
			for (int frame = 0; frame < 20; ++frame) {
				scene->settledFrame(0.02);
			}
			check(scene->emitter->pointSize() >= 20 * 4, "the emitter keeps spawning (a particle or two a frame), saw " + std::to_string(scene->emitter->pointSize() / 4) + " particles");
			check(meshIsConsistent(scene->emitter));

			std::atomic<bool> destroyed{false};
			std::thread destroyer([&]() {
				scene->emitter.reset();
				scene->root.reset();
				destroyed = true;
			});
			auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
			while (!destroyed.load() && std::chrono::steady_clock::now() < deadline) { std::this_thread::yield(); }
			if (destroyed.load()) {
				destroyer.join();
				delete scene;
			} else {
				destroyer.detach();
			}
			check(destroyed.load(), "the emitter's destructor returns");
		});

		test("destroying the emitter with a pipeline in flight returns at once and the pipeline finishes harmlessly", [this]() {
			EmitterScene scene(2);
			std::atomic<bool> entered{false};
			std::atomic<bool> release{false};
			steadySpawn(scene.emitter, 0.001f, 1.0f);
			scene.emitter->pipelineProbe([&](size_t) {
				entered = true;
				while (!release.load()) { std::this_thread::yield(); }
			});
			scene.root->update(1.0 / 60.0);
			auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (!entered.load() && std::chrono::steady_clock::now() < deadline) { std::this_thread::yield(); }
			check(entered.load(), "the pipeline started");

			std::weak_ptr<Emitter> watch = scene.emitter;
			auto started = std::chrono::steady_clock::now();
			scene.emitter.reset();
			scene.root.reset();
			auto took = std::chrono::steady_clock::now() - started;
			check(watch.expired(), "the emitter is gone while its pipeline still runs");
			check(took < std::chrono::seconds(1), "destruction does not wait for the pipeline");

			release = true;
			scene.pool.wait_idle();
			scene.pool.run_posted();
		});

		test("a stage that throws still finishes its pipeline, which the next update replaces", [this]() {
			EmitterScene scene(3);
			std::atomic<int> reported{0};
			scene.pool.on_exception([&](std::exception_ptr) { ++reported; });
			std::atomic<int> stages{0};
			std::atomic<bool> failing{true};
			steadySpawn(scene.emitter, 0.001f, 1.0f);
			scene.emitter->pipelineProbe([&](size_t a_slot) {
				++stages;
				if (failing.load() && a_slot == 0) { throw std::runtime_error("stage failed"); }
			});
			for (int frame = 0; frame < 10; ++frame) {
				scene.settledFrame();
			}
			check_eq(10, reported.load(), "one failure per frame reaches the pool's handler");
			check_eq(30, stages.load(), "every frame starts a pipeline: the gate reopens after a failure");
			check(meshIsConsistent(scene.emitter), "a failed pipeline leaves the last mesh in place");

			failing = false;
			for (int frame = 0; frame < 10; ++frame) {
				scene.settledFrame();
			}
			check(scene.emitter->pointSize() > 0, "the emitter keeps updating after the failures");
			check(meshIsConsistent(scene.emitter));
		});
	}
};

} // namespace mv_tests

using emitter_suite = mv_tests::emitter_suite;
FOUNDRY_REGISTER(emitter_suite)
