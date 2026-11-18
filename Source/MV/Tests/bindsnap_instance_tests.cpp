// BindSnapInstance: a node linked to a .bindsnap file. The file's content is on the node and not saved with it;
// the scene keeps the file's name and what the instance changed.

#include <jaiscript/testing/foundry.hpp>
#include <cmath>
#include "mv_scene_host.hpp"
#include "MV/Render/Scene/sprite.h"
#include "MV/Render/Scene/bindSnapInstance.h"

using namespace jai::foundry;
using MV::Scene::BindSnapInstance;
using MV::Scene::Node;
using MV::Scene::Sprite;

namespace mv_tests {

	class bindsnap_instance_suite : public suite {
	public:
		bindsnap_instance_suite() : suite("MV BindSnap Instance") {}

		// A crate 40 square with a label 10 square at 5,6 in it.
		static void writeCrate(SceneHost& a_host, const std::string& a_file, float a_labelSize = 10.0f, bool a_withLabel = true, bool a_withExtra = false) {
			auto crate = Node::make(a_host.renderer, "Crate");
			crate->attach<Sprite>()->bounds(MV::size(40.0f, 40.0f));
			if (a_withLabel) {
				auto label = crate->make("Label");
				label->position(MV::point(5.0f, 6.0f));
				label->attach<Sprite>()->bounds(MV::size(a_labelSize, a_labelSize));
			}
			if (a_withExtra) {
				crate->make("Extra")->attach<Sprite>()->bounds(MV::size(3.0f, 3.0f));
			}
			crate->saveJai(a_file, a_host.services, false);
		}

		struct Placed {
			SceneHost host;
			ScratchScene file;
			std::shared_ptr<Node> root;
			std::shared_ptr<Node> node;
			std::shared_ptr<BindSnapInstance> instance;
			MV::Scene::BindSnapTracking tracking;

			~Placed() {
				host.services.disconnect<MV::Scene::BindSnapTracking>();
			}

			Placed() {
				host.services.connect(&tracking);
				writeCrate(host, file.path);
				root = Node::make(host.renderer, "root");
				node = root->make("placed");
				node->position(MV::point(100.0f, 50.0f));
				instance = node->attach<BindSnapInstance>(host.services)->source(file.path);
			}

			std::shared_ptr<Node> label() {
				return node->get("Label", false);
			}

			std::shared_ptr<Sprite> sprite(const std::shared_ptr<Node>& a_node) {
				return a_node ? a_node->component<Sprite>(false, false).self() : nullptr;
			}

			std::string saved() {
				ScratchScene scene;
				root->saveJai(scene.path, host.services, false);
				return scene.contents();
			}

			std::shared_ptr<Node> reloaded() {
				ScratchScene scene;
				return saveAndLoad(host, root, scene);
			}
		};

		// the typed accessors a component adds hide Component's own reading of the flag
		static bool savedWithNode(const std::shared_ptr<MV::Scene::Component>& a_component) {
			return a_component->serializable();
		}

		static bool at(const std::shared_ptr<Node>& a_node, float a_x, float a_y) {
			return a_node && std::abs(a_node->position().x - a_x) < 0.01f && std::abs(a_node->position().y - a_y) < 0.01f;
		}

		static bool wide(const std::shared_ptr<Sprite>& a_sprite, float a_width) {
			return a_sprite && std::abs(a_sprite->bounds().width() - a_width) < 0.01f;
		}

		static std::string listed(const std::vector<MV::Scene::BindSnapOverride>& a_overrides) {
			std::string result = std::to_string(a_overrides.size()) + " overrides";
			for (auto&& entry : a_overrides) {
				result += " [" + entry.node + "|" + entry.component + "|" + entry.property + "=" + entry.value.substr(0, 60) + "]";
			}
			return result;
		}

		static bool has(const std::vector<MV::Scene::BindSnapOverride>& a_overrides, const std::string& a_node, const std::string& a_component) {
			for (auto&& entry : a_overrides) {
				if (entry.node == a_node && entry.component == a_component) {
					return true;
				}
			}
			return false;
		}

		void forge_tests() override {
			test("an instance takes the file's content as its node's own, and the scene saves only the link", [] {
				Placed scene;
				check_true(scene.instance->built(), "setting the source builds it");
				check_true(wide(scene.sprite(scene.node), 40.0f), "the file's root gives the node its components");
				check_true(at(scene.label(), 5.0f, 6.0f) && wide(scene.sprite(scene.label()), 10.0f), "and its children, where the file has them");
				check_true(scene.node->id() == "placed" && at(scene.node, 100.0f, 50.0f), "the node keeps its own id and place");
				check_true(!scene.label()->serializable() && !savedWithNode(scene.sprite(scene.node)) && savedWithNode(scene.instance), "the content is not saved with the node; the link is");
				check_true(BindSnapInstance::holding(scene.label()) == scene.instance && !BindSnapInstance::holding(scene.node), "a node of the content knows the instance holding it");

				auto text = scene.saved();
				check_true(hasSerializedType(text, "BindSnapInstance") && text.find("\"source\"") != std::string::npos, "the scene names the file: " + text);
				check_true(text.find("Label") == std::string::npos && !hasSerializedType(text, "Sprite"), "and holds none of its content");

				auto loaded = scene.reloaded()->get("placed", false);
				check_true(loaded && at(loaded, 100.0f, 50.0f) && wide(scene.sprite(loaded), 40.0f) && at(loaded->get("Label", false), 5.0f, 6.0f), "loading the scene builds the content again");
				scene.instance->captureOverrides();
				check_true(scene.instance->overrides().empty(), "untouched, it has nothing of its own: " + listed(scene.instance->overrides()));
			});

			test("what an instance changes is saved as overrides and comes back with the scene", [] {
				Placed scene;
				scene.label()->position(MV::point(50.0f, 60.0f));
				scene.sprite(scene.node)->bounds(MV::size(80.0f, 80.0f));
				scene.instance->captureOverrides();
				auto&& overrides = scene.instance->overrides();
				check_true(has(overrides, "Label", "") && has(overrides, "", "Sprite"), "a child's own property and a root component's: " + listed(overrides));
				check_true(!has(overrides, "Label", "Sprite"), "what was left alone is not one: " + listed(overrides));

				auto text = scene.saved();
				check_true(text.find("\"overrides\"") != std::string::npos && text.find("\"node\"") != std::string::npos && text.find("\"property\"") != std::string::npos && text.find("\"value\"") != std::string::npos, "the scene carries them: " + text);
				auto loaded = scene.reloaded()->get("placed", false);
				check_true(loaded && at(loaded->get("Label", false), 50.0f, 60.0f) && wide(scene.sprite(loaded), 80.0f), "and a load applies them over the file");
				check_true(loaded && wide(scene.sprite(loaded->get("Label", false)), 10.0f), "leaving the rest as the file has it");
			});

			test("a refresh takes the file as it now is and keeps what the instance changed", [] {
				Placed scene;
				scene.label()->position(MV::point(50.0f, 60.0f));
				writeCrate(scene.host, scene.file.path, 30.0f, true, true);
				check_true(scene.instance->refresh(), "the file is read again");
				check_true(scene.node->get("Extra", false) != nullptr, "what the file gained is there");
				check_true(wide(scene.sprite(scene.label()), 30.0f), "what it changed has changed");
				check_true(at(scene.label(), 50.0f, 60.0f), "and the instance's own change is still on top: " + listed(scene.instance->overrides()));
				check_true(scene.node->id() == "placed" && at(scene.node, 100.0f, 50.0f));

				check_true(scene.instance->revert() && at(scene.label(), 5.0f, 6.0f) && scene.instance->overrides().empty(), "revert drops the instance's changes");
			});

			test("an override whose target the file no longer has is dropped", [] {
				Placed scene;
				scene.label()->position(MV::point(50.0f, 60.0f));
				scene.sprite(scene.node)->bounds(MV::size(80.0f, 80.0f));
				writeCrate(scene.host, scene.file.path, 10.0f, false);
				check_true(scene.instance->refresh() && !scene.label(), "the file lost the label");
				check_true(!has(scene.instance->overrides(), "Label", "") && has(scene.instance->overrides(), "", "Sprite") && wide(scene.sprite(scene.node), 80.0f), "its override goes and the others stay: " + listed(scene.instance->overrides()));
			});

			test("writing an instance to its file applies it, and the others take it on a refresh", [] {
				Placed scene;
				auto other = scene.root->make("other");
				auto otherInstance = other->attach<BindSnapInstance>(scene.host.services)->source(scene.file.path);
				scene.label()->position(MV::point(50.0f, 60.0f));
				scene.instance->captureOverrides();
				scene.instance->saveContentAs(scene.file.path);
				check_true(scene.instance->overrides().empty() && scene.instance->built() && at(scene.label(), 50.0f, 60.0f), "the instance has nothing of its own left");
				check_true(!scene.label()->serializable() && savedWithNode(scene.instance), "and its content is the file's again");
				auto text = scene.file.contents();
				check_true(!hasSerializedType(text, "BindSnapInstance") && text.find("Label") != std::string::npos, "the file is a plain BindSnap with no link in it");
				check_true(otherInstance->refresh() && at(other->get("Label", false), 50.0f, 60.0f), "another instance of the file sees the change");
				scene.instance->captureOverrides();
				check_true(scene.instance->overrides().empty(), "and the first matches its file: " + listed(scene.instance->overrides()));
			});

			test("unpacking makes the content the node's own", [] {
				Placed scene;
				scene.label()->position(MV::point(50.0f, 60.0f));
				scene.instance->unpack();
				scene.instance.reset();
				check_true(!scene.node->component<BindSnapInstance>(false, false), "the link is gone");
				check_true(scene.label() && scene.label()->serializable() && savedWithNode(scene.sprite(scene.node)), "and the content is saved with the node");
				auto text = scene.saved();
				check_true(!hasSerializedType(text, "BindSnapInstance") && text.find("Label") != std::string::npos && hasSerializedType(text, "Sprite"), text);
				auto loaded = scene.reloaded()->get("placed", false);
				check_true(loaded && at(loaded->get("Label", false), 50.0f, 60.0f));
			});

			test("a node already holding what was written to a file is linked without a load", [] {
				SceneHost host;
				ScratchScene file;
				auto root = Node::make(host.renderer, "root");
				auto node = root->make("Crate");
				node->attach<Sprite>()->bounds(MV::size(40.0f, 40.0f));
				auto label = node->make("Label");
				node->saveJai(file.path, host.services, false);
				auto instance = node->attach<BindSnapInstance>(host.services)->sourceOfPresentContent(file.path);
				check_true(instance->built() && node->get("Label", false) == label && !label->serializable(), "its content is the file's from then on, the same objects");
				ScratchScene scene;
				root->saveJai(scene.path, host.services, false);
				check_true(scene.contents().find("Label") == std::string::npos && hasSerializedType(scene.contents(), "BindSnapInstance"));
				auto extra = node->make("Mine");
				root->saveJai(scene.path, host.services, false);
				check_true(scene.contents().find("Mine") != std::string::npos, "a child added to the node afterwards is the node's own and is saved");
				auto loaded = Node::load(scene.path, host.services)->get("Crate", false);
				check_true(loaded && loaded->get("Label", false) && loaded->get("Mine", false), "and both come back");
			});

			test("a clone of an instance has the content once", [] {
				Placed scene;
				auto copy = scene.node->clone();
				auto copyInstance = copy->component<BindSnapInstance>(false, false);
				check_true(copyInstance && copyInstance->built() && copyInstance->source() == scene.file.path, "the clone is linked to the same file");
				check_true(std::distance(copy->begin(), copy->end()) == 1 && copy->components<Sprite>(false).size() == 1, "with one label and one sprite");
				check_true(!copy->get("Label", false)->serializable(), "that are the file's, not saved");

				scene.label()->position(MV::point(50.0f, 60.0f));
				auto changedCopy = scene.node->clone();
				check_true(at(changedCopy->get("Label", false), 50.0f, 60.0f), "and a clone carries what the instance had changed");
			});
		}
	};

}

using mv_tests_bindsnap_instance_suite = mv_tests::bindsnap_instance_suite;
FOUNDRY_REGISTER(mv_tests_bindsnap_instance_suite)
