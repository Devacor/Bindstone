// A Camera's view as the scene sees it (never its node's scale), and a CameraConstraint holding that view to hard
// bounds while held and easing it into soft ones once let go. Headless; built into mv_tests.exe (see build_tests.bat).

#include <jaiscript/testing/foundry.hpp>
#include "mv_scene_host.hpp"
#include "MV/Render/Scene/view.h"
#include "MV/Render/Scene/camera.h"
#include "MV/Render/Scene/cameraConstraint.h"

#include <cmath>
#include <string>

using namespace jai::foundry;
using MV::Scene::Node;
using MV::Scene::View;
using MV::Scene::Camera;
using MV::Scene::CameraConstraint;

namespace mv_tests {

	namespace {
		bool closeTo(float a_value, float a_expected, float a_tolerance = 0.01f) {
			return std::abs(a_value - a_expected) <= a_tolerance;
		}

		std::string text(const MV::BoxAABB<>& a_box) {
			return std::to_string(a_box.minPoint.x) + "," + std::to_string(a_box.minPoint.y) + " - " + std::to_string(a_box.maxPoint.x) + "," + std::to_string(a_box.maxPoint.y);
		}

		// A 1280x720 pixel view of a "map" node through the camera "eye"; the map has a hard box of 2560x1440 at the
		// origin, and a soft one 100 inside it.
		struct ConstrainedMap {
			std::shared_ptr<Node> root;
			std::shared_ptr<Node> map;
			std::shared_ptr<Camera> eye;
			std::shared_ptr<CameraConstraint> limits;

			explicit ConstrainedMap(SceneHost& a_host) {
				root = Node::make(a_host.renderer, "root");
				root->attach<View>()->cameraNode("eye");
				eye = root->make("eye")->attach<Camera>().self();
				map = root->make("map");
				limits = map->attach<CameraConstraint>()->cameraNode("eye")
					->hard(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(2560.0f, 1440.0f)))
					->soft(MV::BoxAABB<>(MV::point(100.0f, 100.0f), MV::point(2460.0f, 1340.0f)))
					->hardZoom(0.01f, 4.0f)->softZoom(0.01f, 3.0f);
				root->update(0.0);
			}

			void run(double a_seconds) {
				for (double elapsed = 0.0; elapsed < a_seconds; elapsed += 1.0 / 60.0) {
					root->update(1.0 / 60.0);
				}
			}
		};
	}

	class camera_constraint_suite : public suite {
	public:
		camera_constraint_suite() : suite("MV CameraConstraint") {}

		void forge_tests() override {
			test("a camera sees its view size over its zoom from its node, whatever the node's scale", [] {
				SceneHost host;
				ConstrainedMap scene(host);
				scene.eye->owner()->position(MV::point(100.0f, 50.0f));
				scene.eye->zoom(2.0f);
				auto seen = scene.eye->visibleIn(scene.map);
				check(closeTo(seen.minPoint.x, 100.0f) && closeTo(seen.minPoint.y, 50.0f) && closeTo(seen.maxPoint.x, 740.0f) && closeTo(seen.maxPoint.y, 410.0f), "seen " + text(seen));
				scene.eye->owner()->scale(MV::Scale(3.0f, 3.0f, 1.0f));
				scene.root->scale(MV::Scale(0.5f, 0.5f, 1.0f));
				auto scaled = scene.eye->visibleIn(scene.root);
				check(closeTo(scaled.width(), 1280.0f) && closeTo(scaled.height(), 720.0f), "a scaled node changed the zoom: " + text(scaled));
				scene.root->scale(MV::Scale(1.0f, 1.0f, 1.0f));
				auto center = scene.eye->center();
				check(closeTo(center.x, 420.0f) && closeTo(center.y, 230.0f), "centre " + std::to_string(center.x) + "," + std::to_string(center.y));
			});

			test("zooming around a point of the view keeps the scene under it", [] {
				SceneHost host;
				ConstrainedMap scene(host);
				scene.eye->owner()->position(MV::point(300.0f, 200.0f));
				const MV::Point<> around(0.25f, 0.75f);
				auto before = scene.eye->sceneFromView(around);
				scene.eye->zoomAround(2.5f, around);
				auto after = scene.eye->sceneFromView(around);
				check(closeTo(before.x, after.x) && closeTo(before.y, after.y), "the point under the view moved");
				check_eq(2.5f, scene.eye->zoom());
			});

			test("held, a view dragged past the hard edge stops at it", [] {
				SceneHost host;
				ConstrainedMap scene(host);
				scene.limits->hold();
				scene.eye->owner()->position(MV::point(-500.0f, 2000.0f));
				scene.limits->clamp();
				auto seen = scene.limits->visible();
				check(closeTo(seen.minPoint.x, 0.0f) && closeTo(seen.maxPoint.y, 1440.0f), "not at the hard edge: " + text(seen));
				scene.run(0.5);
				seen = scene.limits->visible();
				check(closeTo(seen.minPoint.x, 0.0f) && closeTo(seen.maxPoint.y, 1440.0f), "a held view eased: " + text(seen));
			});

			test("let go between the hard and soft edges, it eases back over the soft edge at its strength", [] {
				SceneHost host;
				ConstrainedMap scene(host);
				scene.eye->owner()->position(MV::point(40.0f, 400.0f));
				scene.limits->release();
				scene.root->update(1.0 / 60.0);
				const float expected = 40.0f + 60.0f * static_cast<float>(1.0 - std::exp(-12.0 / 60.0));
				check(closeTo(scene.limits->visible().minPoint.x, expected, 0.05f), "one step eased to " + std::to_string(scene.limits->visible().minPoint.x) + ", not " + std::to_string(expected));
				scene.run(2.0);
				check(closeTo(scene.limits->visible().minPoint.x, 100.0f) && scene.limits->settled(), "it did not settle on the soft edge");
				check(closeTo(scene.limits->visible().minPoint.y, 400.0f), "the axis inside the box moved");
			});

			test("zoom clamps to the hard limits while held and eases to the soft ones after release", [] {
				SceneHost host;
				ConstrainedMap scene(host);
				scene.eye->owner()->position(MV::point(1000.0f, 600.0f));
				scene.limits->hold();
				scene.eye->zoom(5.0f);
				scene.limits->clamp();
				check_eq(4.0f, scene.eye->zoom());
				scene.limits->release();
				scene.run(2.0);
				check(closeTo(scene.eye->zoom(), 3.0f, 0.001f), "eased to zoom " + std::to_string(scene.eye->zoom()));
				scene.limits->hold();
				scene.eye->zoom(0.2f);
				scene.limits->clamp();
				check(closeTo(scene.eye->zoom(), 0.5f, 0.001f), "zoom-out does not stop where the view fits the hard box: " + std::to_string(scene.eye->zoom()));
				scene.limits->release();
				scene.run(2.0);
				const float softFit = std::max(1280.0f / 2360.0f, 720.0f / 1240.0f);
				check(closeTo(scene.eye->zoom(), softFit, 0.001f), "eased to zoom " + std::to_string(scene.eye->zoom()) + ", not the soft fit");
				auto seen = scene.limits->visible();
				check(closeTo(seen.minPoint.y, 100.0f) && closeTo(seen.maxPoint.y, 1340.0f), "the fitted axis is not the soft box's: " + text(seen));
			});

			test("an axis the view is wider than is centred on the box", [] {
				SceneHost host;
				ConstrainedMap scene(host);
				scene.limits->fitsMinimumZoom(false);
				scene.eye->zoom(0.25f);
				scene.limits->clamp();
				auto seen = scene.limits->visible();
				check(closeTo(seen.centerPoint().x, 1280.0f) && closeTo(seen.centerPoint().y, 720.0f), "not centred: " + text(seen));
			});

			test("a camera named by id is found anywhere in the scene; none leaves everything alone", [] {
				SceneHost host;
				ConstrainedMap scene(host);
				check(scene.limits->camera() == scene.eye, "the camera beside the map was not found");
				scene.limits->cameraNode("nobody");
				scene.eye->owner()->position(MV::point(-900.0f, -900.0f));
				scene.limits->clamp();
				scene.run(0.5);
				check(closeTo(scene.eye->owner()->position().x, -900.0f), "a constraint with no camera moved one");
				scene.limits->cameraNode("eye")->authoring(true);
				scene.run(0.5);
				check(closeTo(scene.eye->owner()->position().x, -900.0f), "an authoring constraint moved its camera");
			});

			test("its bounds, limits and camera round-trip", [] {
				SceneHost host;
				ConstrainedMap scene(host);
				scene.limits->easing(5.0f)->fitsMinimumZoom(false);
				ScratchScene file;
				auto loaded = saveAndLoad(host, scene.root, file);
				auto limits = loaded->get("map")->component<CameraConstraint>();
				check_eq(std::string("eye"), limits->cameraNode());
				check_eq(2560.0f, limits->hard().maxPoint.x);
				check_eq(100.0f, limits->soft().minPoint.y);
				check_eq(4.0f, limits->maximumZoom(false));
				check_eq(3.0f, limits->maximumZoom(true));
				check_eq(5.0f, limits->easing());
				check(!limits->fitsMinimumZoom(), "fitting lost");
				check(limits->camera() == loaded->get("eye")->component<Camera>().self(), "the loaded constraint does not find its camera");
			});
		}
	};

}

using camera_constraint_suite = mv_tests::camera_constraint_suite;
FOUNDRY_REGISTER(camera_constraint_suite)
