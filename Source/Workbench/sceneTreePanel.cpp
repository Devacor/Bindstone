#include "sceneTreePanel.h"
#include <iostream>

namespace Workbench {

	void SceneTreePanel::build() {
		searchSizer = contentNode->attach<MV::Scene::Sprite>();
		searchSizer->id("searchSizer");
		searchSizer->bounds(MV::size(theme->px(200.0f), theme->fieldHeight));
		searchSizer->hide();

		auto searchHost = contentNode->make("search");
		searchText = makeTextField(searchHost, *services, *theme, *focus, "query", MV::size(theme->px(200.0f), theme->fieldHeight), "", nullptr);
		auto fullAnchor = MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(1.0f, 1.0f));
		if (auto background = searchText->owner()->component<MV::Scene::Sprite>(true, false)) {
			background->anchors().anchor(fullAnchor).parent(searchSizer.self());
		}
		if (auto fill = searchText->owner()->component<MV::Scene::Sprite>("fill", false)) {
			const float line = theme->px(1.0f);
			fill->anchors().anchor(fullAnchor).offset(MV::BoxAABB<>(MV::point(line, line), MV::point(-line, -line))).parent(searchSizer.self());
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

		TreeView<SceneTreeItem>::Adapter adapter;
		adapter.children = [](const SceneTreeItem& a_item) {
			std::vector<SceneTreeItem> result;
			if (auto node = a_item.node.lock()) {
				for (auto&& child : *node) {
					result.emplace_back(child);
				}
			}
			return result;
		};
		adapter.label = [](const SceneTreeItem& a_item) {
			auto node = a_item.node.lock();
			return !node || node->id().empty() ? std::string("(node)") : node->id();
		};
		adapter.hasChildren = [](const SceneTreeItem& a_item) {
			auto node = a_item.node.lock();
			return node && !node->empty();
		};
		tree = std::make_unique<TreeView<SceneTreeItem>>(treeHost, *services, *theme, adapter);
		tree->onSelect = [this](const SceneTreeItem& a_item) {
			auto node = a_item.node.lock();
			if (!node) {
				refresh();
				return;
			}
			builtHeight = theme->fieldHeight + theme->pad + tree->height();
			if (onSelectCallback) {
				onSelectCallback(node);
			}
		};
		tree->onReparent = [this](const SceneTreeItem& a_draggedItem, const SceneTreeItem& a_targetItem, TreeView<SceneTreeItem>::RowZone a_zone) {
			auto scene = sceneGetter();
			auto dragged = a_draggedItem.node.lock();
			auto target = a_targetItem.node.lock();
			if (!scene || !dragged || !target || dragged == scene) {
				refresh();
				return;
			}
			for (auto ancestor = target; ancestor; ancestor = ancestor->parent()) {
				if (ancestor == dragged) {
					return;
				}
			}
			auto oldParent = dragged->parent() ? dragged->parent() : scene;
			float oldDepth = dragged->depth();
			auto zone = a_zone;
			auto performMove = [dragged, target, scene, zone]() {
				if (zone == TreeView<SceneTreeItem>::RowZone::Onto) {
					dragged->parent(target);
				} else {
					auto newParent = target->parent() ? target->parent() : scene;
					if (dragged->parent() != newParent) {
						dragged->parent(newParent);
					}
					dragged->depth(target->depth() + (zone == TreeView<SceneTreeItem>::RowZone::Before ? -0.5f : 0.5f));
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
		auto scene = sceneGetter();
		tree->retainState([&scene](const SceneTreeItem& a_item) {
			for (auto node = a_item.node.lock(); node; node = node->parent()) {
				if (node == scene) {
					return true;
				}
			}
			return false;
		});
		if (scene) {
			tree->root(SceneTreeItem(scene), true);
		} else {
			tree->rebuild();
		}
		builtHeight = theme->fieldHeight + theme->pad + tree->height();
	}

	void SceneTreePanel::showSelection(const std::shared_ptr<MV::Scene::Node>& a_node) {
		if (!tree) {
			return;
		}
		if (!a_node) {
			tree->showSelection(nullptr);
			return;
		}
		auto scene = sceneGetter();
		bool revealed = false;
		for (auto ancestor = a_node->parent(); ancestor; ancestor = ancestor->parent()) {
			revealed = tree->expand(SceneTreeItem(ancestor)) || revealed;
			if (ancestor == scene) {
				break;
			}
		}
		SceneTreeItem item(a_node);
		bool alreadyShown = tree->selection() && *tree->selection() == item;
		tree->showSelection(&item);
		if (revealed && alreadyShown) {
			tree->rebuild();
		}
		builtHeight = theme->fieldHeight + theme->pad + tree->height();
	}

}
