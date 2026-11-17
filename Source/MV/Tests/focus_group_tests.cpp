// Selection contexts: FocusGroup windows and tab strips, and the TapDevice keeping the focus to the one in use.

#include <jaiscript/testing/foundry.hpp>
#include "mv_scene_host.hpp"
#include "MV/Interface/controllerDevice.h"
#include "MV/Render/Scene/clickable.h"
#include "MV/Render/Scene/focusGroup.h"

using namespace jai::foundry;
using MV::ControllerButton;
using MV::ControllerState;
using MV::FocusDirection;
using MV::Scene::Clickable;
using MV::Scene::FocusGroup;
using MV::Scene::Node;

namespace mv_tests {

	class focus_group_suite : public suite {
	public:
		focus_group_suite() : suite("MV Focus Groups") {}

		struct Target {
			std::shared_ptr<Clickable> clickable;
			int accepted = 0;
			jai::signal<Clickable::ButtonSignalSignature>::shared_receiver_type receiver;
		};

		static std::shared_ptr<Target> addTarget(SceneHost& a_host, const std::shared_ptr<Node>& a_parent, const std::string& a_id, const MV::Point<>& a_at) {
			auto result = std::make_shared<Target>();
			auto node = a_parent->make(a_id);
			node->position(a_at);
			result->clickable = node->attach<Clickable>(a_host.mouse)->bounds(MV::size(100.0f, 50.0f));
			result->clickable->focusable(true);
			auto* counts = result.get();
			result->receiver = result->clickable->onAccept.connect([counts](const std::shared_ptr<Clickable>&) { ++counts->accepted; });
			return result;
		}

		struct Window {
			std::shared_ptr<Node> node;
			std::shared_ptr<FocusGroup> group;
		};

		static Window addGroup(SceneHost& a_host, const std::shared_ptr<Node>& a_parent, const std::string& a_id, FocusGroup::Kind a_kind = FocusGroup::Kind::WINDOW) {
			Window result;
			result.node = a_parent->make(a_id);
			result.group = result.node->attach<FocusGroup>(a_host.mouse).self();
			result.group->kind(a_kind);
			return result;
		}

		// Two windows side by side, two targets in each, one above the other.
		struct TwoWindows {
			SceneHost host;
			std::shared_ptr<Node> root;
			Window left;
			Window right;
			std::shared_ptr<Target> leftTop;
			std::shared_ptr<Target> leftBottom;
			std::shared_ptr<Target> rightTop;
			std::shared_ptr<Target> rightBottom;

			TwoWindows() {
				root = Node::make(host.renderer, "root");
				left = addGroup(host, root, "left");
				right = addGroup(host, root, "right");
				leftTop = addTarget(host, left.node, "leftTop", MV::point(0.0f, 0.0f));
				leftBottom = addTarget(host, left.node, "leftBottom", MV::point(0.0f, 100.0f));
				rightTop = addTarget(host, right.node, "rightTop", MV::point(300.0f, 0.0f));
				rightBottom = addTarget(host, right.node, "rightBottom", MV::point(300.0f, 100.0f));
			}
		};

		static bool frame(MV::ControllerDevice& a_pad, SceneHost& a_host, const ControllerState& a_state) {
			a_pad.simulate(a_state);
			a_pad.update();
			return a_pad.navigate(a_host.mouse, 0.016);
		}

		void forge_tests() override {
			test("focus moves keep to the window the focus is in", [] {
				TwoWindows scene;
				auto& mouse = scene.host.mouse;
				check_true(mouse.focusContext() == nullptr, "with nothing focused no window is in use");
				scene.leftTop->clickable->focus();
				const void* context = mouse.focusContext();
				check_true(context == scene.left.node.get(), "the window holding the focus is the one in use");
				check_false(mouse.focusMove(FocusDirection::RIGHT, context), "the window beside it is out of reach");
				check_true(mouse.focusMove(FocusDirection::DOWN, context) && scene.leftBottom->clickable->focused());
				scene.rightTop->clickable->focus();
				check_true(mouse.focusContext() == scene.right.node.get(), "focusing into another window, as a pointer would, makes it the one in use");
			});

			test("stepping windows goes to what each remembers, else its default, else its first", [] {
				TwoWindows scene;
				auto& mouse = scene.host.mouse;
				scene.right.group->defaultFocus("rightBottom");
				scene.leftBottom->clickable->focus();
				mouse.focusContext();
				check_true(mouse.focusWindowNext() && scene.rightBottom->clickable->focused(), "a window not visited yet starts on its default");
				scene.rightTop->clickable->focus();
				mouse.focusContext();
				check_true(mouse.focusWindowNext() && scene.leftBottom->clickable->focused(), "past the last window wraps to the first, onto what it remembers");
				check_true(mouse.focusWindowPrevious() && scene.rightTop->clickable->focused(), "what a window remembers comes before its default");
				check_true(scene.right.group->remembered() == scene.rightTop->clickable);

				auto early = addGroup(scene.host, scene.root, "early");
				early.group->order(-1);
				auto earlyTarget = addTarget(scene.host, early.node, "earlyTarget", MV::point(600.0f, 0.0f));
				mouse.focusContext();
				check_true(mouse.focusWindowNext() && earlyTarget->clickable->focused(), "order comes before place in the scene, and a window with neither starts on its first");
				check_true(mouse.focusWindowNext() && scene.leftBottom->clickable->focused());
			});

			test("a modal window takes the focus as it shows and gives it back as it hides", [] {
				TwoWindows scene;
				auto& mouse = scene.host.mouse;
				auto popup = addGroup(scene.host, scene.root, "popup");
				popup.group->modal(true);
				auto yes = addTarget(scene.host, popup.node, "yes", MV::point(100.0f, 300.0f));
				auto no = addTarget(scene.host, popup.node, "no", MV::point(300.0f, 300.0f));
				popup.group->defaultFocus("no");
				popup.node->hide();
				scene.leftBottom->clickable->focus();
				check_true(mouse.focusContext() == scene.left.node.get(), "a hidden window is no window");

				popup.node->show();
				const void* context = mouse.focusContext();
				check_true(context == popup.node.get() && no->clickable->focused(), "shown, it takes the focus onto its default");
				check_false(mouse.focusWindowNext(), "nothing steps out of it");
				check_false(mouse.focusMove(FocusDirection::UP, context), "and no move leaves it");
				check_true(mouse.focusMove(FocusDirection::LEFT, context) && yes->clickable->focused());
				mouse.focusContext();
				scene.rightTop->clickable->focus();
				check_true(mouse.focusContext() == popup.node.get() && yes->clickable->focused(), "a focus put outside it comes straight back to what it remembers");

				popup.node->hide();
				check_true(mouse.focusContext() == scene.left.node.get() && scene.leftBottom->clickable->focused(), "hidden, the focus returns to where it was");
			});

			test("the focus moves on when what holds it is hidden", [] {
				TwoWindows scene;
				auto& mouse = scene.host.mouse;
				scene.leftTop->clickable->focus();
				mouse.focusContext();
				scene.leftTop->clickable->owner()->hide();
				check_true(mouse.focusContext() == scene.left.node.get() && scene.leftBottom->clickable->focused(), "to something else in its window");

				SceneHost bare;
				auto root = Node::make(bare.renderer, "root");
				auto lone = addTarget(bare, root, "lone", MV::point(0.0f, 0.0f));
				lone->clickable->focus();
				bare.mouse.focusContext();
				check_true(lone->clickable->focused(), "a focus that stands is left alone");
				lone->clickable->owner()->hide();
				bare.mouse.focusContext();
				check_false(bare.mouse.hasFocus(), "with no window to stay in, it is let go");
			});

			test("stepping tabs presses the next header of the strip in the window in use", [] {
				SceneHost host;
				auto& mouse = host.mouse;
				auto root = Node::make(host.renderer, "root");
				auto window = addGroup(host, root, "window");
				auto strip = addGroup(host, window.node, "strip", FocusGroup::Kind::TABS);
				auto first = addTarget(host, strip.node, "first", MV::point(0.0f, 0.0f));
				auto second = addTarget(host, strip.node, "second", MV::point(120.0f, 0.0f));
				auto third = addTarget(host, strip.node, "third", MV::point(240.0f, 0.0f));
				auto content = addTarget(host, window.node, "content", MV::point(0.0f, 100.0f));
				auto elsewhere = addGroup(host, root, "elsewhere");
				auto other = addTarget(host, elsewhere.node, "other", MV::point(600.0f, 0.0f));

				other->clickable->focus();
				mouse.focusContext();
				check_false(mouse.focusTabNext(), "a window with no strip has no tabs to step");
				check_true(mouse.focusWindowNext() && content->clickable->focused(), "stepping into the window lands on its content, not a tab header");

				check_true(mouse.focusTabNext() && second->accepted == 1 && first->accepted == 0, "the first tab is taken to be showing, so next is the second");
				check_true(content->clickable->focused(), "and the focus stays where it was");
				check_true(mouse.focusTabNext() && third->accepted == 1);
				check_true(mouse.focusTabNext() && first->accepted == 1, "past the last wraps to the first");
				check_true(mouse.focusTabPrevious() && third->accepted == 2);

				second->clickable->focus();
				mouse.focusContext();
				content->clickable->focus();
				check_true(mouse.focusTabNext() && third->accepted == 3, "a header chosen some other way is where stepping carries on from");
			});

			test("backing out is the window's own signal", [] {
				TwoWindows scene;
				auto& mouse = scene.host.mouse;
				int leftBacks = 0;
				int rightBacks = 0;
				auto leftReceiver = scene.left.group->onBack.connect([&](const std::shared_ptr<FocusGroup>&) { ++leftBacks; });
				auto rightReceiver = scene.right.group->onBack.connect([&](const std::shared_ptr<FocusGroup>&) { ++rightBacks; });
				check_false(mouse.focusBack(), "with no window in use there is nothing to back out of");
				scene.rightTop->clickable->focus();
				mouse.focusContext();
				check_true(mouse.focusBack() && rightBacks == 1 && leftBacks == 0);
			});

			test("groups nest: stepping is between the groups side by side, a move onto a child goes down a level, and back goes up one", [] {
				SceneHost host;
				auto& mouse = host.mouse;
				auto root = Node::make(host.renderer, "root");
				auto screen = addGroup(host, root, "screen");
				auto list = addGroup(host, screen.node, "list");
				auto detail = addGroup(host, screen.node, "detail");
				auto itemOne = addTarget(host, list.node, "itemOne", MV::point(0.0f, 0.0f));
				auto itemTwo = addTarget(host, list.node, "itemTwo", MV::point(0.0f, 100.0f));
				auto fieldOne = addTarget(host, detail.node, "fieldOne", MV::point(300.0f, 0.0f));
				auto fieldTwo = addTarget(host, detail.node, "fieldTwo", MV::point(300.0f, 100.0f));
				auto close = addTarget(host, screen.node, "close", MV::point(0.0f, 300.0f));
				auto apply = addTarget(host, screen.node, "apply", MV::point(300.0f, 300.0f));

				itemTwo->clickable->focus();
				const void* context = mouse.focusContext();
				check_true(context == list.node.get(), "the group in use is the innermost one holding the focus");
				check_false(mouse.focusMove(FocusDirection::RIGHT, context) || mouse.focusMove(FocusDirection::DOWN, context), "moves stay inside it");
				check_true(mouse.focusWindowNext() && fieldOne->clickable->focused(), "stepping goes to the group beside it");
				fieldTwo->clickable->focus();
				mouse.focusContext();
				check_true(mouse.focusWindowNext() && itemTwo->clickable->focused(), "and round again, each onto what it remembers");
				check_true(mouse.focusWindowPrevious() && fieldTwo->clickable->focused());

				check_true(mouse.focusBack() && apply->clickable->focused() == false && close->clickable->focused(), "back goes up a level, onto the parent's own controls");
				context = mouse.focusContext();
				check_true(context == screen.node.get(), "where the parent is the group in use");
				check_true(mouse.focusMove(FocusDirection::RIGHT, context) && apply->clickable->focused());
				mouse.focusContext();
				check_true(mouse.focusMove(FocusDirection::UP, context) && fieldTwo->clickable->focused(), "from there a move onto a child's control goes down into it");
				check_true(mouse.focusContext() == detail.node.get());
				check_true(mouse.focusBack() && apply->clickable->focused(), "and back returns to the parent control last used, not one inside a child");
				mouse.focusContext();
				check_false(mouse.focusBack(), "at the top, with nothing listening, there is nowhere further back");
				check_true(apply->clickable->focused());

				int screenBacks = 0;
				int listBacks = 0;
				auto screenReceiver = screen.group->onBack.connect([&](const std::shared_ptr<FocusGroup>&) { ++screenBacks; });
				itemOne->clickable->focus();
				mouse.focusContext();
				check_true(mouse.focusBack() && screenBacks == 0 && apply->clickable->focused(), "a group that does not listen for back still only goes up a level");
				mouse.focusContext();
				check_true(mouse.focusBack() && screenBacks == 1, "and the one that listens gets it");
				auto listReceiver = list.group->onBack.connect([&](const std::shared_ptr<FocusGroup>&) { ++listBacks; });
				itemOne->clickable->focus();
				mouse.focusContext();
				check_true(mouse.focusBack() && listBacks == 1 && screenBacks == 1 && itemOne->clickable->focused(), "a group that listens keeps back to itself");
			});

			test("a group with none beside it steps the level above, and back passes through a parent with no controls of its own", [] {
				SceneHost host;
				auto& mouse = host.mouse;
				auto root = Node::make(host.renderer, "root");
				auto outer = addGroup(host, root, "outer");
				auto inner = addGroup(host, outer.node, "inner");
				auto deep = addTarget(host, inner.node, "deep", MV::point(0.0f, 0.0f));
				auto beside = addGroup(host, root, "beside");
				auto other = addTarget(host, beside.node, "other", MV::point(300.0f, 0.0f));
				int outerBacks = 0;
				auto outerReceiver = outer.group->onBack.connect([&](const std::shared_ptr<FocusGroup>&) { ++outerBacks; });

				check_true(mouse.focusWindowNext() && deep->clickable->focused(), "with nothing in use, stepping starts at the first top group");
				mouse.focusContext();
				check_true(mouse.focusWindowNext() && other->clickable->focused(), "the inner group has none beside it, so its parent's level steps");
				mouse.focusContext();
				check_true(mouse.focusWindowNext() && deep->clickable->focused(), "and stepping back into the parent lands inside its child");
				mouse.focusContext();
				check_true(mouse.focusBack() && outerBacks == 1 && deep->clickable->focused(), "back from the inner group reaches the parent that listens, there being no control to stop at");
			});

			test("a modal window holds its own levels: nothing steps or backs out past it", [] {
				TwoWindows scene;
				auto& mouse = scene.host.mouse;
				auto popup = addGroup(scene.host, scene.root, "popup");
				popup.group->modal(true);
				auto confirm = addTarget(scene.host, popup.node, "confirm", MV::point(100.0f, 300.0f));
				auto options = addGroup(scene.host, popup.node, "options");
				auto option = addTarget(scene.host, options.node, "option", MV::point(100.0f, 400.0f));
				scene.leftTop->clickable->focus();
				const void* context = mouse.focusContext();
				check_true(context == popup.node.get() && confirm->clickable->focused());
				check_true(mouse.focusMove(FocusDirection::DOWN, context) && option->clickable->focused());
				check_true(mouse.focusContext() == options.node.get(), "a group inside the modal window is the one in use while it holds the focus");
				check_false(mouse.focusWindowNext(), "it has none beside it and stepping stops at the modal window");
				check_true(option->clickable->focused());
				check_true(mouse.focusBack() && confirm->clickable->focused(), "back goes up to the modal window's own controls");
				mouse.focusContext();
				check_false(mouse.focusBack(), "and no further");
				check_true(confirm->clickable->focused());
			});

			test("a controller's triggers step windows, its shoulders step tabs and its east button backs out", [] {
				TwoWindows scene;
				auto& host = scene.host;
				auto strip = addGroup(host, scene.right.node, "strip", FocusGroup::Kind::TABS);
				auto tabOne = addTarget(host, strip.node, "tabOne", MV::point(300.0f, 200.0f));
				auto tabTwo = addTarget(host, strip.node, "tabTwo", MV::point(420.0f, 200.0f));
				int backs = 0;
				auto backReceiver = scene.right.group->onBack.connect([&](const std::shared_ptr<FocusGroup>&) { ++backs; });
				MV::ControllerDevice pad;
				scene.leftTop->clickable->focus();

				check_false(frame(pad, host, ControllerState().press(ControllerButton::DPAD_RIGHT)), "the d-pad stays in the window");
				check_true(scene.leftTop->clickable->focused());
				frame(pad, host, ControllerState());
				check_false(frame(pad, host, ControllerState().press(ControllerButton::RIGHT_SHOULDER)), "no tabs in this window");
				frame(pad, host, ControllerState());
				check_true(frame(pad, host, ControllerState().axis(MV::ControllerAxis::RIGHT_TRIGGER, 1.0f)) && scene.rightTop->clickable->focused(), "the right trigger goes to the next window");
				check_false(frame(pad, host, ControllerState().axis(MV::ControllerAxis::RIGHT_TRIGGER, 1.0f)), "held, it does not step again");
				check_true(scene.rightTop->clickable->focused());
				frame(pad, host, ControllerState());
				check_true(frame(pad, host, ControllerState().press(ControllerButton::RIGHT_SHOULDER)) && tabTwo->accepted == 1, "the right shoulder presses the next tab");
				frame(pad, host, ControllerState());
				check_true(frame(pad, host, ControllerState().press(ControllerButton::LEFT_SHOULDER)) && tabOne->accepted == 1, "the left shoulder the previous");
				frame(pad, host, ControllerState());
				check_true(frame(pad, host, ControllerState().press(ControllerButton::EAST)) && backs == 1, "east backs out of the window in use");
				frame(pad, host, ControllerState());
				check_true(frame(pad, host, ControllerState().axis(MV::ControllerAxis::LEFT_TRIGGER, 1.0f)) && scene.leftTop->clickable->focused(), "the left trigger goes back a window");
			});
		}
	};

}

using mv_tests_focus_group_suite = mv_tests::focus_group_suite;
FOUNDRY_REGISTER(mv_tests_focus_group_suite)
