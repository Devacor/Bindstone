// Views and Cameras: a View gives its subtree a slot, its camera's projection and eye, and a rectangle of the window;
// a Camera's mask picks the groups that draw; a subject is drawn again through another View. Headless; built into
// mv_tests.exe (see build_tests.bat).

#include <jaiscript/testing/foundry.hpp>
#include "mv_scene_host.hpp"
#include "MV/Render/Scene/view.h"
#include "MV/Render/Scene/camera.h"
#include <jaiscript/signals/signal.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace jai::foundry;
using MV::Scene::Node;
using MV::Scene::View;
using MV::Scene::Camera;

namespace mv_tests {

	struct DrawRecord {
		std::string node;
		int32_t slot;
	};

	class DrawProbe : public MV::Scene::Component {
	public:
		DrawProbe(const std::weak_ptr<Node>& a_owner, std::vector<DrawRecord>* a_records) :
			Component(a_owner),
			records(a_records) {
		}

		bool draw() override {
			records->push_back({ owner()->id(), owner()->renderer().drawSlot(owner()->cameraId()) });
			return true;
		}

	private:
		std::vector<DrawRecord>* records;
	};

	static bool around(const MV::Point<int>& a_point, int a_x, int a_y) {
		return std::abs(a_point.x - a_x) <= 1 && std::abs(a_point.y - a_y) <= 1;
	}

	static std::string text(const MV::Point<int>& a_point) {
		return std::to_string(a_point.x) + "," + std::to_string(a_point.y);
	}

	class view_suite : public suite {
	public:
		view_suite() : suite("MV View") {}

		void forge_tests() override {
			test("a scene with no View draws through the window's own view", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto child = root->make("child");
				child->position(MV::point(100.0f, 50.0f));
				check_eq(int32_t(0), child->cameraId());
				check(around(child->screenFromLocal(MV::Point<>()), 100, 50), "unexpected screen point " + text(child->screenFromLocal(MV::Point<>())));
			});

			test("a View gives its subtree a slot of its own and gives it back when it goes", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto layer = root->make("layer");
				auto view = layer->attach<View>();
				auto deep = layer->make("a")->make("b");
				check(view->slot() > 0, "View has no slot");
				check_eq(view->slot(), deep->cameraId());
				check_eq(view->slot(), layer->cameraId());
				check_eq(int32_t(0), root->cameraId());

				auto late = Node::make(host.renderer, "late");
				layer->add(late);
				check_eq(view->slot(), late->cameraId());

				layer->detach<View>();
				check_eq(int32_t(0), deep->cameraId());
				check_eq(int32_t(0), late->cameraId());
			});

			test("a nested View keeps its own slot when its parent's changes", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto outer = root->make("outer");
				auto outerView = outer->attach<View>();
				auto inner = outer->make("inner");
				auto innerView = inner->attach<View>();
				auto leaf = inner->make("leaf");
				check(innerView->slot() != outerView->slot(), "nested Views share a slot");
				check_eq(innerView->slot(), leaf->cameraId());

				auto moved = Node::make(host.renderer, "moved");
				moved->add(inner);
				check_eq(innerView->slot(), leaf->cameraId());
			});

			test("a pixel camera's eye is its node: moving the camera moves what it sees", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto layer = root->make("layer");
				auto eye = layer->make("eye");
				eye->attach<Camera>()->units(Camera::Units::PIXELS);
				auto view = layer->attach<View>();
				auto thing = layer->make("thing");
				thing->position(MV::point(100.0f, 50.0f));
				check(around(thing->screenFromLocal(MV::Point<>()), 100, 50), "camera at the origin: " + text(thing->screenFromLocal(MV::Point<>())));

				eye->position(MV::point(40.0f, 10.0f));
				root->update(0.0);
				check(around(thing->screenFromLocal(MV::Point<>()), 60, 40), "camera moved: " + text(thing->screenFromLocal(MV::Point<>())));

				eye->component<Camera>()->zoom(2.0f);
				root->update(0.0);
				check(around(thing->screenFromLocal(MV::Point<>()), 120, 80), "camera zoomed: " + text(thing->screenFromLocal(MV::Point<>())));
			});

			test("a centred camera puts its node in the middle of the view", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto layer = root->make("layer");
				auto eye = layer->make("eye");
				eye->position(MV::point(500.0f, 500.0f));
				eye->attach<Camera>()->units(Camera::Units::PIXELS)->origin(Camera::Origin::CENTER);
				layer->attach<View>();
				auto thing = layer->make("thing");
				thing->position(MV::point(510.0f, 490.0f));
				check(around(thing->screenFromLocal(MV::Point<>()), 650, 350), "centred: " + text(thing->screenFromLocal(MV::Point<>())));
			});

			test("a reference camera fits its reference height, its width, or a blend of the two", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto layer = root->make("layer");
				auto camera = layer->make("eye")->attach<Camera>()->units(Camera::Units::REFERENCE)->width(640.0f)->height(180.0f);
				layer->attach<View>();
				auto thing = layer->make("thing");
				thing->position(MV::point(100.0f, 90.0f));
				check(around(thing->screenFromLocal(MV::Point<>()), 400, 360), "matching height: " + text(thing->screenFromLocal(MV::Point<>())));
				camera->match(0.0f);
				root->update(0.0);
				check(around(thing->screenFromLocal(MV::Point<>()), 200, 180), "matching width: " + text(thing->screenFromLocal(MV::Point<>())));
				camera->match(0.5f);
				root->update(0.0);
				check(around(thing->screenFromLocal(MV::Point<>()), 283, 255), "blended: " + text(thing->screenFromLocal(MV::Point<>())));
			});

			test("a user-scaled camera grows with the user's scale and the change is announced", [] {
				SceneHost host;
				float announced = 0.0f;
				auto receiver = host.renderer.onUiScaleChanged.connect([&](float a_scale) { announced = a_scale; });
				auto root = Node::make(host.renderer, "root");
				auto layer = root->make("layer");
				auto camera = layer->make("eye")->attach<Camera>()->userScaled(true);
				layer->attach<View>();
				auto thing = layer->make("thing");
				thing->position(MV::point(100.0f, 50.0f));
				host.renderer.window().userScale(1.5f);
				root->update(0.0);
				check_eq(1.5f, announced);
				check(around(thing->screenFromLocal(MV::Point<>()), 150, 75), "user scaled: " + text(thing->screenFromLocal(MV::Point<>())));
				camera->userScaled(false);
				root->update(0.0);
				check(around(thing->screenFromLocal(MV::Point<>()), 100, 50), "not user scaled: " + text(thing->screenFromLocal(MV::Point<>())));
				host.renderer.window().userScale(1.0f);
			});

			test("a View's viewport places it in its part of the window, and its box is what it sees", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto layer = root->make("layer");
				layer->make("eye")->attach<Camera>()->units(Camera::Units::PIXELS);
				auto view = layer->attach<View>();
				view->viewport(MV::BoxAABB<>(MV::point(0.5f, 0.5f), MV::point(1.0f, 1.0f)));
				auto thing = layer->make("thing");
				check(around(thing->screenFromLocal(MV::Point<>()), 640, 360), "viewport origin: " + text(thing->screenFromLocal(MV::Point<>())));
				thing->position(MV::point(640.0f, 360.0f));
				check(around(thing->screenFromLocal(MV::Point<>()), 1280, 720), "viewport corner: " + text(thing->screenFromLocal(MV::Point<>())));

				const auto area = view->bounds();
				check(std::abs(area.minPoint.x) < 0.5f && std::abs(area.minPoint.y) < 0.5f, "area starts off the origin");
				check(std::abs(area.maxPoint.x - 640.0f) < 0.5f && std::abs(area.maxPoint.y - 360.0f) < 0.5f, "area is not the viewport's size");
				const auto pixels = view->pixelRect();
				check_eq(640, pixels.minPoint.x);
				check_eq(720, pixels.maxPoint.y);
			});

			test("a perspective camera projects and picks through the node's plane", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto layer = root->make("layer");
				auto eye = layer->make("eye");
				eye->position(MV::point(0.0f, 0.0f, -500.0f));
				eye->attach<Camera>()->projection(Camera::Projection::PERSPECTIVE)->planes(1.0f, 5000.0f);
				layer->attach<View>();
				auto thing = layer->make("thing");
				check(around(thing->screenFromLocal(MV::Point<>()), 640, 360), "on the axis: " + text(thing->screenFromLocal(MV::Point<>())));

				thing->position(MV::point(100.0f, 100.0f));
				const auto onScreen = thing->screenFromLocal(MV::Point<>());
				check(onScreen.x > 640 && onScreen.y > 360, "right and down should stay right and down: " + text(onScreen));

				auto further = layer->make("further");
				further->position(MV::point(100.0f, 100.0f, 500.0f));
				const auto distant = further->screenFromLocal(MV::Point<>());
				check(distant.x > 640 && distant.x < onScreen.x, "further away should be nearer the middle: " + text(distant));

				const auto picked = thing->localFromScreen(onScreen);
				check(std::abs(picked.x) < 2.0f && std::abs(picked.y) < 2.0f, "picking missed: " + std::to_string(picked.x) + "," + std::to_string(picked.y));
			});

			test("a camera's mask draws only the groups it sees, each node by its own group", [] {
				SceneHost host;
				std::vector<DrawRecord> records;
				auto root = Node::make(host.renderer, "root");
				auto layer = root->make("layer");
				layer->make("eye")->attach<Camera>()->sees(1u << 0 | 1u << 2);
				layer->attach<View>();
				auto seen = layer->make("seen");
				seen->attach<DrawProbe>(&records);
				auto hidden = layer->make("hidden")->group(1);
				hidden->attach<DrawProbe>(&records);
				auto underHidden = hidden->make("underHidden")->group(2);
				underHidden->attach<DrawProbe>(&records);
				auto outside = root->make("outside")->group(1);
				outside->attach<DrawProbe>(&records);
				root->draw();

				std::vector<std::string> drawn;
				for (auto&& record : records) {
					drawn.push_back(record.node);
				}
				check_eq(size_t(3), drawn.size());
				check(std::find(drawn.begin(), drawn.end(), "hidden") == drawn.end(), "a group the camera does not see drew");
				check(std::find(drawn.begin(), drawn.end(), "underHidden") != drawn.end(), "a child in a seen group did not draw");
				check(std::find(drawn.begin(), drawn.end(), "outside") != drawn.end(), "a node outside the View was filtered");
			});

			test("a View's subject draws again through the View's slot", [] {
				SceneHost host;
				std::vector<DrawRecord> records;
				auto root = Node::make(host.renderer, "root");
				auto world = root->make("world");
				auto worldView = world->attach<View>();
				world->make("tree")->attach<DrawProbe>(&records);
				auto minimap = root->make("minimap");
				auto minimapView = minimap->attach<View>();
				minimapView->subject("world");
				root->draw();

				check_eq(size_t(2), records.size());
				check_eq(worldView->slot(), records[0].slot);
				check_eq(minimapView->slot(), records[1].slot);
			});

			test("a subject that holds its View is not drawn inside itself", [] {
				SceneHost host;
				std::vector<DrawRecord> records;
				auto root = Node::make(host.renderer, "root");
				root->attach<DrawProbe>(&records);
				auto layer = root->make("layer");
				layer->attach<View>()->subject("root");
				root->draw();
				check_eq(size_t(1), records.size());
			});

			test("two Views that show each other draw each other once", [] {
				SceneHost host;
				std::vector<DrawRecord> records;
				auto root = Node::make(host.renderer, "root");
				auto first = root->make("first");
				first->attach<View>()->subject("second");
				first->make("a")->attach<DrawProbe>(&records);
				auto second = root->make("second");
				second->attach<View>()->subject("first");
				second->make("b")->attach<DrawProbe>(&records);
				root->draw();
				check_eq(size_t(4), records.size());
			});

			test("a page anchored to a View saves the same after it loads under it", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto layer = root->make("layer");
				layer->attach<View>()->id("Interface");
				auto page = layer->make("page");
				auto panel = page->make("panel");
				auto sprite = panel->attach<MV::Scene::Sprite>();
				sprite->bounds(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::size(299.0f, 135.0f)));
				sprite->anchors().anchor(MV::BoxAABB<>(MV::point(0.5f, 0.5f), MV::point(0.5f, 0.5f))).parent(layer->component<View>().self(), true);
				panel->make("field")->position(MV::point(99.0f, 9.0f))->attach<MV::Scene::Sprite>()->bounds(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::size(200.0f, 36.0f)));

				ScratchScene firstFile;
				page->saveJai(firstFile.path, host.services, false);
				page->removeFromParent();
				auto loaded = layer->loadChild(firstFile.path, host.services, "page");
				root->update(0.0);
				ScratchScene secondFile;
				loaded->saveJai(secondFile.path, host.services, false);
				check(firstFile.contents() == secondFile.contents(), "the page drifted on a load and save under its View");
			});

			test("a View lays itself out in the renderer's target, as an editor's game view pushes it", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto layer = root->make("layer");
				layer->make("eye")->attach<Camera>();
				auto view = layer->attach<View>();
				auto thing = layer->make("thing");
				thing->position(MV::point(100.0f, 50.0f));
				host.renderer.pushTarget(MV::BoxAABB<int>(MV::Point<int>(640, 360), MV::Point<int>(1280, 720)));
				root->update(0.0);
				check(around(thing->screenFromLocal(MV::Point<>()), 740, 410), "inside the target: " + text(thing->screenFromLocal(MV::Point<>())));
				check_eq(640, view->pixelRect().minPoint.x);
				check_eq(720, view->pixelRect().maxPoint.y);
				host.renderer.popTarget();
				root->update(0.0);
				check(around(thing->screenFromLocal(MV::Point<>()), 100, 50), "the whole window again: " + text(thing->screenFromLocal(MV::Point<>())));
			});

			test("an authoring View keeps its slot to take part again", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto layer = root->make("layer");
				auto view = layer->attach<View>();
				const auto slot = view->slot();
				view->authoring(true);
				view->authoring(false);
				check_eq(slot, view->slot());
				check_eq(slot, layer->cameraId());
			});

			test("an authoring View takes no part", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto layer = root->make("layer");
				layer->make("eye")->attach<Camera>()->units(Camera::Units::PIXELS)->zoom(2.0f);
				auto view = layer->attach<View>();
				auto thing = layer->make("thing");
				thing->position(MV::point(100.0f, 50.0f));
				view->authoring(true);
				check_eq(int32_t(0), thing->cameraId());
				check(around(thing->screenFromLocal(MV::Point<>()), 100, 50), "authoring still zoomed: " + text(thing->screenFromLocal(MV::Point<>())));
				view->authoring(false);
				check_eq(view->slot(), thing->cameraId());
				check(around(thing->screenFromLocal(MV::Point<>()), 200, 100), "View back on: " + text(thing->screenFromLocal(MV::Point<>())));
			});

			test("View, Camera and group round-trip, and no camera id is written", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto layer = root->make("layer");
				auto eye = layer->make("eye");
				eye->attach<Camera>()->projection(Camera::Projection::PERSPECTIVE)->fieldOfView(45.0f)->sees(5u);
				auto view = layer->attach<View>();
				view->viewport(MV::BoxAABB<>(MV::point(0.25f, 0.0f), MV::point(1.0f, 0.5f)))->clear(View::Clear::DEPTH)->cameraNode("eye")->subject("other");
				layer->make("thing")->group(7);
				root->make("other");

				ScratchScene file;
				auto loaded = saveAndLoad(host, root, file);
				const auto json = file.contents();
				check(json.find("ourCameraId") == std::string::npos, "the run-time camera id was written");
				check(hasSerializedType(json, "View") && hasSerializedType(json, "Camera"), "View or Camera missing from the file");

				auto loadedView = loaded->get("layer")->component<View>();
				check_eq(0.25f, loadedView->viewport().minPoint.x);
				check_eq(0.5f, loadedView->viewport().maxPoint.y);
				check(loadedView->clear() == View::Clear::DEPTH, "clear lost");
				check_eq(std::string("eye"), loadedView->cameraNode());
				check_eq(std::string("other"), loadedView->subject());
				auto loadedCamera = loadedView->camera();
				check_not_null(loadedCamera, "camera not found after load");
				check(loadedCamera->projection() == Camera::Projection::PERSPECTIVE, "projection lost");
				check_eq(45.0f, loadedCamera->fieldOfView());
				check_eq(5u, loadedCamera->sees());
				check_eq(7u, loaded->get("thing")->group());
				check(loadedView->slot() > 0, "loaded View has no slot");
				check_eq(loadedView->slot(), loaded->get("thing")->cameraId());
			});
		}
	};

}

using view_suite = mv_tests::view_suite;
FOUNDRY_REGISTER(view_suite)
