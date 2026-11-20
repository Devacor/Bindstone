// Bindstone (MV) device-binding lifetime tests: a texture binding or a drawable's device buffers and uniform sets that
// outlive the renderer (a script engine's globals keep scene nodes past Managers::renderer on client shutdown) are
// released by the renderer's teardown while its device lives, so their later destruction never reaches the destroyed
// device; and a holder frees the uniform set it replaces rather than leaving it to the device's shutdown.

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/thread_pool.hpp>
#include "MV/Render/render.h"
#include "MV/Render/textures.h"
#include "MV/Render/deviceBindings.h"
#include "MV/Render/Backends/headlessDevice.h"
#include "MV/Render/Scene/node.h"
#include "MV/Render/Scene/sprite.h"
#include "MV/Render/Scene/emitter.h"

#include <memory>
#include <string>
#include <vector>

using namespace jai::foundry;
using MV::Render::DeviceBindings;

namespace mv_tests {

	class device_binding_suite : public suite {
	public:
		device_binding_suite() : suite("MV Device Bindings") {}

		void forge_tests() override {
			test("a texture binding that outlives its renderer is released by the renderer's teardown", [] {
				std::unique_ptr<MV::LoadedTextureData> survivor;
				MV::Render::Device* device = nullptr;
				{
					MV::Draw2D renderer;
					renderer.makeHeadless();
					device = renderer.device();
					const size_t holdersBefore = DeviceBindings::holdersOn(device);
					survivor = std::make_unique<MV::LoadedTextureData>(device, device->createTexture(MV::Render::TextureDesc{}, nullptr));
					check_true(survivor->bound().valid(), "the texture data holds a device binding");
					check_eq(holdersBefore + 1, DeviceBindings::holdersOn(device));
				}
				check_true(!survivor->bound().valid(), "the renderer's teardown releases the binding of texture data that outlives it");
				check_eq(static_cast<size_t>(0), DeviceBindings::holdersOn(device));
				survivor.reset();
			});

			test("a drawable that outlives its renderer has its device buffers released by the renderer's teardown", [] {
				std::shared_ptr<MV::Scene::Node> survivor;
				MV::Render::Device* device = nullptr;
				{
					MV::Draw2D renderer;
					renderer.makeHeadless();
					renderer.initialize(MV::Size<int>(64, 64));
					renderer.loadShaderCode(MV::PREMULTIPLY_ID, "", "");
					device = renderer.device();
					const size_t holdersBefore = DeviceBindings::holdersOn(device);
					survivor = MV::Scene::Node::make(renderer, "survivor");
					survivor->attach<MV::Scene::Sprite>()->bounds(MV::BoxAABB<>(MV::Size<>(8.0f, 8.0f)));
					survivor->draw();
					check_eq(holdersBefore + 1, DeviceBindings::holdersOn(device));
				}
				check_eq(static_cast<size_t>(0), DeviceBindings::holdersOn(device));
				survivor.reset();
			});

			test("a holder destroyed before its renderer frees its own device objects", [] {
				MV::Draw2D renderer;
				renderer.makeHeadless();
				MV::Render::Device* device = renderer.device();
				const size_t holdersBefore = DeviceBindings::holdersOn(device);
				{
					MV::LoadedTextureData data(device, device->createTexture(MV::Render::TextureDesc{}, nullptr));
					check_eq(holdersBefore + 1, DeviceBindings::holdersOn(device));
				}
				check_eq(holdersBefore, DeviceBindings::holdersOn(device));
			});

			test("a drawable frees the uniform set it replaces and the one it holds when it goes", [] {
				MV::Draw2D renderer;
				makeRenderer(renderer);
				auto& device = headlessDevice(renderer);
				const size_t baseline = device.liveUniformSets();
				{
					auto root = MV::Scene::Node::make(renderer, "root");
					std::vector<std::shared_ptr<MV::Scene::Sprite>> sprites;
					for (int i = 0; i < 3; ++i) {
						sprites.push_back(ownUniformSprite(root, "sprite" + std::to_string(i)));
					}
					root->draw();
					check_eq(baseline + 3, device.liveUniformSets(), "one set per drawable");
					for (int frame = 0; frame < 6; ++frame) {
						sprites[0]->blend(frame % 2 == 0 ? MV::BLEND_ADD : MV::BLEND_DEFAULT);
						sprites[1]->blend(frame % 3 == 0 ? MV::BLEND_MULTIPLY : MV::BLEND_SCREEN);
						root->draw();
						check_eq(baseline + 3, device.liveUniformSets(), "a new pipeline replaces a set instead of adding one");
					}
				}
				check_eq(baseline, device.liveUniformSets(), "destroying the drawables frees their sets");
			});

			test("an emitter frees the uniform set it replaces and the one it holds when it goes", [] {
				jai::thread_pool pool(2);
				MV::Draw2D renderer;
				makeRenderer(renderer);
				auto& device = headlessDevice(renderer);
				const size_t baseline = device.liveUniformSets();
				{
					auto root = MV::Scene::Node::make(renderer, "root");
					auto emitter = root->make("emitter")->attach<MV::Scene::Emitter>(pool)->simulateWhileHeadless(true);
					MV::Scene::EmitterSpawnProperties properties;
					properties.maximumParticles = 50;
					properties.minimumSpawnRate = 0.01f;
					properties.maximumSpawnRate = 0.01f;
					properties.minimum->maxLifespan = 5.0f;
					properties.maximum->maxLifespan = 5.0f;
					emitter->properties(properties);
					for (int frame = 0; frame < 8; ++frame) {
						emitter->blend(frame % 2 == 0 ? MV::BLEND_ADD : MV::BLEND_DEFAULT);
						root->update(1.0 / 30.0);
						pool.wait_idle();
						pool.run_posted();
						root->draw();
					}
					check_true(emitter->pointSize() > 0, "the emitter drew particles");
					check_eq(baseline + 1, device.liveUniformSets(), "one set, replaced as its pipeline changes");
				}
				pool.wait_idle();
				pool.run_posted();
				check_eq(baseline, device.liveUniformSets(), "destroying the emitter frees its set");
			});

			test("a drawable that outlives its renderer has its uniform set freed by the renderer's teardown", [] {
				std::shared_ptr<MV::Scene::Node> survivor;
				{
					MV::Draw2D renderer;
					makeRenderer(renderer);
					auto& device = headlessDevice(renderer);
					const size_t baseline = device.liveUniformSets();
					survivor = MV::Scene::Node::make(renderer, "survivor");
					ownUniformSprite(survivor, "sprite");
					survivor->draw();
					check_eq(baseline + 1, device.liveUniformSets());
					// the first thing the renderer's teardown does, here where the device can still be counted
					DeviceBindings::release(renderer.device());
					check_eq(baseline, device.liveUniformSets(), "the teardown frees the set of a drawable that outlives it");
					check_eq(static_cast<size_t>(0), DeviceBindings::holdersOn(renderer.device()));
				}
				survivor.reset();
			});
		}

		static void makeRenderer(MV::Draw2D& a_renderer) {
			a_renderer.makeHeadless();
			a_renderer.initialize(MV::Size<int>(64, 64));
			a_renderer.loadShaderCode(MV::PREMULTIPLY_ID, "", "");
		}

		static MV::Render::HeadlessDevice& headlessDevice(MV::Draw2D& a_renderer) {
			return dynamic_cast<MV::Render::HeadlessDevice&>(*a_renderer.device());
		}

		// A material callback keeps a sprite on its own uniform set, drawn on its own.
		static std::shared_ptr<MV::Scene::Sprite> ownUniformSprite(const std::shared_ptr<MV::Scene::Node>& a_parent, const std::string& a_id) {
			auto result = a_parent->make(a_id)->attach<MV::Scene::Sprite>()->bounds(MV::BoxAABB<>(MV::Size<>(8.0f, 8.0f)));
			result->materialSettings([](MV::Shader&) {});
			return result;
		}
	};

}

using device_binding_suite = mv_tests::device_binding_suite;
FOUNDRY_REGISTER(device_binding_suite)
