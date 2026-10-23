// Bindstone (MV) device-binding lifetime tests: a texture binding or a drawable's device buffers that outlive the
// renderer (a script engine's globals keep scene nodes past Managers::renderer on client shutdown) are released by the
// renderer's teardown while its device lives, so their later destruction never reaches the destroyed device.

#include <jaiscript/testing/foundry.hpp>
#include "MV/Render/render.h"
#include "MV/Render/textures.h"
#include "MV/Render/deviceBindings.h"
#include "MV/Render/Scene/node.h"
#include "MV/Render/Scene/sprite.h"

#include <memory>

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
		}
	};

}

using device_binding_suite = mv_tests::device_binding_suite;
FOUNDRY_REGISTER(device_binding_suite)
