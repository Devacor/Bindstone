// Bindstone (MV) TextureHandle regression tests: 9-slice margin squeeze in a box smaller
// than the margins, flip state across a pack-based postLoadInitialize, and logicalSize
// measured against texture size (not content size). Headless CPU math only.

#include <jaiscript/testing/foundry.hpp>
#include "mv_scene_host.hpp"
#include "MV/Render/Scene/sprite.h"
#include "MV/Render/textures.h"

#include <cmath>
#include <memory>
#include <string>

using namespace jai::foundry;
using MV::Scene::Node;

namespace mv_tests {

	static void checkNear(float a_expected, float a_actual, float a_tolerance, const std::string& a_message) {
		check_true(std::abs(a_expected - a_actual) <= a_tolerance, a_message + " (expected " + std::to_string(a_expected) + ", actual " + std::to_string(a_actual) + ")");
	}

	struct SizedTexture : MV::TextureDefinition {
		SizedTexture(const MV::Size<int>& a_size, const MV::Size<int>& a_contentSize) :
			MV::TextureDefinition("sized", false) {
			textureSize = a_size;
			desiredSize = a_contentSize;
		}
		void reloadImplementation() override {}
	};

	class texture_handle_suite : public suite {
	public:
		texture_handle_suite() : suite("MV Texture Handles") {}

		void forge_tests() override {
			test("small-box 9-slice squeezes margins proportionally instead of collapsing to the midpoint", [] {
				MV::Draw2D renderer; renderer.makeHeadless();
				auto texture = MV::DynamicTextureDefinition::make("nine", MV::Size<int>(128, 64));
				auto handle = texture->makeHandle();
				handle->slice(MV::BoxAABB<>(MV::point(0.25f, 10.0f / 64.0f), MV::point(0.75f, 34.0f / 64.0f)));
				auto scene = Node::make(renderer, "root");
				auto sprite = scene->attach<MV::Scene::Sprite>()->texture(handle)->bounds(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::size(104.0f, 35.0f)));

				check_eq(size_t(16), sprite->pointSize(), "sliced sprite is not a 16-point mesh");
				checkNear(5.46875f, sprite->point(4).y, 1e-3f, "top interior row: 10px margin scaled by 35/64, not the 12.03 midpoint");
				checkNear(18.59375f, sprite->point(5).y, 1e-3f, "bottom interior row: 35 - 30px margin scaled by 35/64; interior band survives");
				checkNear(32.0f, sprite->point(4).x, 1e-3f, "unsqueezed axis leading margin untouched");
				checkNear(72.0f, sprite->point(7).x, 1e-3f, "unsqueezed axis trailing margin untouched");
				check_true(sprite->point(4).y < sprite->point(5).y, "interior rows stay distinct on the squeezed axis");
			});

			test("flipX survives a pack-based postLoadInitialize", [] {
				SceneHost host;
				auto pack = host.textures.pack("ui", &host.renderer);
				check_true(pack->add("a", MV::DynamicTextureDefinition::make("a", MV::Size<int>(32, 32))), "pack add failed");
				auto handle = MV::DynamicTextureDefinition::make("loose", MV::Size<int>(32, 32))->makeHandle()->pack("ui")->name("a")->flipX(true);
				auto scene = Node::make(host.renderer, "root");
				scene->make("icon")->attach<MV::Scene::Sprite>()->texture(handle);

				ScratchScene file;
				auto loaded = saveAndLoad(host, scene, file);
				auto loadedHandle = loaded->get("icon")->component<MV::Scene::Sprite>(true, false)->texture();
				check_true(loadedHandle->flipX(), "flipX dropped by the pack re-resolve");
				check_false(loadedHandle->flipY(), "flipY invented by the pack re-resolve");
				auto expected = pack->percentBounds(pack->shape("a").bounds);
				checkNear(expected.maxPoint.x, loadedHandle->rawPercent().minPoint.x, 1e-6f, "handle not re-pointed at the pack shape");
				checkNear(expected.minPoint.x, loadedHandle->rawPercent().maxPoint.x, 1e-6f, "handle not re-pointed at the pack shape");
			});

			test("logicalSize is the handlePercent extent of the texture size, not the content size", [] {
				auto texture = std::make_shared<SizedTexture>(MV::Size<int>(128, 64), MV::Size<int>(100, 50));
				auto handle = texture->makeHandle()->rawPercent(MV::BoxAABB<>(MV::point(0.25f, 0.25f), MV::point(0.75f, 0.75f)));
				checkNear(64.0f, handle->logicalSize().width, 1e-4f, "logicalSize scaled by contentSize");
				checkNear(32.0f, handle->logicalSize().height, 1e-4f, "logicalSize scaled by contentSize");
			});
		}
	};

}

using texture_handle_suite = mv_tests::texture_handle_suite;
FOUNDRY_REGISTER(texture_handle_suite)
