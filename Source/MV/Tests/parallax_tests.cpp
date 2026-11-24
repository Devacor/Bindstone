// Parallax: a layer at its rest view sits at localOffset; away from it, it follows (1 - translateRatio) of the camera's
// move from the rest centre and grows by (zoom / rest zoom)^(zoomRatio - 1) about the camera's centre. The camera is
// the one its View draws through. Headless; built into mv_tests.exe (see build_tests.bat).

#include <jaiscript/testing/foundry.hpp>
#include "mv_scene_host.hpp"
#include "MV/Render/Scene/view.h"
#include "MV/Render/Scene/camera.h"
#include "MV/Render/Scene/parallax.h"

#include <cmath>
#include <string>

using namespace jai::foundry;
using MV::Scene::Node;
using MV::Scene::View;
using MV::Scene::Camera;
using MV::Scene::Parallax;

namespace mv_tests {

	namespace {
		bool closeTo(float a_value, float a_expected, float a_tolerance = 0.01f) {
			return std::abs(a_value - a_expected) <= a_tolerance;
		}

		std::string text(const MV::Point<>& a_point) {
			return std::to_string(a_point.x) + "," + std::to_string(a_point.y);
		}

		// A View over the camera "eye" (1280x720 pixels, top-left origin) and a layer under a group at (100, 50), resting
		// at (10, 20) for the view centred on (740, 410) at zoom 1.
		struct LayeredScene {
			std::shared_ptr<Node> root;
			std::shared_ptr<Node> group;
			std::shared_ptr<Node> layerNode;
			std::shared_ptr<Camera> eye;
			std::shared_ptr<Parallax> layer;

			LayeredScene(SceneHost& a_host, const MV::Point<>& a_ratio, const MV::Point<>& a_zoomRatio = MV::Point<>(1.0f, 1.0f, 1.0f), float a_scale = 1.0f) {
				root = Node::make(a_host.renderer, "root");
				root->attach<View>()->cameraNode("eye");
				eye = root->make("eye")->position(MV::point(100.0f, 50.0f))->attach<Camera>().self();
				group = root->make("group")->position(MV::point(100.0f, 50.0f));
				layerNode = group->make("layer")->scale(MV::Scale(a_scale, a_scale, 1.0f));
				layer = layerNode->attach<Parallax>()->translateRatio(a_ratio)->zoomRatio(a_zoomRatio)->localOffset(MV::point(10.0f, 20.0f));
				root->update(0.0);
				layer->restViewFrom(eye);
				root->update(0.0);
			}
		};
	}

	class parallax_suite : public suite {
	public:
		parallax_suite() : suite("MV Parallax") {}

		void forge_tests() override {
			test("a new layer is enabled, and at its rest view sits at localOffset", [] {
				SceneHost host;
				LayeredScene scene(host, MV::point(0.5f, 0.25f));
				check(scene.layer->enabled(), "a new Parallax starts disabled");
				check(closeTo(scene.layer->restCenter().x, 640.0f) && closeTo(scene.layer->restCenter().y, 360.0f), "rest centre " + text(scene.layer->restCenter()));
				check_eq(1.0f, scene.layer->restZoom());
				check(closeTo(scene.layerNode->position().x, 10.0f) && closeTo(scene.layerNode->position().y, 20.0f), "at rest " + text(scene.layerNode->position()));
			});

			test("away from the rest centre a layer follows the share of the move translateRatio leaves", [] {
				SceneHost host;
				LayeredScene scene(host, MV::point(0.5f, 0.25f));
				scene.eye->owner()->translate(MV::point(200.0f, -40.0f));
				scene.root->update(0.0);
				check(closeTo(scene.layerNode->position().x, 10.0f + 0.5f * 200.0f) && closeTo(scene.layerNode->position().y, 20.0f + 0.75f * -40.0f), "moved to " + text(scene.layerNode->position()));
				check(closeTo(scene.layer->placement(MV::point(840.0f, 320.0f), 1.0f).x, scene.layerNode->position().x), "placement disagrees with the placed layer");
			});

			test("zooming about the camera's centre leaves a layer with zoomRatio 1 where it is", [] {
				SceneHost host;
				LayeredScene scene(host, MV::point(0.3f, 0.3f));
				scene.eye->zoomAround(2.5f, MV::Point<>(0.5f, 0.5f));
				scene.root->update(0.0);
				check(closeTo(scene.layerNode->position().x, 10.0f) && closeTo(scene.layerNode->position().y, 20.0f), "a centred zoom moved it to " + text(scene.layerNode->position()));
				check_eq(1.0f, scene.layerNode->scale().x);
			});

			test("a layer with zoomRatio 0 keeps its size and place on the screen as the camera zooms", [] {
				SceneHost host;
				LayeredScene scene(host, MV::point(0.0f, 0.0f), MV::point(0.0f, 0.0f));
				auto screenBefore = scene.layerNode->screenFromLocal(MV::point(30.0f, 30.0f));
				scene.eye->zoomAround(2.0f, MV::Point<>(0.25f, 0.75f));
				scene.root->update(0.0);
				check(closeTo(scene.layerNode->scale().x, 0.5f) && closeTo(scene.layerNode->scale().y, 0.5f), "scale " + std::to_string(scene.layerNode->scale().x));
				auto screenAfter = scene.layerNode->screenFromLocal(MV::point(30.0f, 30.0f));
				check(std::abs(screenAfter.x - screenBefore.x) <= 1 && std::abs(screenAfter.y - screenBefore.y) <= 1, "it moved on the screen");
			});

			test("zoomRatio 0.5 grows by the square root of the zoom, about the camera's centre", [] {
				SceneHost host;
				LayeredScene scene(host, MV::point(1.0f, 1.0f), MV::point(0.5f, 0.5f), 2.0f);
				scene.eye->zoomAround(4.0f, MV::Point<>(0.5f, 0.5f));
				scene.root->update(0.0);
				check(closeTo(scene.layerNode->scale().x, 1.0f), "authored 2 at a quarter: " + std::to_string(scene.layerNode->scale().x));
				const float centre = 640.0f;
				check(closeTo(scene.layerNode->position().x, centre + 0.5f * (10.0f - centre)), "not grown about the centre: " + text(scene.layerNode->position()));
			});

			test("disabled, a layer goes back to rest with the scale it was given", [] {
				SceneHost host;
				LayeredScene scene(host, MV::point(0.0f, 0.0f), MV::point(0.0f, 0.0f), 3.0f);
				scene.eye->owner()->translate(MV::point(50.0f, 50.0f));
				scene.eye->zoom(2.0f);
				scene.root->update(0.0);
				check(!closeTo(scene.layerNode->scale().x, 3.0f), "the zoom did not reach the layer");
				scene.layer->disable();
				check(closeTo(scene.layerNode->scale().x, 3.0f) && closeTo(scene.layerNode->position().x, 10.0f), "disable left the zoomed scale or the displaced position");
				scene.eye->owner()->translate(MV::point(50.0f, 50.0f));
				scene.root->update(0.0);
				check(closeTo(scene.layerNode->position().x, 10.0f), "a disabled layer moved");
			});

			test("taken off its node, a layer rests and follows the camera no more", [] {
				SceneHost host;
				LayeredScene scene(host, MV::point(0.0f, 0.0f));
				scene.eye->owner()->translate(MV::point(80.0f, 0.0f));
				scene.root->update(0.0);
				scene.layerNode->detach<Parallax>();
				check(closeTo(scene.layerNode->position().x, 10.0f), "a detached layer kept its displacement");
				scene.eye->owner()->translate(MV::point(80.0f, 0.0f));
				scene.root->update(0.0);
				check(closeTo(scene.layerNode->position().x, 10.0f), "a detached layer still follows the camera");
			});

			test("authoring rests a layer; a preview places it for the given camera, and a move then moves its rest", [] {
				SceneHost host;
				LayeredScene scene(host, MV::point(0.5f, 0.5f));
				scene.eye->owner()->translate(MV::point(100.0f, 0.0f));
				scene.layer->authoring(true);
				scene.root->update(0.0);
				check(closeTo(scene.layerNode->position().x, 10.0f), "an authoring layer is not at rest");
				scene.layerNode->position(MV::point(15.0f, 20.0f));
				scene.root->update(0.0);
				check(closeTo(scene.layer->localOffset().x, 15.0f), "a node moved at rest did not carry localOffset");
				scene.layer->authoringPreview(scene.eye);
				scene.root->update(0.0);
				check(closeTo(scene.layerNode->position().x, 15.0f + 50.0f), "the preview did not place it for the camera: " + text(scene.layerNode->position()));
				scene.layerNode->translate(MV::point(7.0f, 0.0f));
				scene.root->update(0.0);
				check(closeTo(scene.layer->localOffset().x, 22.0f) && closeTo(scene.layerNode->position().x, 22.0f + 50.0f), "a move while previewing did not move the rest by as much");
				scene.layer->authoringPreview(nullptr);
				check(closeTo(scene.layerNode->position().x, 22.0f), "ending the preview did not put it at rest");
			});

			test("its rest view and ratios round-trip, and it loads at rest", [] {
				SceneHost host;
				LayeredScene scene(host, MV::point(0.25f, 0.5f), MV::point(0.75f, 1.0f));
				scene.layer->restView(MV::point(300.0f, 200.0f), 1.5f);
				scene.root->update(0.0);
				ScratchScene file;
				scene.layer->authoring(true);
				scene.root->update(0.0);
				auto loaded = saveAndLoad(host, scene.root, file);
				auto layer = loaded->get("layer")->component<Parallax>();
				check(closeTo(layer->restCenter().x, 300.0f) && closeTo(layer->restCenter().y, 200.0f), "rest centre lost");
				check_eq(1.5f, layer->restZoom());
				check_eq(0.25f, layer->translateRatio().x);
				check_eq(0.75f, layer->zoomRatio().x);
				check(file.contents().find("zoomOffset") == std::string::npos, "the retired zoomOffset was written");
			});
		}
	};

}

using parallax_suite = mv_tests::parallax_suite;
FOUNDRY_REGISTER(parallax_suite)
