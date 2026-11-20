// Node bounds: a node's box takes in its children where their transforms put them, and follows them as they change.

#include <jaiscript/testing/foundry.hpp>
#include <cmath>
#include "mv_scene_host.hpp"
#include "MV/Render/Scene/sprite.h"
#include "MV/Render/Scene/slider.h"

using namespace jai::foundry;
using MV::Scene::Node;

namespace mv_tests {

	class bounds_suite : public suite {
	public:
		bounds_suite() : suite("MV Bounds") {}

		static std::shared_ptr<Node> addBox(const std::shared_ptr<Node>& a_parent, const std::string& a_id, const MV::Point<>& a_at, const MV::Size<>& a_size) {
			auto node = a_parent->make(a_id);
			node->position(a_at);
			node->attach<MV::Scene::Sprite>()->bounds(a_size);
			return node;
		}

		static bool boxIs(const MV::BoxAABB<>& a_box, float a_left, float a_top, float a_right, float a_bottom) {
			auto same = [](float a_value, float a_wanted) { return std::abs(a_value - a_wanted) < 0.01f; };
			return same(a_box.minPoint.x, a_left) && same(a_box.minPoint.y, a_top) && same(a_box.maxPoint.x, a_right) && same(a_box.maxPoint.y, a_bottom);
		}

		static std::string shown(const MV::BoxAABB<>& a_box) {
			return std::to_string(a_box.minPoint.x) + "," + std::to_string(a_box.minPoint.y) + " - " + std::to_string(a_box.maxPoint.x) + "," + std::to_string(a_box.maxPoint.y);
		}

		void forge_tests() override {
			test("a node's bounds take in each child where its position puts it", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto parent = root->make("parent");
				auto first = addBox(parent, "first", MV::point(300.0f, 0.0f), MV::size(100.0f, 50.0f));
				auto second = addBox(parent, "second", MV::point(300.0f, 100.0f), MV::size(100.0f, 50.0f));
				check_true(boxIs(parent->bounds(), 300.0f, 0.0f, 400.0f, 150.0f), shown(parent->bounds()));
				check_true(boxIs(parent->childBounds(), 300.0f, 0.0f, 400.0f, 150.0f), shown(parent->childBounds()));
				check_true(parent->bounds(false).empty(), "with no components of its own it has no box of its own");
				check_true(boxIs(first->bounds(), 0.0f, 0.0f, 100.0f, 50.0f), "a child's own box stays in its own space");
			});

			test("its own components and its children make one box", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto parent = addBox(root, "parent", MV::point(50.0f, 50.0f), MV::size(40.0f, 40.0f));
				addBox(parent, "child", MV::point(-20.0f, 60.0f), MV::size(30.0f, 30.0f));
				check_true(boxIs(parent->bounds(), -20.0f, 0.0f, 40.0f, 90.0f), shown(parent->bounds()));
				check_true(boxIs(parent->bounds(false), 0.0f, 0.0f, 40.0f, 40.0f));
				check_true(boxIs(root->bounds(), 30.0f, 50.0f, 90.0f, 140.0f), "and a grandparent takes that in where the parent sits: " + shown(root->bounds()));
			});

			test("the bounds follow a child that moves, scales, turns, hides, arrives or leaves", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto parent = root->make("parent");
				auto child = addBox(parent, "child", MV::point(0.0f, 0.0f), MV::size(100.0f, 50.0f));
				check_true(boxIs(parent->bounds(), 0.0f, 0.0f, 100.0f, 50.0f));
				check_true(boxIs(root->bounds(), 0.0f, 0.0f, 100.0f, 50.0f));

				child->position(MV::point(200.0f, 10.0f));
				check_true(boxIs(parent->bounds(), 200.0f, 10.0f, 300.0f, 60.0f), "moved: " + shown(parent->bounds()));
				check_true(boxIs(root->bounds(), 200.0f, 10.0f, 300.0f, 60.0f), "and every node above it follows: " + shown(root->bounds()));

				child->scale(MV::Scale(2.0f, 2.0f, 1.0f));
				check_true(boxIs(parent->bounds(), 200.0f, 10.0f, 400.0f, 110.0f), "scaled: " + shown(parent->bounds()));
				child->scale(MV::Scale(1.0f, 1.0f, 1.0f));

				child->rotation(MV::AxisAngles(0.0f, 0.0f, 90.0f));
				auto turned = parent->bounds();
				check_true(std::abs(turned.width() - 50.0f) < 0.01f && std::abs(turned.height() - 100.0f) < 0.01f, "turned a quarter, its box is as tall as it was wide: " + shown(turned));
				child->rotation(MV::AxisAngles(0.0f, 0.0f, 0.0f));

				auto other = addBox(parent, "other", MV::point(0.0f, 0.0f), MV::size(20.0f, 20.0f));
				check_true(boxIs(parent->bounds(), 0.0f, 0.0f, 300.0f, 60.0f), "a child arriving: " + shown(parent->bounds()));
				other->hide();
				check_true(boxIs(parent->bounds(), 200.0f, 10.0f, 300.0f, 60.0f), "hidden, it is left out: " + shown(parent->bounds()));
				other->show();
				check_true(boxIs(parent->bounds(), 0.0f, 0.0f, 300.0f, 60.0f));
				parent->remove(other);
				check_true(boxIs(parent->bounds(), 200.0f, 10.0f, 300.0f, 60.0f), "and gone when it leaves: " + shown(parent->bounds()));
				check_true(boxIs(root->bounds(), 200.0f, 10.0f, 300.0f, 60.0f));
			});

			test("a node's place on screen takes in its children", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto parent = root->make("parent");
				auto child = addBox(parent, "child", MV::point(300.0f, 100.0f), MV::size(100.0f, 50.0f));
				auto parentBox = parent->screenBounds();
				auto childBox = child->screenBounds();
				check_true(parentBox.minPoint == childBox.minPoint && parentBox.maxPoint == childBox.maxPoint, "a parent holding one child is on screen where the child is");
			});

			test("a square turned 45 degrees is 141.4 across in world space and on screen", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto square = addBox(root, "square", MV::point(400.0f, 300.0f), MV::size(100.0f, 100.0f));
				square->rotation(MV::AxisAngles(0.0f, 0.0f, 45.0f));
				auto world = square->worldFromLocal(square->bounds(false));
				check_true(std::abs(world.width() - 141.42f) < 0.05f && std::abs(world.height() - 141.42f) < 0.05f, shown(world));
				auto center = square->worldFromLocal(MV::point(50.0f, 50.0f));
				check_true(std::abs(world.centerPoint().x - center.x) < 0.05f && std::abs(world.centerPoint().y - center.y) < 0.05f, "centred on the square's centre: " + shown(world));
				check_true(boxIs(square->worldBounds(false), world.minPoint.x, world.minPoint.y, world.maxPoint.x, world.maxPoint.y), "worldBounds is the same box");

				auto screen = square->screenFromLocal(square->bounds(false));
				check_true(std::abs(screen.width() - 141) <= 1 && std::abs(screen.height() - 141) <= 1, "and on screen: " + std::to_string(screen.width()) + " x " + std::to_string(screen.height()));
			});

			test("scale then turn: a 2 x 1 scaled square turned 30 degrees takes in every corner", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto box = addBox(root, "box", MV::point(500.0f, 300.0f), MV::size(100.0f, 100.0f));
				box->scale(MV::Scale(2.0f, 1.0f, 1.0f));
				box->rotation(MV::AxisAngles(0.0f, 0.0f, 30.0f));
				auto world = box->worldBounds(false);
				auto corners = box->worldFromLocal(std::vector<MV::Point<>>{ MV::point(0.0f, 0.0f), MV::point(100.0f, 0.0f), MV::point(0.0f, 100.0f), MV::point(100.0f, 100.0f) });
				MV::BoxAABB<> expected(corners[0]);
				for (auto&& corner : corners) {
					expected.expandWith(corner);
				}
				check_true(boxIs(world, expected.minPoint.x, expected.minPoint.y, expected.maxPoint.x, expected.maxPoint.y), shown(world) + " around its corners " + shown(expected));
				check_true(world.width() > 200.0f && world.height() > 100.0f, "wider and taller than the unturned box: " + shown(world));
			});

			test("a turned child of a turned parent adds both turns", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto parent = root->make("parent")->position(MV::point(200.0f, 100.0f));
				parent->rotation(MV::AxisAngles(0.0f, 0.0f, 45.0f));
				auto child = addBox(parent, "child", MV::point(50.0f, 0.0f), MV::size(100.0f, 50.0f));
				child->rotation(MV::AxisAngles(0.0f, 0.0f, 45.0f));
				auto world = child->worldBounds(false);
				check_true(std::abs(world.width() - 50.0f) < 0.05f && std::abs(world.height() - 100.0f) < 0.05f, "a quarter turn in all, as tall as it was wide: " + shown(world));
				auto corners = child->worldFromLocal(std::vector<MV::Point<>>{ MV::point(0.0f, 0.0f), MV::point(100.0f, 0.0f), MV::point(0.0f, 50.0f), MV::point(100.0f, 50.0f) });
				MV::BoxAABB<> expected(corners[0]);
				for (auto&& corner : corners) {
					expected.expandWith(corner);
				}
				check_true(boxIs(world, expected.minPoint.x, expected.minPoint.y, expected.maxPoint.x, expected.maxPoint.y), shown(world) + " around its corners " + shown(expected));

				auto parentWorld = parent->worldBounds();
				check_true(parentWorld.contains(world), "the parent's world box holds its child's: " + shown(parentWorld));
			});

			test("a flipped node's box still runs from its least to its greatest corner", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto flipped = addBox(root, "flipped", MV::point(300.0f, 200.0f), MV::size(100.0f, 50.0f));
				flipped->scale(MV::Scale(-1.0f, 1.0f, 1.0f));
				auto world = flipped->worldBounds(false);
				check_true(boxIs(world, 200.0f, 200.0f, 300.0f, 250.0f), shown(world));
				auto screen = flipped->screenBounds(false);
				check_true(screen.minPoint.x < screen.maxPoint.x && screen.width() == 100, "on screen too");
			});

			test("world and screen boxes map back to the local box around them", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto square = addBox(root, "square", MV::point(400.0f, 300.0f), MV::size(100.0f, 100.0f));
				auto local = square->bounds(false);
				check_true(boxIs(square->localFromWorld(square->worldFromLocal(local)), 0.0f, 0.0f, 100.0f, 100.0f), "unturned, the round trip is the box itself");

				square->rotation(MV::AxisAngles(0.0f, 0.0f, 45.0f));
				// the world box is a 141.4 square; turned back 45 degrees it spans 200 around the square's centre
				auto back = square->localFromWorld(square->worldFromLocal(local));
				check_true(boxIs(back, -50.0f, -50.0f, 150.0f, 150.0f), "from world: " + shown(back));
				auto backFromScreen = square->localFromScreen(square->screenFromLocal(local));
				auto nearTo = [](float a_value, float a_wanted) { return std::abs(a_value - a_wanted) < 1.5f; };
				check_true(nearTo(backFromScreen.minPoint.x, -50.0f) && nearTo(backFromScreen.minPoint.y, -50.0f) && nearTo(backFromScreen.maxPoint.x, 150.0f) && nearTo(backFromScreen.maxPoint.y, 150.0f), "from screen: " + shown(backFromScreen));

				auto worldBox = MV::BoxAABB<>(MV::point(400.0f, 300.0f), MV::point(500.0f, 400.0f));
				auto inLocal = square->localFromWorld(worldBox);
				check_true(std::abs(inLocal.width() - 141.42f) < 0.05f && std::abs(inLocal.height() - 141.42f) < 0.05f, "a world square seen from a turned node: " + shown(inLocal));
			});

			test("a slider's track is its own box, whatever its handle does", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto track = root->make("track");
				auto slider = track->attach<MV::Scene::Slider>(host.mouse)->bounds(MV::size(200.0f, 20.0f));
				auto handle = addBox(track, "handle", MV::point(0.0f, 0.0f), MV::size(30.0f, 30.0f));
				slider->handle(handle);

				slider->percent(0.0f);
				check_true(std::abs(handle->position().x) < 0.01f && std::abs(handle->position().y + 5.0f) < 0.01f, "at nothing the handle is at the start, centred across the track");
				slider->percent(1.0f);
				check_true(std::abs(handle->position().x - 170.0f) < 0.01f, "at full its far edge meets the track's: " + std::to_string(handle->position().x));
				slider->percent(0.5f);
				check_true(std::abs(handle->position().x - 85.0f) < 0.01f, "and halfway is halfway, not drifting as the handle carries the bounds with it: " + std::to_string(handle->position().x));
				slider->percent(1.0f);
				slider->percent(0.5f);
				check_true(std::abs(handle->position().x - 85.0f) < 0.01f, "from either side");
				check_true(boxIs(track->bounds(false), 0.0f, 0.0f, 200.0f, 20.0f));
			});
		}
	};

}

using mv_tests_bounds_suite = mv_tests::bounds_suite;
FOUNDRY_REGISTER(mv_tests_bounds_suite)
