// Draw batching: the drawables of one material that sit next to each other in draw order share one draw, and anything
// between them that changes what a draw sees (another material, a stencil, a uniform the shader reads) splits the run
// without reordering it. Counted on the headless device through Draw2D's batching layer.

#include <jaiscript/testing/foundry.hpp>
#include "MV/Render/render.h"
#include "MV/Render/Backends/headlessDevice.h"
#include "MV/Render/Scene/node.h"
#include "MV/Render/Scene/sprite.h"
#include "MV/Render/Scene/stencil.h"

#include <memory>
#include <string>

using namespace jai::foundry;
using MV::Scene::Node;

namespace mv_tests {

	class batching_suite : public suite {
	public:
		batching_suite() : suite("MV Batching") {}

		static void makeRenderer(MV::Draw2D& a_renderer) {
			a_renderer.makeHeadless();
			a_renderer.initialize(MV::Size<int>(320, 240));
			a_renderer.loadShaderCode(MV::PREMULTIPLY_ID, "", "")->batchable(true);
		}

		static std::shared_ptr<MV::Scene::Sprite> addSprite(const std::shared_ptr<Node>& a_parent, int a_index) {
			return a_parent->make("sprite" + std::to_string(a_index))->position(MV::point(static_cast<float>(a_index) * 10.0f, 0.0f))->attach<MV::Scene::Sprite>()->bounds(MV::BoxAABB<>(MV::Size<>(8.0f, 8.0f)));
		}

		static MV::Render::DrawStatistics drawOnce(MV::Draw2D& a_renderer, const std::shared_ptr<Node>& a_root) {
			auto before = a_renderer.batches()->frame();
			a_root->draw();
			a_renderer.flushDraws();
			auto after = a_renderer.batches()->frame();
			MV::Render::DrawStatistics result;
			result.draws = after.draws - before.draws;
			result.queuedDraws = after.queuedDraws - before.queuedDraws;
			result.batches = after.batches - before.batches;
			result.loneDraws = after.loneDraws - before.loneDraws;
			return result;
		}

		void forge_tests() override {
			test("ten sprites of one material next to each other are one draw", [] {
				MV::Draw2D renderer;
				makeRenderer(renderer);
				auto root = Node::make(renderer, "root");
				for (int i = 0; i < 10; ++i) {
					addSprite(root, i);
				}
				auto counted = drawOnce(renderer, root);
				check_eq(10u, counted.queuedDraws);
				check_eq(1u, counted.batches);
				check_eq(1u, counted.draws);
			});

			test("with batching off each drawable draws on its own", [] {
				MV::Draw2D renderer;
				makeRenderer(renderer);
				renderer.batches()->batching(false);
				auto root = Node::make(renderer, "root");
				for (int i = 0; i < 10; ++i) {
					addSprite(root, i);
				}
				auto counted = drawOnce(renderer, root);
				check_eq(0u, counted.queuedDraws);
				check_eq(10u, counted.draws);
			});

			test("a sprite of another material splits the run where it stands", [] {
				MV::Draw2D renderer;
				makeRenderer(renderer);
				auto root = Node::make(renderer, "root");
				for (int i = 0; i < 7; ++i) {
					auto sprite = addSprite(root, i);
					if (i == 3) {
						sprite->blend(MV::BLEND_ADD);
					}
				}
				auto counted = drawOnce(renderer, root);
				check_eq(3u, counted.draws, "the three before it, it, and the three after it: the ones after never join the ones before");
				check_eq(2u, counted.batches, "the two runs of three merge");
				check_eq(1u, counted.loneDraws, "the one between them draws alone, from its own buffers");
			});

			test("a run shorter than the minimum draws each of its draws alone", [] {
				MV::Draw2D renderer;
				makeRenderer(renderer);
				auto root = Node::make(renderer, "root");
				for (size_t i = 0; i + 1 < MV::Render::BatchingDevice::MINIMUM_MERGED_RUN; ++i) {
					addSprite(root, static_cast<int>(i));
				}
				auto counted = drawOnce(renderer, root);
				check_eq(0u, counted.batches);
				check_eq(static_cast<uint32_t>(MV::Render::BatchingDevice::MINIMUM_MERGED_RUN - 1), counted.loneDraws);
			});

			test("a named material batches unless its apply function goes undeclared", [] {
				MV::Draw2D renderer;
				makeRenderer(renderer);
				auto standardTransformation = [](MV::Shader& a_shader, const MV::MaterialContext& a_context) {
					a_shader.set("transformation", a_context.renderer.cameraProjectionMatrix(a_context.cameraId) * a_context.worldTransform);
				};
				check_true(renderer.loadMaterial("bare", MV::PREMULTIPLY_ID)->batchable(), "a material without an apply function batches");
				auto plain = renderer.loadMaterial("plain", MV::PREMULTIPLY_ID);
				plain->setApply(standardTransformation).batchable(true);
				auto custom = renderer.loadMaterial("custom", MV::PREMULTIPLY_ID);
				custom->setApply(standardTransformation);
				check_true(!custom->batchable(), "one with an apply function draws on its own until it says otherwise");

				auto root = Node::make(renderer, "root");
				for (int i = 0; i < 6; ++i) {
					addSprite(root, i)->material(i < 3 ? plain : custom);
				}
				check_eq(4u, drawOnce(renderer, root).draws, "the three plain ones share a draw, the three custom ones draw alone");

				custom->batchable(true);
				check_eq(2u, drawOnce(renderer, root).draws, "declared batchable, the custom ones share one too, apart from the plain ones");
			});

			test("a draw nothing joins draws from its own buffers, which stay put from frame to frame", [] {
				MV::Draw2D renderer;
				makeRenderer(renderer);
				auto root = Node::make(renderer, "root");
				auto sprite = addSprite(root, 0);
				auto& backend = dynamic_cast<MV::Render::HeadlessDevice&>(*renderer.backendDevice());
				drawOnce(renderer, root);
				const size_t buffersAfterFirstFrame = backend.liveBuffers();
				for (int frame = 0; frame < 3; ++frame) {
					auto counted = drawOnce(renderer, root);
					check_eq(1u, counted.loneDraws);
					check_eq(0u, counted.batches);
				}
				check_eq(buffersAfterFirstFrame, backend.liveBuffers(), "no stream buffer and no new buffer for a lone draw");
			});

			test("a draw that went alone skips the queue until the next probing frame", [] {
				MV::Draw2D renderer;
				makeRenderer(renderer);
				auto root = Node::make(renderer, "root");
				addSprite(root, 0);
				auto* batches = renderer.batches();
				for (uint64_t frame = 0; frame < 2 * MV::Render::BatchingDevice::PROBE_INTERVAL; ++frame) {
					auto counted = drawOnce(renderer, root);
					check_eq(1u, counted.draws);
					check_eq(batches->probing() ? 1u : 0u, counted.queuedDraws, "it queues only on a probing frame");
					batches->endFrame();
				}
				addSprite(root, 1);
				addSprite(root, 2);
				for (uint64_t frame = 0; frame < MV::Render::BatchingDevice::PROBE_INTERVAL; ++frame) {
					batches->endFrame();
				}
				check_true(batches->probing());
				check_eq(1u, drawOnce(renderer, root).batches, "on the probing frame the newcomers join it");
				batches->endFrame();
				auto counted = drawOnce(renderer, root);
				check_eq(3u, counted.queuedDraws, "and having merged, all three keep queueing");
				check_eq(1u, counted.batches);
			});

			test("a drawable's own material callback keeps it out of the queue", [] {
				MV::Draw2D renderer;
				makeRenderer(renderer);
				auto root = Node::make(renderer, "root");
				for (int i = 0; i < 3; ++i) {
					auto sprite = addSprite(root, i);
					if (i == 1) {
						sprite->materialSettings([](MV::Shader&) {});
					}
				}
				auto counted = drawOnce(renderer, root);
				check_eq(2u, counted.queuedDraws);
				check_eq(3u, counted.draws);
			});

			test("a uniform the shader reads keeps drawables apart where it differs", [] {
				MV::Draw2D renderer;
				makeRenderer(renderer);
				auto root = Node::make(renderer, "root");
				std::shared_ptr<MV::Scene::Sprite> middle;
				for (int i = 0; i < 7; ++i) {
					auto sprite = addSprite(root, i);
					if (i == 3) {
						middle = sprite;
					}
				}
				middle->owner()->update(0.5);
				auto counted = drawOnce(renderer, root);
				check_eq(3u, counted.draws, "the headless shader reads every uniform, so the middle sprite's later time splits the run");
				check_eq(2u, counted.batches);
			});

			test("a stencil splits the draws around it exactly as unbatched drawing does", [] {
				auto stencilScene = [](MV::Draw2D& a_renderer) {
					auto root = Node::make(a_renderer, "root");
					addSprite(root, 0);
					auto masked = root->make("masked");
					masked->attach<MV::Scene::Stencil>()->bounds(MV::BoxAABB<>(MV::Size<>(20.0f, 20.0f)));
					addSprite(masked, 1);
					addSprite(masked, 2);
					addSprite(masked, 3);
					addSprite(root, 4);
					return root;
				};
				MV::Draw2D batched;
				makeRenderer(batched);
				auto batchedCount = drawOnce(batched, stencilScene(batched));
				MV::Draw2D unbatched;
				makeRenderer(unbatched);
				unbatched.batches()->batching(false);
				auto unbatchedCount = drawOnce(unbatched, stencilScene(unbatched));
				check_eq(7u, unbatchedCount.draws, "a sprite, the mask, three masked sprites, the mask again, a sprite");
				check_eq(5u, batchedCount.draws, "only the three masked sprites share a draw: the stencil state changes on both sides of them");
			});
		}
	};

}

using mv_tests_batching_suite = mv_tests::batching_suite;
FOUNDRY_REGISTER(mv_tests_batching_suite)
