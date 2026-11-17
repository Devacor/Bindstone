// ControllerDevice: buttons and sticks read as one pad, and the pad as focus moves on a TapDevice.

#include <jaiscript/testing/foundry.hpp>
#include <cmath>
#include "mv_scene_host.hpp"
#include "MV/Interface/controllerDevice.h"
#include "MV/Render/Scene/clickable.h"

using namespace jai::foundry;
using MV::ControllerAxis;
using MV::ControllerButton;
using MV::ControllerState;
using MV::FocusDirection;
using MV::Scene::Clickable;
using MV::Scene::Node;

namespace mv_tests {

	class controller_suite : public suite {
	public:
		controller_suite() : suite("MV Controller") {}

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

		static ControllerState stickAt(float a_x, float a_y) {
			return ControllerState().axis(ControllerAxis::LEFT_X, a_x).axis(ControllerAxis::LEFT_Y, a_y);
		}

		// One frame of a pad held a_state: read, then turned into focus moves.
		static bool frame(MV::ControllerDevice& a_pad, SceneHost& a_host, const ControllerState& a_state, double a_dt = 0.016) {
			a_pad.simulate(a_state);
			a_pad.update();
			return a_pad.navigate(a_host.mouse, a_dt);
		}

		void forge_tests() override {
			test("a button reports going down and up once each, and reads as held in between", [] {
				MV::ControllerDevice pad;
				std::vector<std::string> seen;
				auto downReceiver = pad.onButtonDown.connect([&](MV::ControllerDevice& a_pad, ControllerButton a_button) {
					seen.push_back("down " + std::to_string(static_cast<int>(a_button)) + (a_pad.down(a_button) ? " held" : " not held"));
				});
				auto upReceiver = pad.onButtonUp.connect([&](MV::ControllerDevice&, ControllerButton a_button) {
					seen.push_back("up " + std::to_string(static_cast<int>(a_button)));
				});
				pad.update();
				check_false(pad.connected(), "a headless run has no pad");
				pad.simulate(ControllerState().press(ControllerButton::EAST));
				pad.update();
				pad.update();
				check_true(pad.connected() && pad.down(ControllerButton::EAST) && !pad.down(ControllerButton::SOUTH));
				pad.simulate(ControllerState());
				pad.update();
				check_eq(size_t(2), seen.size());
				check_eq(std::string("down 1 held"), seen[0]);
				check_eq(std::string("up 1"), seen[1]);
			});

			test("a trigger pulled halfway is a button, and a pad coming and going is reported", [] {
				MV::ControllerDevice pad;
				int connects = 0;
				int disconnects = 0;
				auto connectReceiver = pad.onConnect.connect([&](MV::ControllerDevice&) { ++connects; });
				auto disconnectReceiver = pad.onDisconnect.connect([&](MV::ControllerDevice&) { ++disconnects; });
				pad.simulate(ControllerState().axis(ControllerAxis::RIGHT_TRIGGER, 0.3f));
				pad.update();
				check_false(pad.down(ControllerButton::RIGHT_TRIGGER));
				pad.simulate(ControllerState().axis(ControllerAxis::RIGHT_TRIGGER, 0.8f));
				pad.update();
				check_true(pad.down(ControllerButton::RIGHT_TRIGGER) && !pad.down(ControllerButton::LEFT_TRIGGER));
				check_true(connects == 1 && disconnects == 0);
				pad.stopSimulating();
				pad.update();
				check_true(connects == 1 && disconnects == 1 && !pad.connected() && !pad.down(ControllerButton::RIGHT_TRIGGER), "with the simulated pad gone nothing is held");
			});

			test("a stick reads nothing inside its dead zone and full at the rim", [] {
				MV::ControllerDevice pad;
				pad.deadZone(0.25f);
				pad.simulate(stickAt(0.2f, -0.1f));
				pad.update();
				check_true(pad.leftStick().x == 0.0f && pad.leftStick().y == 0.0f, "inside the dead zone");
				check_true(pad.axis(ControllerAxis::LEFT_X) == 0.2f, "the axis itself reads as it is");
				pad.simulate(stickAt(0.0f, 1.0f));
				pad.update();
				check_true(std::abs(pad.leftStick().y - 1.0f) < 0.001f && pad.leftStick().x == 0.0f, "full at the rim");
				pad.simulate(stickAt(-0.625f, 0.0f));
				pad.update();
				check_true(std::abs(pad.leftStick().x + 0.5f) < 0.001f, "halfway between the dead zone and the rim is half");
				check_true(pad.rightStick().x == 0.0f && pad.rightStick().y == 0.0f);
			});

			test("the d-pad gives eight directions and the stick a wide slice for each of the four", [] {
				MV::ControllerDevice pad;
				auto held = [&pad](const ControllerState& a_state) {
					pad.simulate(a_state);
					pad.update();
					return pad.navigationDirection();
				};
				check_true(!held(ControllerState()).has_value());
				check_true(held(ControllerState().press(ControllerButton::DPAD_LEFT)) == FocusDirection::LEFT);
				check_true(held(ControllerState().press(ControllerButton::DPAD_UP).press(ControllerButton::DPAD_RIGHT)) == FocusDirection::UP_RIGHT);
				check_true(held(ControllerState().press(ControllerButton::DPAD_DOWN).press(ControllerButton::DPAD_LEFT)) == FocusDirection::DOWN_LEFT);
				check_true(!held(ControllerState().press(ControllerButton::DPAD_LEFT).press(ControllerButton::DPAD_RIGHT)).has_value(), "opposite ways cancel");
				check_true(held(stickAt(1.0f, 0.0f)) == FocusDirection::RIGHT);
				check_true(held(stickAt(0.9f, 0.4f)) == FocusDirection::RIGHT, "roughly sideways is sideways");
				check_true(held(stickAt(0.7f, 0.7f)) == FocusDirection::DOWN_RIGHT);
				check_true(held(stickAt(-0.7f, -0.7f)) == FocusDirection::UP_LEFT);
				check_true(held(stickAt(0.2f, -0.9f)) == FocusDirection::UP);
				check_true(!held(stickAt(0.4f, 0.3f)).has_value(), "a stick pushed a little is not a move");
				check_true(held(ControllerState().press(ControllerButton::DPAD_DOWN).axis(ControllerAxis::LEFT_X, 1.0f)) == FocusDirection::DOWN, "the d-pad wins over the stick");
			});

			test("a direction moves the focus once, then repeats while it is held", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				std::vector<std::shared_ptr<Target>> row;
				for (int index = 0; index < 5; ++index) {
					row.push_back(addTarget(host, root, "target" + std::to_string(index), MV::point(index * 150.0f, 0.0f)));
				}
				MV::ControllerDevice pad;
				pad.navigationRepeat(0.4, 0.1);
				auto right = ControllerState().press(ControllerButton::DPAD_RIGHT);
				row[0]->clickable->focus();
				check_true(frame(pad, host, right) && row[1]->clickable->focused(), "pressed, it moves at once");
				for (int waited = 0; waited < 3; ++waited) {
					check_false(frame(pad, host, right, 0.1), "held, it waits");
				}
				check_true(row[1]->clickable->focused());
				check_true(frame(pad, host, right, 0.11) && row[2]->clickable->focused(), "then repeats");
				check_true(frame(pad, host, right, 0.11) && row[3]->clickable->focused(), "at the shorter interval");
				frame(pad, host, ControllerState());
				check_true(frame(pad, host, ControllerState().press(ControllerButton::DPAD_LEFT)) && row[2]->clickable->focused(), "let go and pressed again moves at once");
				frame(pad, host, ControllerState());
				check_true(frame(pad, host, stickAt(1.0f, 0.0f)) && row[3]->clickable->focused(), "the left stick moves it too");
				check_false(frame(pad, host, stickAt(0.5f, 0.0f), 0.1), "and stays held as it eases back a little");
				check_true(pad.navigationDirection() == FocusDirection::RIGHT);
				frame(pad, host, stickAt(0.1f, 0.0f));
				check_true(frame(pad, host, stickAt(0.7f, -0.7f)) && row[4]->clickable->focused(), "a diagonal with nothing that way goes sideways");
				frame(pad, host, ControllerState());
				check_false(frame(pad, host, right), "and at the end of the row there is nowhere further");
				check_true(row[4]->clickable->focused());
			});

			test("with nothing focused a direction takes the first focusable", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto first = addTarget(host, root, "first", MV::point(0.0f, 0.0f));
				auto second = addTarget(host, root, "second", MV::point(0.0f, 100.0f));
				MV::ControllerDevice pad;
				check_true(frame(pad, host, ControllerState().press(ControllerButton::DPAD_DOWN)) && first->clickable->focused());
				check_false(second->clickable->focused());
			});

			test("the south button presses the focused Clickable once, and the shoulders step through the order", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto first = addTarget(host, root, "first", MV::point(0.0f, 0.0f));
				auto second = addTarget(host, root, "second", MV::point(300.0f, 300.0f));
				MV::ControllerDevice pad;
				first->clickable->focus();
				auto south = ControllerState().press(ControllerButton::SOUTH);
				check_true(frame(pad, host, south));
				check_false(frame(pad, host, south), "held, it does not press again");
				check_eq(1, first->accepted);
				frame(pad, host, ControllerState());
				check_true(frame(pad, host, ControllerState().press(ControllerButton::RIGHT_SHOULDER)) && second->clickable->focused(), "the right shoulder is next");
				frame(pad, host, ControllerState());
				check_true(frame(pad, host, ControllerState().press(ControllerButton::LEFT_SHOULDER)) && first->clickable->focused(), "the left shoulder is previous");
				frame(pad, host, ControllerState());
				host.mouse.focusClear();
				check_false(frame(pad, host, south), "with nothing focused there is nothing to press");
				check_eq(1, first->accepted);
			});
		}
	};

}

using mv_tests_controller_suite = mv_tests::controller_suite;
FOUNDRY_REGISTER(mv_tests_controller_suite)
