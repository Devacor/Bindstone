// Draw batching: the drawables of one material that sit next to each other in draw order share one draw, and anything
// between them that changes what a draw sees (another material, a stencil, a uniform the shader reads) splits the run
// without reordering it. Counted on the headless device through Draw2D's batching layer.

#include <jaiscript/testing/foundry.hpp>
#include "MV/Render/render.h"
#include "MV/Render/Backends/headlessDevice.h"
#include "MV/Render/Scene/node.h"
#include "MV/Render/Scene/sprite.h"
#include "MV/Render/Scene/stencil.h"
#include "MV/Render/Scene/clipped.h"

#include <memory>
#include <string>
#include <vector>

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

		static std::vector<float> transformationSeen(const MV::Render::HeadlessDevice& a_device, size_t a_draw) {
			auto& uniforms = a_device.drawsSeen()[a_draw].uniforms;
			auto found = uniforms.find("transformation");
			return found == uniforms.end() ? std::vector<float>() : found->second;
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
				auto plain = renderer.loadMaterial("plain", MV::PREMULTIPLY_ID);
				check_true(plain->batchable(), "a material without an apply function batches");
				auto custom = renderer.loadMaterial("custom", MV::PREMULTIPLY_ID);
				custom->setApply([](MV::Shader&, const MV::MaterialContext&) {});
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

			test("a node's alpha, times its parents', reaches the shader on a lone draw and on a merged one", [] {
				MV::Draw2D renderer;
				makeRenderer(renderer);
				auto& backend = dynamic_cast<MV::Render::HeadlessDevice&>(*renderer.backendDevice());
				auto root = Node::make(renderer, "root");
				auto inner = root->make("faded")->alpha(0.5f)->make("inner")->alpha(0.5f);
				addSprite(inner, 0);
				auto alphaSeen = [&](size_t a_draw) {
					auto& uniforms = backend.drawsSeen()[a_draw].uniforms;
					auto found = uniforms.find("alpha");
					return found == uniforms.end() || found->second.empty() ? -1.0f : found->second[0];
				};

				backend.recordDraws(true);
				auto counted = drawOnce(renderer, root);
				check_eq(1u, counted.loneDraws);
				check_eq(static_cast<size_t>(1), backend.drawsSeen().size());
				check_near(0.25f, alphaSeen(0), 1e-6f);

				addSprite(inner, 1);
				addSprite(inner, 2);
				addSprite(root, 3);
				backend.recordDraws(true);
				counted = drawOnce(renderer, root);
				check_eq(1u, counted.batches);
				check_eq(1u, counted.loneDraws, "the unfaded sprite after them does not join them");
				check_eq(static_cast<size_t>(2), backend.drawsSeen().size());
				check_eq(18u, backend.drawsSeen()[0].indexCount, "the three faded sprites share a draw");
				check_near(0.25f, alphaSeen(0), 1e-6f);
				check_near(1.0f, alphaSeen(1), 1e-6f);
				backend.recordDraws(false);
			});

			test("a child under a faded Clipped ends up at the alpha it has without clipping", [] {
				// headless, a Clipped skips its capture, so the test draws the capture as refreshTexture does
				auto overallAlpha = [](float a_aboveClipped, float a_clipped, float a_child) {
					MV::Draw2D renderer;
					makeRenderer(renderer);
					auto& backend = dynamic_cast<MV::Render::HeadlessDevice&>(*renderer.backendDevice());
					auto root = Node::make(renderer, "root");
					auto clipping = root->make("above")->alpha(a_aboveClipped)->make("clipping")->alpha(a_clipped);
					clipping->attach<MV::Scene::Clipped>()->bounds(MV::BoxAABB<>(MV::Size<>(40.0f, 40.0f)));
					clipping->make("child")->alpha(a_child)->attach<MV::Scene::Sprite>()->bounds(MV::BoxAABB<>(MV::Size<>(8.0f, 8.0f)));

					backend.recordDraws(true);
					root->draw();
					renderer.flushDraws();
					clipping->drawChildren(MV::TransformMatrix());
					renderer.flushDraws();
					auto& draws = backend.drawsSeen();
					float result = draws.size() == 2 ? 1.0f : -1.0f;
					for (auto&& seen : draws) {
						auto found = seen.uniforms.find("alpha");
						result *= found == seen.uniforms.end() || found->second.empty() ? -1.0f : found->second[0];
					}
					backend.recordDraws(false);
					return result;
				};
				check_near(0.5f, overallAlpha(1.0f, 0.5f, 1.0f), 1e-6f);
				check_near(0.25f, overallAlpha(0.5f, 0.5f, 1.0f), 1e-6f);
				check_near(0.125f, overallAlpha(0.5f, 0.5f, 0.5f), 1e-6f);
			});

			test("a capture draw leaves the node's own world alpha as it was", [] {
				MV::Draw2D renderer;
				makeRenderer(renderer);
				auto root = Node::make(renderer, "root");
				auto clipping = root->make("clipping")->alpha(0.5f);
				auto child = clipping->make("child")->alpha(0.5f);
				clipping->drawChildren(MV::TransformMatrix());
				check_near(0.25f, child->worldAlpha(), 1e-6f);
			});

			test("a bare material draws with the standard transformation, alone and merged", [] {
				MV::Draw2D renderer;
				makeRenderer(renderer);
				auto& backend = dynamic_cast<MV::Render::HeadlessDevice&>(*renderer.backendDevice());
				auto bare = renderer.loadMaterial("bare", MV::PREMULTIPLY_ID);
				auto root = Node::make(renderer, "root");
				auto lone = addSprite(root, 0);
				lone->material(bare);

				backend.recordDraws(true);
				check_eq(1u, drawOnce(renderer, root).loneDraws);
				const auto expected = renderer.cameraProjectionMatrix(lone->owner()->cameraId()) * lone->owner()->worldTransform();
				auto transformation = transformationSeen(backend, 0);
				check_eq(static_cast<size_t>(16), transformation.size());
				for (size_t i = 0; i < 16 && i < transformation.size(); ++i) {
					check_near(expected[i], transformation[i], 1e-6f);
				}

				addSprite(root, 1)->material(bare);
				addSprite(root, 2)->material(bare);
				backend.recordDraws(true);
				auto counted = drawOnce(renderer, root);
				check_eq(1u, counted.batches, "three sprites of one bare material merge");
				auto merged = transformationSeen(backend, 0);
				check_true(merged.size() == 16 && merged[0] == 1.0f && merged[5] == 1.0f && merged[12] == 0.0f, "the merged draw's vertices arrive in clip space");
				backend.recordDraws(false);
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
