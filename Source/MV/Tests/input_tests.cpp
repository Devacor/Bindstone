// Pointer hover: TapDevice settles which Clickable is under the pointer and Clickable reports enter and leave.

#include <jaiscript/testing/foundry.hpp>
#include "mv_scene_host.hpp"
#include "MV/Render/Scene/clickable.h"

using namespace jai::foundry;
using MV::Scene::Clickable;
using MV::Scene::Node;

namespace mv_tests {

	class hover_suite : public suite {
	public:
		hover_suite() : suite("MV Hover") {}

		struct Target {
			std::shared_ptr<Clickable> clickable;
			int entered = 0;
			int left = 0;
			std::vector<jai::signal<Clickable::ButtonSignalSignature>::shared_receiver_type> receivers;
		};

		static std::shared_ptr<Target> addTarget(SceneHost& a_host, const std::shared_ptr<Node>& a_parent, const std::string& a_id, const MV::Point<>& a_at) {
			auto result = std::make_shared<Target>();
			auto node = a_parent->make(a_id);
			node->position(a_at);
			result->clickable = node->attach<Clickable>(a_host.mouse)->bounds(MV::size(100.0f, 50.0f));
			auto* counts = result.get();
			result->receivers.push_back(result->clickable->onHoverEnter.connect([counts](const std::shared_ptr<Clickable>&) { ++counts->entered; }));
			result->receivers.push_back(result->clickable->onHoverLeave.connect([counts](const std::shared_ptr<Clickable>&) { ++counts->left; }));
			return result;
		}

		static void pointAt(SceneHost& a_host, const std::shared_ptr<Node>& a_node, const MV::Point<>& a_local, bool a_pressed = false) {
			a_host.mouse.simulate(a_node->screenFromLocal(a_local), a_pressed);
			a_host.mouse.update();
		}

		void forge_tests() override {
			test("hover is not tracked until the device is asked to", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto target = addTarget(host, root, "a", MV::point(0.0f, 0.0f));
				pointAt(host, target->clickable->owner(), MV::point(10.0f, 10.0f));
				check_eq(0, target->entered);
				check_false(target->clickable->hovered());
				host.mouse.trackHover(true);
				host.mouse.update();
				check_eq(1, target->entered, "tracking starts with the pointer where it is");
				check_true(target->clickable->hovered());
			});

			test("the pointer coming over and leaving fires enter and leave once each", [] {
				SceneHost host;
				host.mouse.trackHover(true);
				auto root = Node::make(host.renderer, "root");
				auto target = addTarget(host, root, "a", MV::point(0.0f, 0.0f));
				auto node = target->clickable->owner();
				pointAt(host, node, MV::point(300.0f, 300.0f));
				check_eq(0, target->entered);
				pointAt(host, node, MV::point(10.0f, 10.0f));
				pointAt(host, node, MV::point(20.0f, 12.0f));
				check_eq(1, target->entered, "moving within the bounds enters once");
				check_eq(0, target->left);
				pointAt(host, node, MV::point(300.0f, 300.0f));
				check_eq(1, target->left);
				check_false(target->clickable->hovered());
			});

			test("only the top-most of two overlapping Clickables is hovered", [] {
				SceneHost host;
				host.mouse.trackHover(true);
				auto root = Node::make(host.renderer, "root");
				auto under = addTarget(host, root, "under", MV::point(0.0f, 0.0f));
				auto over = addTarget(host, root, "over", MV::point(50.0f, 0.0f));
				pointAt(host, root, MV::point(75.0f, 10.0f));
				check_eq(1, over->entered, "the later sibling is on top");
				check_eq(0, under->entered, "the one beneath is covered");
				pointAt(host, root, MV::point(25.0f, 10.0f));
				check_eq(1, over->left);
				check_eq(1, under->entered, "the one beneath is hovered where it shows");
			});

			test("a Clickable that lets touches through shares the hover with the one beneath", [] {
				SceneHost host;
				host.mouse.trackHover(true);
				auto root = Node::make(host.renderer, "root");
				auto under = addTarget(host, root, "under", MV::point(0.0f, 0.0f));
				auto over = addTarget(host, root, "over", MV::point(0.0f, 0.0f));
				over->clickable->stopEatingTouches();
				pointAt(host, root, MV::point(10.0f, 10.0f));
				check_eq(1, over->entered);
				check_eq(1, under->entered);
			});

			test("hover holds through a press and settles on release", [] {
				SceneHost host;
				host.mouse.trackHover(true);
				auto root = Node::make(host.renderer, "root");
				auto first = addTarget(host, root, "first", MV::point(0.0f, 0.0f));
				auto second = addTarget(host, root, "second", MV::point(200.0f, 0.0f));
				pointAt(host, root, MV::point(10.0f, 10.0f));
				pointAt(host, root, MV::point(10.0f, 10.0f), true);
				pointAt(host, root, MV::point(210.0f, 10.0f), true);
				check_eq(0, first->left, "a drag off the pressed Clickable keeps its hover");
				check_eq(0, second->entered);
				pointAt(host, root, MV::point(210.0f, 10.0f), false);
				check_eq(1, first->left);
				check_eq(1, second->entered, "the release settles hover where the pointer is");
			});

			test("refreshHover asks again when the scene moved under a still pointer", [] {
				SceneHost host;
				host.mouse.trackHover(true);
				auto root = Node::make(host.renderer, "root");
				auto target = addTarget(host, root, "a", MV::point(0.0f, 0.0f));
				pointAt(host, root, MV::point(10.0f, 10.0f));
				check_eq(1, target->entered);
				target->clickable->owner()->position(MV::point(400.0f, 0.0f));
				host.mouse.update();
				check_eq(0, target->left, "a still pointer is not asked about again");
				host.mouse.refreshHover();
				host.mouse.update();
				check_eq(1, target->left);
			});

			test("a hovered Clickable that is destroyed is forgotten", [] {
				SceneHost host;
				host.mouse.trackHover(true);
				auto root = Node::make(host.renderer, "root");
				auto doomed = addTarget(host, root, "doomed", MV::point(0.0f, 0.0f));
				auto under = addTarget(host, root, "under", MV::point(0.0f, 0.0f));
				under->clickable->owner()->depth(-1.0f);
				pointAt(host, root, MV::point(10.0f, 10.0f));
				check_eq(1, doomed->entered);
				check_eq(0, under->entered);
				root->remove("doomed");
				doomed->receivers.clear();
				doomed->clickable.reset();
				host.mouse.refreshHover();
				host.mouse.update();
				check_eq(1, under->entered, "the one beneath takes the hover");
			});

			test("turning tracking off leaves whatever was hovered", [] {
				SceneHost host;
				host.mouse.trackHover(true);
				auto root = Node::make(host.renderer, "root");
				auto target = addTarget(host, root, "a", MV::point(0.0f, 0.0f));
				pointAt(host, root, MV::point(10.0f, 10.0f));
				host.mouse.trackHover(false);
				check_eq(1, target->left);
				check_false(target->clickable->hovered());
			});
		}
	};

}

using hover_suite = mv_tests::hover_suite;
FOUNDRY_REGISTER(hover_suite)
