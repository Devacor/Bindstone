// Keyboard and controller focus: TapDevice walks the focusable Clickables and Clickable reports taking and losing it.

#include <jaiscript/testing/foundry.hpp>
#include "mv_scene_host.hpp"
#include "MV/Render/Scene/clickable.h"
#include "MV/Render/Scene/button.h"

using namespace jai::foundry;
using MV::Scene::Clickable;
using MV::Scene::Node;

namespace mv_tests {

	class focus_suite : public suite {
	public:
		focus_suite() : suite("MV Focus") {}

		struct Target {
			std::shared_ptr<Clickable> clickable;
			int entered = 0;
			int left = 0;
			int accepted = 0;
			std::vector<jai::signal<Clickable::ButtonSignalSignature>::shared_receiver_type> receivers;
		};

		static std::shared_ptr<Target> addTarget(SceneHost& a_host, const std::shared_ptr<Node>& a_parent, const std::string& a_id, const MV::Point<>& a_at, bool a_focusable = true) {
			auto result = std::make_shared<Target>();
			auto node = a_parent->make(a_id);
			node->position(a_at);
			result->clickable = node->attach<Clickable>(a_host.mouse)->bounds(MV::size(100.0f, 50.0f));
			result->clickable->focusable(a_focusable);
			auto* counts = result.get();
			result->receivers.push_back(result->clickable->onFocusEnter.connect([counts](const std::shared_ptr<Clickable>&) { ++counts->entered; }));
			result->receivers.push_back(result->clickable->onFocusLeave.connect([counts](const std::shared_ptr<Clickable>&) { ++counts->left; }));
			result->receivers.push_back(result->clickable->onAccept.connect([counts](const std::shared_ptr<Clickable>&) { ++counts->accepted; }));
			return result;
		}

		void forge_tests() override {
			test("focus walks the focusable Clickables in scene order, wrapping at each end", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto first = addTarget(host, root, "first", MV::point(0.0f, 0.0f));
				auto skipped = addTarget(host, root, "skipped", MV::point(0.0f, 60.0f), false);
				auto second = addTarget(host, root, "second", MV::point(0.0f, 120.0f));
				auto third = addTarget(host, root, "third", MV::point(0.0f, 180.0f));
				check_false(host.mouse.hasFocus());
				check_true(host.mouse.focusNext(), "there is somewhere to go");
				check_true(first->clickable->focused() && first->entered == 1, "the first focusable takes it");
				host.mouse.focusNext();
				check_true(second->clickable->focused() && first->left == 1 && !first->clickable->focused(), "then the next, passing one that is not focusable");
				check_eq(0, skipped->entered);
				host.mouse.focusNext();
				host.mouse.focusNext();
				check_true(first->clickable->focused(), "past the last it wraps to the first");
				host.mouse.focusPrevious();
				check_true(third->clickable->focused(), "and back before the first wraps to the last");
			});

			test("focusOrder comes before place in the scene", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto first = addTarget(host, root, "first", MV::point(0.0f, 0.0f));
				auto second = addTarget(host, root, "second", MV::point(0.0f, 60.0f));
				auto third = addTarget(host, root, "third", MV::point(0.0f, 120.0f));
				third->clickable->focusOrder(-1);
				host.mouse.focusNext();
				check_true(third->clickable->focused(), "the lowest order is first");
				host.mouse.focusNext();
				check_true(first->clickable->focused());
			});

			test("a hidden or disabled Clickable is passed over, and a scope keeps the walk under one node", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto panel = root->make("panel");
				auto outside = addTarget(host, root, "outside", MV::point(300.0f, 0.0f));
				auto inside = addTarget(host, panel, "inside", MV::point(0.0f, 0.0f));
				auto hidden = addTarget(host, panel, "hidden", MV::point(0.0f, 60.0f));
				auto disabled = addTarget(host, panel, "disabled", MV::point(0.0f, 120.0f));
				auto last = addTarget(host, panel, "last", MV::point(0.0f, 180.0f));
				hidden->clickable->owner()->hide();
				disabled->clickable->disable();
				host.mouse.focusNext(panel.get());
				check_true(inside->clickable->focused());
				host.mouse.focusNext(panel.get());
				check_true(last->clickable->focused(), "the hidden and the disabled one are passed over");
				host.mouse.focusNext(panel.get());
				check_true(inside->clickable->focused(), "and the one outside the scope is never reached");
				check_eq(0, outside->entered);
			});

			test("focusMove goes to the nearest focusable that way on screen", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto topLeft = addTarget(host, root, "topLeft", MV::point(0.0f, 0.0f));
				auto topRight = addTarget(host, root, "topRight", MV::point(200.0f, 0.0f));
				auto bottomLeft = addTarget(host, root, "bottomLeft", MV::point(0.0f, 100.0f));
				auto bottomRight = addTarget(host, root, "bottomRight", MV::point(200.0f, 100.0f));
				topLeft->clickable->focus();
				check_true(host.mouse.focusMove(MV::FocusDirection::RIGHT) && topRight->clickable->focused(), "right along the row");
				check_true(host.mouse.focusMove(MV::FocusDirection::DOWN) && bottomRight->clickable->focused(), "down the column");
				check_true(host.mouse.focusMove(MV::FocusDirection::LEFT) && bottomLeft->clickable->focused(), "left along the row");
				check_true(host.mouse.focusMove(MV::FocusDirection::UP) && topLeft->clickable->focused(), "up the column");
				check_false(host.mouse.focusMove(MV::FocusDirection::LEFT), "nothing lies further left");
				check_true(topLeft->clickable->focused(), "so the focus stays");
			});

			test("a diagonal move takes what lies that way, and falls back to its sideways and up-or-down parts", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto topLeft = addTarget(host, root, "topLeft", MV::point(0.0f, 0.0f));
				auto topRight = addTarget(host, root, "topRight", MV::point(200.0f, 0.0f));
				auto bottomLeft = addTarget(host, root, "bottomLeft", MV::point(0.0f, 200.0f));
				auto bottomRight = addTarget(host, root, "bottomRight", MV::point(200.0f, 200.0f));
				bottomLeft->clickable->focus();
				check_true(host.mouse.focusMove(MV::FocusDirection::UP_RIGHT) && topRight->clickable->focused(), "up and right across the square");
				check_true(host.mouse.focusMove(MV::FocusDirection::DOWN_LEFT) && bottomLeft->clickable->focused(), "and back");
				topRight->clickable->owner()->hide();
				check_true(host.mouse.focusMove(MV::FocusDirection::UP_RIGHT) && bottomRight->clickable->focused(), "with nothing on the diagonal it goes sideways");
				bottomRight->clickable->owner()->hide();
				bottomLeft->clickable->focus();
				check_true(host.mouse.focusMove(MV::FocusDirection::UP_RIGHT) && topLeft->clickable->focused(), "and with nothing sideways either, up");
				check_false(host.mouse.focusMove(MV::FocusDirection::UP_LEFT), "a diagonal with nothing in any of its parts stays put");
				check_true(topLeft->clickable->focused());
			});

			test("a row laid out for four directions answers the diagonals too", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto left = addTarget(host, root, "left", MV::point(0.0f, 0.0f));
				auto middle = addTarget(host, root, "middle", MV::point(150.0f, 0.0f));
				auto right = addTarget(host, root, "right", MV::point(300.0f, 0.0f));
				left->clickable->focus();
				check_true(host.mouse.focusMove(MV::FocusDirection::DOWN_RIGHT) && middle->clickable->focused());
				check_true(host.mouse.focusMove(MV::FocusDirection::UP_RIGHT) && right->clickable->focused());
				check_true(host.mouse.focusMove(MV::FocusDirection::DOWN_LEFT) && middle->clickable->focused());
			});

			test("a named neighbour is followed before order or position, and one that cannot take the focus is ignored", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto first = addTarget(host, root, "first", MV::point(0.0f, 0.0f));
				auto second = addTarget(host, root, "second", MV::point(0.0f, 60.0f));
				auto third = addTarget(host, root, "third", MV::point(0.0f, 120.0f));
				auto distant = addTarget(host, root, "far", MV::point(600.0f, 300.0f));
				first->clickable->focusLink(MV::FocusLink::NEXT, "third");
				first->clickable->focusLink(MV::FocusLink::DOWN, "far");
				first->clickable->focusLink("upRight", "far");
				check_eq(std::string("third"), first->clickable->focusLink(MV::FocusLink::NEXT));
				first->clickable->focus();
				check_true(host.mouse.focusNext() && third->clickable->focused(), "next goes where it is told, past the one in between");
				first->clickable->focus();
				check_true(host.mouse.focusMove(MV::FocusDirection::DOWN) && distant->clickable->focused(), "down goes where it is told, not to the nearest below");
				first->clickable->focus();
				check_true(host.mouse.focusMove(MV::FocusDirection::UP_RIGHT) && distant->clickable->focused(), "a diagonal can be named as well");
				distant->clickable->owner()->hide();
				first->clickable->focus();
				check_true(host.mouse.focusMove(MV::FocusDirection::DOWN) && second->clickable->focused(), "a neighbour that is hidden is passed over for the nearest below");
				first->clickable->focusLink(MV::FocusLink::NEXT, "");
				first->clickable->focus();
				check_true(host.mouse.focusNext() && second->clickable->focused(), "an emptied link goes back to the order");
			});

			test("a link named at one end is followed back from the other, unless that end names its own", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto start = addTarget(host, root, "start", MV::point(0.0f, 0.0f));
				auto close = addTarget(host, root, "near", MV::point(0.0f, 60.0f));
				auto distant = addTarget(host, root, "far", MV::point(600.0f, 300.0f));
				auto elsewhere = addTarget(host, root, "elsewhere", MV::point(300.0f, 0.0f));
				start->clickable->focusLink(MV::FocusLink::DOWN, "far");
				start->clickable->focusLink(MV::FocusLink::NEXT, "far");
				distant->clickable->focus();
				check_true(host.mouse.focusMove(MV::FocusDirection::UP) && start->clickable->focused(), "up from the far one returns along the link, though others are nearer above it");
				distant->clickable->focus();
				check_true(host.mouse.focusPrevious() && start->clickable->focused(), "previous follows a next back the same way");
				distant->clickable->focusLink(MV::FocusLink::UP, "elsewhere");
				distant->clickable->focus();
				check_true(host.mouse.focusMove(MV::FocusDirection::UP) && elsewhere->clickable->focused(), "its own link wins over the way back");
				check_eq(0, close->entered, "and the one in between was never part of it");
			});

			test("a Button shows its highlight view while it is focused or under the pointer, and its idle view after", [] {
				SceneHost host;
				host.mouse.trackHover(true);
				auto root = Node::make(host.renderer, "root");
				auto buttonNode = root->make("button");
				auto active = buttonNode->make("active");
				auto idle = buttonNode->make("idle");
				auto disabled = buttonNode->make("disabled");
				auto highlight = buttonNode->make("highlight");
				auto button = buttonNode->attach<MV::Scene::Button>(host.mouse)->bounds(MV::size(100.0f, 50.0f));
				button->activeNode(active)->idleNode(idle)->disabledNode(disabled)->highlightNode(highlight);
				auto other = addTarget(host, root, "other", MV::point(0.0f, 200.0f));
				auto shows = [&](const std::shared_ptr<Node>& a_view) {
					int shown = 0;
					for (auto&& view : { active, idle, disabled, highlight }) {
						shown += view->visible() ? 1 : 0;
					}
					return shown == 1 && a_view->visible();
				};
				auto pointer = [&](const MV::Point<>& a_local, bool a_pressed) {
					host.mouse.simulate(buttonNode->screenFromLocal(a_local), a_pressed);
					host.mouse.update();
				};
				pointer(MV::point(500.0f, 500.0f), false);
				check_true(shows(idle) && !button->highlighted(), "at rest");

				button->focus();
				check_true(shows(highlight) && button->highlighted(), "focused");
				pointer(MV::point(10.0f, 10.0f), false);
				check_true(shows(highlight) && button->hovered(), "and with the pointer over it as well");
				other->clickable->focus();
				check_true(shows(highlight), "the focus going leaves it highlighted while the pointer stays");
				pointer(MV::point(10.0f, 10.0f), true);
				check_true(shows(active), "pressed");
				pointer(MV::point(10.0f, 10.0f), false);
				check_true(shows(highlight), "let go with the pointer still over it");
				button->disable();
				check_true(shows(disabled), "disabled");
				button->clickDetectionType(Clickable::BoundsType::LOCAL);
				check_true(shows(highlight), "enabled again under the pointer");
				other->clickable->focus();
				pointer(MV::point(500.0f, 500.0f), false);
				check_true(shows(idle) && !button->highlighted(), "the pointer and the focus both gone");

				button->highlightNode(nullptr);
				button->focus();
				check_true(idle->visible() && !active->visible() && !disabled->visible() && button->highlighted(), "a button without a highlight view keeps its idle view");
			});

			test("a move into a row or a column lands on the end nearest where it came from", [] {
				SceneHost host;
				auto& mouse = host.mouse;
				auto root = Node::make(host.renderer, "root");
				// [before] [0 1 2 3] [after], all in a line
				auto before = addTarget(host, root, "before", MV::point(0.0f, 0.0f));
				std::vector<std::shared_ptr<Target>> row;
				for (int index = 0; index < 4; ++index) {
					row.push_back(addTarget(host, root, "row" + std::to_string(index), MV::point(150.0f + index * 150.0f, 0.0f)));
				}
				auto after = addTarget(host, root, "after", MV::point(750.0f, 0.0f));
				before->clickable->focus();
				check_true(mouse.focusMove(MV::FocusDirection::RIGHT) && row[0]->clickable->focused(), "from before, the first");
				after->clickable->focus();
				check_true(mouse.focusMove(MV::FocusDirection::LEFT) && row[3]->clickable->focused(), "from after, the last");

				// [left] then a column of four beside it, then [right], tops in a line
				SceneHost columnHost;
				auto columnRoot = Node::make(columnHost.renderer, "root");
				auto left = addTarget(columnHost, columnRoot, "left", MV::point(0.0f, 0.0f));
				std::vector<std::shared_ptr<Target>> column;
				for (int index = 0; index < 4; ++index) {
					column.push_back(addTarget(columnHost, columnRoot, "column" + std::to_string(index), MV::point(150.0f, index * 60.0f)));
				}
				auto right = addTarget(columnHost, columnRoot, "right", MV::point(300.0f, 0.0f));
				auto below = addTarget(columnHost, columnRoot, "below", MV::point(150.0f, 300.0f));
				right->clickable->focus();
				check_true(columnHost.mouse.focusMove(MV::FocusDirection::LEFT) && column[0]->clickable->focused(), "into a column from the side, the one level with where it came from");
				left->clickable->focus();
				check_true(columnHost.mouse.focusMove(MV::FocusDirection::RIGHT) && column[0]->clickable->focused());
				below->clickable->focus();
				check_true(columnHost.mouse.focusMove(MV::FocusDirection::UP) && column[3]->clickable->focused(), "into a column from its foot, the last");
			});

			test("a wide control is as near as its edge, not its middle", [] {
				SceneHost host;
				auto& mouse = host.mouse;
				auto root = Node::make(host.renderer, "root");
				auto corner = addTarget(host, root, "corner", MV::point(0.0f, 0.0f));
				auto bar = addTarget(host, root, "bar", MV::point(0.0f, 100.0f));
				bar->clickable->bounds(MV::size(800.0f, 50.0f));
				auto lowest = addTarget(host, root, "lowest", MV::point(0.0f, 400.0f));
				auto beside = addTarget(host, root, "beside", MV::point(900.0f, 100.0f));
				corner->clickable->focus();
				check_true(mouse.focusMove(MV::FocusDirection::DOWN) && bar->clickable->focused(), "a bar squarely below is reached though its middle is far to one side");
				check_true(mouse.focusMove(MV::FocusDirection::UP) && corner->clickable->focused(), "and the way back from it finds what sits over its end");
				bar->clickable->focus();
				check_true(mouse.focusMove(MV::FocusDirection::RIGHT) && beside->clickable->focused(), "from the bar, what is past its end is next, measured from that end");
				check_true(mouse.focusMove(MV::FocusDirection::LEFT) && bar->clickable->focused());
				check_true(mouse.focusMove(MV::FocusDirection::DOWN) && lowest->clickable->focused());
			});

			test("focusNearest takes the focusable at a point, or the closest one to it", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto left = addTarget(host, root, "left", MV::point(0.0f, 0.0f));
				auto right = addTarget(host, root, "right", MV::point(400.0f, 0.0f));
				auto unfocusable = addTarget(host, root, "unfocusable", MV::point(200.0f, 0.0f), false);
				MV::Point<int> centre;
				check_false(host.mouse.focusedScreenCentre(centre), "nothing is focused yet");
				right->clickable->focus();
				check_true(host.mouse.focusedScreenCentre(centre));
				host.mouse.focusClear();
				check_true(host.mouse.focusNearest(centre) && right->clickable->focused(), "the one the point is on");
				auto between = unfocusable->clickable->owner()->screenFromLocal(MV::point(10.0f, 25.0f));
				check_true(host.mouse.focusNearest(between) && left->clickable->focused(), "off every focusable, the nearest by its edge");
				check_eq(0, unfocusable->entered);
			});

			test("focusActivate presses the focused Clickable, and pressOnFocus presses as the focus arrives", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto button = addTarget(host, root, "button", MV::point(0.0f, 0.0f));
				auto field = addTarget(host, root, "field", MV::point(0.0f, 60.0f));
				field->clickable->pressOnFocus(true);
				check_false(host.mouse.focusActivate(), "nothing is focused yet");
				host.mouse.focusNext();
				check_eq(0, button->accepted, "taking the focus does not press an ordinary Clickable");
				check_true(host.mouse.focusActivate());
				check_eq(1, button->accepted);
				host.mouse.focusNext();
				check_eq(1, field->accepted, "one that presses on focus is pressed as it takes it");
			});

			test("a pointer press on a focusable Clickable gives it the focus without a second press", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto first = addTarget(host, root, "first", MV::point(0.0f, 0.0f));
				auto field = addTarget(host, root, "field", MV::point(0.0f, 60.0f));
				field->clickable->pressOnFocus(true);
				first->clickable->focus();
				auto at = field->clickable->owner()->screenFromLocal(MV::point(10.0f, 10.0f));
				host.mouse.simulate(at, false);
				host.mouse.update();
				host.mouse.simulate(at, true);
				host.mouse.update();
				host.mouse.simulate(at, false);
				host.mouse.update();
				check_true(field->clickable->focused() && first->left == 1, "the press moves the focus");
				check_eq(1, field->accepted, "and it is accepted once");
			});

			test("a Button is focusable from the start, and clearing the focus leaves it", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto button = root->make("button")->attach<MV::Scene::Button>(host.mouse);
				check_true(button->focusable());
				check_true(host.mouse.focusNext() && button->focused());
				host.mouse.focusClear();
				check_false(button->focused());
				check_false(host.mouse.hasFocus());
			});

			test("a focused Clickable that is destroyed is forgotten", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto doomed = addTarget(host, root, "doomed", MV::point(0.0f, 0.0f));
				auto other = addTarget(host, root, "other", MV::point(0.0f, 60.0f));
				doomed->clickable->focus();
				root->remove("doomed");
				doomed->receivers.clear();
				doomed->clickable.reset();
				check_false(host.mouse.hasFocus());
				check_true(host.mouse.focusNext() && other->clickable->focused());
			});
		}
	};

}

using focus_suite = mv_tests::focus_suite;
FOUNDRY_REGISTER(focus_suite)
