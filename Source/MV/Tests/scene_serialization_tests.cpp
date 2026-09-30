// Bindstone (MV) scene save/load regression tests: each test pins one load-path bug
// (glyph-container orphaning, serializable filter depth, postLoadStep idempotence, Button
// view $ref order, Stencil/Palette slicing, PathMap load overload, one-level clone, inert
// persisted script receivers). Headless; built into mv_tests.exe (see build_tests.bat).

#include <jaiscript/testing/foundry.hpp>
#include "mv_scene_host.hpp"
#include "MV/Render/Scene/text.h"
#include "MV/Render/Scene/sprite.h"
#include "MV/Render/Scene/button.h"
#include "MV/Render/Scene/clickable.h"
#include "MV/Render/Scene/stencil.h"
#include "MV/Render/Scene/palette.h"
#include "MV/Render/Scene/path.h"
#include "MV/Render/Scene/grid.h"
#include <cstdlib>

#include <string>
#include <iostream>

using namespace jai::foundry;
using MV::Scene::Node;

namespace mv_tests {

	struct AnchorProbe : MV::Scene::Drawable {
		static size_t registeredChildAnchors(MV::Scene::Drawable& a_drawable) {
			return (a_drawable.*(&AnchorProbe::childAnchors)).size();
		}
	};

	static std::shared_ptr<Node> makeTextScene(SceneHost& a_host) {
		a_host.loadFont("test");
		auto scene = Node::make(a_host.renderer, "root");
		auto frame = scene->make("frame");
		frame->attach<MV::Scene::Sprite>()->id("panel")->bounds(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::size(200.0f, 100.0f)));
		auto label = frame->make("label");
		label->attach<MV::Scene::Text>(a_host.textLibrary, std::string("test"))->text("Login");
		auto badge = label->attach<MV::Scene::Sprite>()->id("badge")->bounds(MV::BoxAABB<>(MV::point(10.0f, 20.0f), MV::size(30.0f, 40.0f)));
		badge->anchors().parent(frame->component<MV::Scene::Sprite>().self(), true);
		return scene;
	}

	class scene_serialization_suite : public suite {
	public:
		scene_serialization_suite() : suite("MV Scene Serialization") {}

		void forge_tests() override {
			test("loaded Text reattaches its glyph container and cursor under the owner", [] {
				SceneHost host;
				auto scene = makeTextScene(host);
				ScratchScene file;
				auto loaded = saveAndLoad(host, scene, file);

				auto label = loaded->get("label");
				auto text = label->component<MV::Scene::Text>(true, false);
				check_not_null(text.self(), "Text component missing after load");
				check_eq(std::string("Login"), std::string(text->text()));
				check_eq(size_t(1), childrenWithPrefix(label, "TEXT_"), "glyph container count");
				check_eq(size_t(1), childrenWithPrefix(label, "CURSOR_"), "cursor node count");
				check_true(childWithPrefix(label, "TEXT_")->parent() == label, "glyph container parent is the owner");
				check_true(childWithPrefix(label, "CURSOR_")->parent() == label, "cursor parent is the owner");
			});

			test("re-saving a loaded scene never writes TEXT_/CURSOR_ nodes at any depth", [] {
				SceneHost host;
				auto scene = makeTextScene(host);
				ScratchScene first;
				auto loaded = saveAndLoad(host, scene, first);

				ScratchScene second;
				loaded->saveJai(second.path, host.services, false);
				auto json = second.contents();
				check_true(json.find("\"label\"") != std::string::npos, "nested node was saved");
				check_true(json.find("TEXT_") == std::string::npos, "runtime glyph container leaked into the save");
				check_true(json.find("CURSOR_") == std::string::npos, "runtime cursor node leaked into the save");
			});

			test("postLoadStep is idempotent for glyph attachment and anchor registration", [] {
				SceneHost host;
				auto scene = makeTextScene(host);
				ScratchScene file;
				auto loaded = saveAndLoad(host, scene, file);

				auto label = loaded->get("label");
				auto panel = loaded->get("frame")->component<MV::Scene::Sprite>(true, false);
				check_not_null(panel.self(), "anchor parent missing after load");
				auto labelChildren = label->size();
				auto rootChildren = loaded->size();
				check_eq(size_t(1), AnchorProbe::registeredChildAnchors(*panel.self()), "one anchor registered by the load");

				for (int pass = 0; pass < 3; ++pass) {
					loaded->postLoadStep();
				}
				check_eq(labelChildren, label->size(), "label child count drifted");
				check_eq(rootChildren, loaded->size(), "root child count drifted");
				check_eq(size_t(1), childrenWithPrefix(label, "TEXT_"), "glyph container duplicated");
				check_eq(size_t(1), childrenWithPrefix(label, "CURSOR_"), "cursor duplicated");
				check_eq(size_t(1), AnchorProbe::registeredChildAnchors(*panel.self()), "anchor registered more than once");
			});

			test("Button child views survive the childComponents-before-childNodes wire order", [] {
				SceneHost host;
				auto scene = Node::make(host.renderer, "root");
				auto buttonNode = scene->make("button");
				auto active = buttonNode->make("active");
				auto idle = buttonNode->make("idle");
				auto disabled = buttonNode->make("disabled");
				buttonNode->attach<MV::Scene::Button>(host.mouse)->activeNode(active)->idleNode(idle)->disabledNode(disabled);

				ScratchScene file;
				auto loaded = saveAndLoad(host, scene, file);
				auto loadedNode = loaded->get("button");
				check_eq(size_t(3), loadedNode->size(), "child view count");
				auto button = loadedNode->component<MV::Scene::Button>(true, false);
				check_not_null(button.self(), "Button missing after load");
				check_not_null(button->activeNode(), "activeView null");
				check_not_null(button->idleNode(), "idleView null");
				check_not_null(button->disabledNode(), "disabledView null");
				check_true(button->activeNode() == loaded->get("active"), "activeView is not the child node");
				check_true(button->idleNode() == loaded->get("idle"), "idleView is not the child node");
				check_true(button->disabledNode() == loaded->get("disabled"), "disabledView is not the child node");
			});

			test("Stencil and Palette save and load as their own $type", [] {
				SceneHost host;
				auto scene = Node::make(host.renderer, "root");
				scene->attach<MV::Scene::Stencil>()->id("mask");
				scene->attach<MV::Scene::Palette>(host.mouse)->id("colors");

				ScratchScene file;
				auto loaded = saveAndLoad(host, scene, file);
				auto json = file.contents();
				check_true(hasSerializedType(json, "Stencil"), "Stencil saved as a sliced base type");
				check_true(hasSerializedType(json, "Palette"), "Palette saved as a sliced base type");
				auto stencil = loaded->component<MV::Scene::Stencil>(true, false);
				auto palette = loaded->component<MV::Scene::Palette>(true, false);
				check_not_null(stencil.self(), "Stencil did not load as Stencil");
				check_not_null(palette.self(), "Palette did not load as Palette");
				check_eq(std::string("mask"), stencil->id());
				check_eq(std::string("colors"), palette->id());
			});

			test("reflected Point/Size/Color properties are script vectors writing the live member, both backends", [] {
				for (bool interpreter : { false, true }) {
					_putenv(interpreter ? "MV_SCRIPT_INTERPRETER=1" : "MV_SCRIPT_INTERPRETER=");
					SceneHost host;
					auto scene = Node::make(host.renderer, "root");
					auto grid = scene->make("grid")->attach<MV::Scene::Grid>().self();
					auto palette = scene->make("palette")->attach<MV::Scene::Palette>(host.mouse).self();
					host.engine->add_global("g", host.engine->make_value(grid));
					host.engine->add_global("p", host.engine->make_value(palette));
					host.engine->add_global("n", host.engine->make_value(scene));
					const std::string label = interpreter ? " (interpreter)" : " (vm)";
					check_eq(std::string("math::vec3 math::vec3 math::vec3 math::vec3"),
						host.engine->execute("type_of(g.topLeftOffset) + \" \" + type_of(g.cellDimensions) + \" \" + type_of(n.translateTo) + \" \" + type_of(p.selectorPercentSize)").to_string(), "types" + label);
					host.engine->execute("g.topLeftOffset = math::vec3(1, 2, 3); g.topLeftOffset.x = 5; g.topLeftOffset.y += 10; g.topLeftOffset.z++; "
						"g.cellDimensions.width = 4; g.cellDimensions.height += 2; n.translateTo.x = 7; n.translateTo.y -= 3; p.selectorPercentSize.width *= 2;");
					const MV::Point<> offset = grid->gridOffset();
					check_true(offset.x == 5.0f && offset.y == 12.0f && offset.z == 4.0f, "whole, component and compound stores reach topLeftOffset" + label);
					check_true(grid->cellSize().width == 4.0f && grid->cellSize().height == 2.0f, "cellDimensions (width/height aliases)" + label);
					check_true(scene->position().x == 7.0f && scene->position().y == -3.0f, "a node property" + label);
					check_eq(std::string("true"), host.engine->execute("to_string(math::abs(p.selectorPercentSize.width - 1.7) < 0.0001)").to_string(), "a private palette property reads back the store" + label);
					host.engine->execute("p.color(math::color(1, 0.25, 0, 1));");
					check_eq(std::string("math::color (1.0, 0.25, 0.0, 1.0)"), host.engine->execute("type_of(p.color()) + \" \" + to_string(p.color())").to_string(), "a property named like its method is reached through the method" + label);
				}
				_putenv("MV_SCRIPT_INTERPRETER=");
			});

			test("PathMap keeps its Drawable-level state through save and load", [] {
				SceneHost host;
				auto scene = Node::make(host.renderer, "root");
				auto pathMap = scene->make("paths")->attach<MV::Scene::PathMap>(MV::Size<>(10.0f, 5.0f), MV::Size<int>(4, 3))->id("grid");
				pathMap->blend(MV::BLEND_ADD);

				ScratchScene file;
				auto loaded = saveAndLoad(host, scene, file);
				auto loadedMap = loaded->get("paths")->component<MV::Scene::PathMap>(true, false);
				check_not_null(loadedMap.self(), "PathMap missing after load");
				check_eq(std::string("grid"), loadedMap->id(), "componentId lost");
				check_eq(4, loadedMap->gridSize().width);
				check_eq(3, loadedMap->gridSize().height);
				check_near(10.0f, loadedMap->cellSize().width, 1e-5f);
				check_near(5.0f, loadedMap->cellSize().height, 1e-5f);
				check_eq(static_cast<int>(MV::BLEND_ADD), static_cast<int>(loadedMap->blend()), "blendMode lost");
				check_eq(size_t(4 * 12), loadedMap->pointSize(), "debug points not rebuilt from the grid");
				check_eq(size_t(6 * 12), loadedMap->pointIndices().size(), "vertexIndices not rebuilt from the grid");
			});

			test("Sprite clone copies the whole property chain", [] {
				SceneHost host;
				auto scene = Node::make(host.renderer, "root");
				auto panel = scene->make("frame")->attach<MV::Scene::Sprite>()->id("panel")->bounds(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::size(200.0f, 100.0f)));
				auto sprite = scene->make("holder")->attach<MV::Scene::Sprite>()->id("badge")->bounds(MV::BoxAABB<>(MV::point(10.0f, 20.0f), MV::size(30.0f, 40.0f)))->subdivide(2);
				sprite->blend(MV::BLEND_ADD);
				sprite->anchors().parent(panel, true);
				check_true(sprite->anchors().usePosition(), "source anchor applies position");

				auto copy = sprite->clone(scene->make("target"));
				check_not_null(copy.self(), "clone returned null");
				check_eq(std::string("badge"), copy->id(), "componentId not cloned");
				check_eq(2, static_cast<int>(copy->subdivisions()), "subdivisions not cloned");
				check_eq(static_cast<int>(MV::BLEND_ADD), static_cast<int>(copy->blend()), "blendMode not cloned");
				check_true(copy->anchors().usePosition(), "anchors applyingPosition dropped by clone");
				check_eq(sprite->pointSize(), copy->pointSize(), "points not cloned");
				check_near(sprite->bounds().minPoint.x, copy->bounds().minPoint.x, 1e-3f);
				check_near(sprite->bounds().minPoint.y, copy->bounds().minPoint.y, 1e-3f);
				check_near(sprite->bounds().maxPoint.x, copy->bounds().maxPoint.x, 1e-3f);
				check_near(sprite->bounds().maxPoint.y, copy->bounds().maxPoint.y, 1e-3f);
			});

			test("loaded Clickable script receiver is armed and fires on press", [] {
				SceneHost host;
				int pressCount = 0;
				host.engine->add_function("notePress", [&pressCount]() -> int { return ++pressCount; });
				auto scene = Node::make(host.renderer, "root");
				auto clickable = scene->make("button")->attach<MV::Scene::Clickable>(host.mouse)->id("click");
				clickable->onAccept.connect("script", std::string("notePress();"));

				ScratchScene file;
				auto loaded = saveAndLoad(host, scene, file);
				auto loadedClickable = loaded->get("button")->component<MV::Scene::Clickable>(true, false);
				check_not_null(loadedClickable.self(), "Clickable missing after load");
				check_true(loadedClickable->onAccept.connected("script"), "owned script receiver not restored");
				check_eq(0, pressCount);
				loadedClickable->press();
				check_eq(1, pressCount, "persisted script receiver loaded inert (no engine bound at load)");
			});
		}
	};

}

using scene_serialization_suite = mv_tests::scene_serialization_suite;
FOUNDRY_REGISTER(scene_serialization_suite)
