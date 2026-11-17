// A Scroller's content, the focus scrolling it to what it lands on, and what a clip leaves showing of a node.

#include <jaiscript/testing/foundry.hpp>
#include <cmath>
#include "mv_scene_host.hpp"
#include "MV/Render/Scene/clickable.h"
#include "MV/Render/Scene/focusGroup.h"
#include "MV/Render/Scene/scroller.h"
#include "MV/Render/Scene/stencil.h"

using namespace jai::foundry;
using MV::FocusDirection;
using MV::Scene::Clickable;
using MV::Scene::Node;

namespace mv_tests {

	class scroller_suite : public suite {
	public:
		scroller_suite() : suite("MV Scroller") {}

		static std::shared_ptr<Clickable> addItem(SceneHost& a_host, const std::shared_ptr<Node>& a_parent, const std::string& a_id, const MV::Point<>& a_at) {
			auto node = a_parent->make(a_id);
			node->position(a_at);
			auto result = node->attach<Clickable>(a_host.mouse)->bounds(MV::size(200.0f, 50.0f));
			result->focusable(true);
			return result;
		}

		// A list 200 by 100 at 100,100 that clips its rows, each 50 tall, and a control under it at 100,220.
		struct List {
			SceneHost host;
			std::shared_ptr<Node> root;
			std::shared_ptr<Node> list;
			std::shared_ptr<Node> content;
			std::shared_ptr<MV::Scene::Scroller> scroller;
			std::vector<std::shared_ptr<Clickable>> rows;
			std::shared_ptr<Clickable> under;

			explicit List(int a_rows = 6) {
				root = Node::make(host.renderer, "root");
				list = root->make("list");
				list->position(MV::point(100.0f, 100.0f));
				content = list->make("content");
				scroller = list->attach<MV::Scene::Scroller>(host.mouse)->bounds(MV::size(200.0f, 100.0f));
				scroller->content(content);
				list->attach<MV::Scene::Stencil>()->bounds(MV::size(200.0f, 100.0f));
				for (int i = 0; i < a_rows; ++i) {
					rows.push_back(addItem(host, content, "row" + std::to_string(i), MV::point(0.0f, 50.0f * static_cast<float>(i))));
				}
				under = addItem(host, root, "under", MV::point(100.0f, 220.0f));
			}

			float scrolled() const {
				return content->position().y;
			}

			bool scrolledTo(float a_wanted) const {
				return std::abs(scrolled() - a_wanted) < 0.01f && std::abs(content->position().x) < 0.01f;
			}

			MV::Point<int> onScreen(float a_x, float a_y) {
				return list->screenFromLocal(MV::point(a_x, a_y));
			}

			void point(const MV::Point<int>& a_at, bool a_pressed) {
				host.mouse.simulate(a_at, a_pressed);
				host.mouse.update();
			}

			// press, drag by a number of pixels down the screen, let go
			void drag(int a_pixels) {
				auto at = onScreen(100.0f, 60.0f);
				point(at, false);
				point(at, true);
				point(at + MV::Point<int>(0, a_pixels), true);
				point(at + MV::Point<int>(0, a_pixels), false);
			}

			float listUnitsFor(int a_pixels) {
				return list->localFromScreen(MV::Point<int>(0, a_pixels)).y - list->localFromScreen(MV::Point<int>(0, 0)).y;
			}
		};

		static bool sameBox(const MV::BoxAABB<int>& a_box, const MV::Point<int>& a_min, const MV::Point<int>& a_max) {
			return a_box.minPoint.x == a_min.x && a_box.minPoint.y == a_min.y && a_box.maxPoint.x == a_max.x && a_box.maxPoint.y == a_max.y;
		}

		void forge_tests() override {
			test("content longer than the view scrolls with a drag and stops at each end", [] {
				List scene;
				scene.drag(-30);
				check_true(scene.scrolledTo(scene.listUnitsFor(-30)), "dragged up 30 pixels it follows: " + std::to_string(scene.scrolled()));
				check_true(scene.scrolled() < -1.0f, "which is a move: " + std::to_string(scene.scrolled()));
				scene.drag(-2000);
				check_true(scene.scrolledTo(-200.0f), "its last row stops at the foot of the view: " + std::to_string(scene.scrolled()));
				scene.drag(4000);
				check_true(scene.scrolledTo(0.0f), "and its first at the head: " + std::to_string(scene.scrolled()));
			});

			test("content that fits the view stays where it was put", [] {
				List scene(1);
				scene.content->position(MV::point(0.0f, 20.0f));
				scene.drag(-30);
				check_true(scene.scrolledTo(20.0f), std::to_string(scene.scrolled()));
				scene.drag(60);
				check_true(scene.scrolledTo(20.0f), std::to_string(scene.scrolled()));
			});

			test("focusOnChild puts a row in the middle of the view, as near as the content's ends allow", [] {
				List scene;
				scene.scroller->focusOnChild(scene.rows[3]->owner());
				check_true(scene.scrolledTo(-125.0f), "a row below comes up to the middle: " + std::to_string(scene.scrolled()));
				scene.scroller->focusOnChild(scene.rows[2]->owner());
				check_true(scene.scrolledTo(-75.0f), "a row already showing is put there too: " + std::to_string(scene.scrolled()));
				scene.scroller->focusOnChild(scene.rows[1]->owner());
				check_true(scene.scrolledTo(-25.0f));
				scene.scroller->focusOnChild(scene.rows[0]->owner());
				check_true(scene.scrolledTo(0.0f), "the first row stops with the content's head at the view's: " + std::to_string(scene.scrolled()));
				scene.scroller->focusOnChild(scene.rows[5]->owner());
				check_true(scene.scrolledTo(-200.0f), "and the last with its foot at the view's: " + std::to_string(scene.scrolled()));
				scene.scroller->focusOnChild(scene.under->owner());
				check_true(scene.scrolledTo(-200.0f), "a node that is not in the content is not its business");
				scene.rows[0]->owner()->focusInParents();
				check_true(scene.scrolledTo(0.0f), "a node asks everything above it");
			});

			test("the focus moving onto a row out of view scrolls to it, and a pointer's press does not", [] {
				List scene;
				scene.rows[0]->focus();
				check_true(scene.scrolledTo(0.0f));
				check_true(scene.host.mouse.focusMove(FocusDirection::DOWN) && scene.rows[1]->focused() && scene.scrolledTo(-25.0f), "the row the focus moves to goes to the middle: " + std::to_string(scene.scrolled()));
				scene.content->position(MV::point(0.0f, 0.0f));
				check_true(scene.host.mouse.focusMove(FocusDirection::DOWN) && scene.rows[2]->focused(), "the row below, though it is clipped away, is next");
				check_true(scene.scrolledTo(-75.0f), "and is brought in: " + std::to_string(scene.scrolled()));
				check_true(scene.host.mouse.focusMove(FocusDirection::DOWN) && scene.rows[3]->focused());
				check_true(scene.scrolledTo(-125.0f));
				check_true(scene.host.mouse.focusNext() && scene.rows[4]->focused() && scene.scrolledTo(-175.0f), "walking in order scrolls the same way");
				check_true(scene.host.mouse.focusMove(FocusDirection::UP) && scene.rows[3]->focused() && scene.scrolledTo(-125.0f));

				scene.content->position(MV::point(0.0f, -75.0f));
				auto at = scene.onScreen(100.0f, 10.0f);
				scene.point(at, false);
				scene.point(at, true);
				scene.point(at, false);
				check_true(scene.rows[1]->focused(), "pressed on the half of it that shows, a row takes the focus");
				check_true(scene.scrolledTo(-75.0f), "and the list stays under the pointer: " + std::to_string(scene.scrolled()));
			});

			test("a row scrolled out of view is reached only from inside its list", [] {
				List scene;
				scene.under->focus();
				check_true(scene.host.mouse.focusMove(FocusDirection::UP), "up from under the list");
				check_true(scene.rows[1]->focused(), "lands on the last row showing, not on the hidden one whose true place is behind where it started");
				check_true(scene.scrolledTo(-25.0f));

				scene.rows[0]->focus();
				auto behind = scene.root->screenFromLocal(MV::point(200.0f, 215.0f));
				check_true(scene.host.mouse.focusNearest(behind) && scene.under->focused(), "and the nearest to a point is never one that does not show");
			});

			test("what shows of a node is its box cut to the clipping on it and above it", [] {
				List scene;
				check_true(sameBox(scene.list->visibleScreenBounds(), scene.onScreen(0.0f, 0.0f), scene.onScreen(200.0f, 100.0f)), "the list shows as much as its view");
				check_true(scene.list->screenBounds().height() > scene.list->visibleScreenBounds().height() * 2, "while its bounds are the whole of its content");
				check_true(sameBox(scene.under->owner()->visibleScreenBounds(), scene.under->owner()->screenBounds().minPoint, scene.under->owner()->screenBounds().maxPoint), "with nothing clipping it a node shows whole");

				scene.content->position(MV::point(0.0f, -25.0f));
				auto firstRow = scene.rows[0]->owner();
				check_true(sameBox(firstRow->visibleScreenBounds(), scene.onScreen(0.0f, 0.0f), scene.onScreen(200.0f, 25.0f)), "a row half out of the view shows its other half");
				check_true(firstRow->clippedAwayBy(firstRow->bounds()) == nullptr);
				auto hiddenRow = scene.rows[4]->owner();
				check_true(hiddenRow->visibleScreenBounds().empty(), "a row out of it shows nothing");
				check_true(hiddenRow->clippedAwayBy(hiddenRow->bounds()) == scene.list, "and the list is what hides it");
				check_true(sameBox(scene.content->visibleScreenBounds(), scene.onScreen(0.0f, 0.0f), scene.onScreen(200.0f, 100.0f)), "the content shows through the view and no more");
			});

			test("a group that scrolls is, for a move, as big as what shows of it", [] {
				List scene;
				auto group = scene.list->attach<MV::Scene::FocusGroup>(scene.host.mouse).self();
				group->explicitSelect(true);
				scene.under->focus();
				scene.host.mouse.focusContext();
				check_true(scene.host.mouse.focusMove(FocusDirection::UP) && group->selected(), "up from under it reaches it, though its content runs on down behind where the move started");
				check_true(scene.host.mouse.focusActivate() && scene.rows[0]->focused(), "and going in starts on its first row");
				check_true(scene.host.mouse.focusMove(FocusDirection::DOWN, scene.list.get()) && scene.host.mouse.focusMove(FocusDirection::DOWN, scene.list.get()) && scene.rows[2]->focused());
				check_true(scene.scrolledTo(-75.0f), "moves inside it scroll: " + std::to_string(scene.scrolled()));
			});
		}
	};

}

using mv_tests_scroller_suite = mv_tests::scroller_suite;
FOUNDRY_REGISTER(mv_tests_scroller_suite)
