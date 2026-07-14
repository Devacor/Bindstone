#include "sceneTreePanel.h"
#include <iostream>

namespace Workbench {

	void SceneTreePanel::build() {
		searchSizer = contentNode->attach<MV::Scene::Sprite>();
		searchSizer->id("searchSizer");
		searchSizer->bounds(MV::size(200.0f, theme->fieldHeight));
		searchSizer->hide();

		auto searchHost = contentNode->make("search");
		searchText = makeTextField(searchHost, *services, *theme, *focus, "query", MV::size(200.0f, theme->fieldHeight), "", nullptr);
		auto fullAnchor = MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(1.0f, 1.0f));
		if (auto background = searchText->owner()->component<MV::Scene::Sprite>(true, false)) {
			background->anchors().anchor(fullAnchor).parent(searchSizer.self());
		}
		if (auto clickable = searchText->owner()->component<MV::Scene::Clickable>(true, false)) {
			clickable->anchors().anchor(fullAnchor).parent(searchSizer.self());
		}
		searchText->anchors().anchor(fullAnchor).parent(searchSizer.self());
		auto searchField = searchText;
		std::weak_ptr<MV::Scene::Text> weakSearch = searchField;
		searchField->onChange.connect("liveFilter", [this, weakSearch](std::shared_ptr<MV::Scene::Text>) {
			if (auto locked = weakSearch.lock(); locked && tree) {
				tree->filter(locked->text());
				builtHeight = theme->fieldHeight + theme->pad + tree->height();
			}
		});
		treeHost = contentNode->make("rows");
		treeHost->position(MV::point(0.0f, theme->fieldHeight + theme->pad));

		TreeView<MV::Scene::Node*>::Adapter adapter;
		adapter.children = [](MV::Scene::Node* const& a_node) {
			std::vector<MV::Scene::Node*> result;
			for (auto&& child : *a_node) {
				result.push_back(child.get());
			}
			return result;
		};
		adapter.label = [](MV::Scene::Node* const& a_node) {
			return a_node->id().empty() ? std::string("(node)") : a_node->id();
		};
		adapter.hasChildren = [](MV::Scene::Node* const& a_node) {
			return !a_node->empty();
		};
		tree = std::make_unique<TreeView<MV::Scene::Node*>>(treeHost, *services, *theme, adapter);
		tree->onSelect = [this](MV::Scene::Node* const& a_node) {
			builtHeight = theme->fieldHeight + theme->pad + tree->height();
			if (onSelectCallback) {
				onSelectCallback(a_node->shared_from_this());
			}
		};
		tree->onReparent = [this](MV::Scene::Node* const& a_dragged, MV::Scene::Node* const& a_target, TreeView<MV::Scene::Node*>::RowZone a_zone) {
			auto scene = sceneGetter();
			if (!scene || a_dragged == scene.get()) {
				return;
			}
			for (auto ancestor = a_target; ancestor; ancestor = ancestor->parent().get()) {
				if (ancestor == a_dragged) {
					return;
				}
			}
			auto dragged = a_dragged->shared_from_this();
			auto target = a_target->shared_from_this();
			auto oldParent = dragged->parent() ? dragged->parent() : scene;
			float oldDepth = dragged->depth();
			auto zone = a_zone;
			auto performMove = [dragged, target, scene, zone]() {
				if (zone == TreeView<MV::Scene::Node*>::RowZone::Onto) {
					dragged->parent(target);
				} else {
					auto newParent = target->parent() ? target->parent() : scene;
					if (dragged->parent() != newParent) {
						dragged->parent(newParent);
					}
					dragged->depth(target->depth() + (zone == TreeView<MV::Scene::Node*>::RowZone::Before ? -0.5f : 0.5f));
					newParent->normalizeDepth();
				}
			};
			try {
				performMove();
				history->record("reparent " + (dragged->id().empty() ? std::string("(node)") : dragged->id()),
					performMove,
					[dragged, oldParent, oldDepth]() {
						dragged->parent(oldParent);
						dragged->depth(oldDepth);
						oldParent->normalizeDepth();
					});
			} catch (const std::exception& a_error) {
				std::cerr << "Workbench reparent failed: " << a_error.what() << std::endl;
			}
			refresh();
		};
		refresh();
	}

	void SceneTreePanel::refresh() {
		if (!tree) {
			return;
		}
		if (auto scene = sceneGetter()) {
			tree->root(scene.get(), true);
		} else {
			tree->rebuild();
		}
		builtHeight = theme->fieldHeight + theme->pad + tree->height();
	}

	std::shared_ptr<MV::Scene::Node> SceneTreePanel::selected() const {
		if (tree) {
			if (auto* nodePtr = tree->selection()) {
				return (*nodePtr)->shared_from_this();
			}
		}
		return nullptr;
	}

}
