#ifndef _WORKBENCH_SCENETREEPANEL_H_
#define _WORKBENCH_SCENETREEPANEL_H_

#include "commands.h"
#include "dock.h"
#include "treeView.h"

namespace Workbench {

	// A hierarchy row: the pointer is identity only (map key, comparison); every read locks the weak handle.
	struct SceneTreeItem {
		SceneTreeItem() = default;
		explicit SceneTreeItem(const std::shared_ptr<MV::Scene::Node>& a_node) : identity(a_node.get()), node(a_node) {}

		bool operator==(const SceneTreeItem& a_other) const { return identity == a_other.identity; }
		bool operator<(const SceneTreeItem& a_other) const { return std::less<const MV::Scene::Node*>{}(identity, a_other.identity); }

		const MV::Scene::Node* identity = nullptr;
		std::weak_ptr<MV::Scene::Node> node;
	};

	class SceneTreePanel : public Panel {
	public:
		SceneTreePanel(CommandHistory& a_history, std::function<std::shared_ptr<MV::Scene::Node>()> a_sceneGetter, std::function<void(const std::shared_ptr<MV::Scene::Node>&)> a_onSelect) :
			Panel("Hierarchy"),
			history(&a_history),
			sceneGetter(std::move(a_sceneGetter)),
			onSelectCallback(std::move(a_onSelect)) {
		}

		void refresh();

		// A row dragged out of the tree and released elsewhere (the inspector's reference fields take these).
		std::function<void(const std::shared_ptr<MV::Scene::Node>&, const MV::Point<int>&)> onNodeDroppedOutside;

		// Highlights and reveals the app's selection; never calls back into the app.
		void showSelection(const std::shared_ptr<MV::Scene::Node>& a_node);

		void resized(const MV::Size<>& a_size) override {
			if (searchSizer) {
				searchSizer->bounds(MV::size(std::max(theme->px(60.0f), a_size.width - theme->pad), theme->fieldHeight));
			}
			if (tree) {
				tree->width(a_size.width);
				builtHeight = theme->fieldHeight + theme->pad + tree->height();
			}
		}

	protected:
		void build() override;

	private:
		CommandHistory* history = nullptr;
		std::function<std::shared_ptr<MV::Scene::Node>()> sceneGetter;
		std::function<void(const std::shared_ptr<MV::Scene::Node>&)> onSelectCallback;
		std::unique_ptr<TreeView<SceneTreeItem>> tree;
		std::shared_ptr<MV::Scene::Node> treeHost;
		std::shared_ptr<MV::Scene::Text> searchText;
		MV::Scene::SafeComponent<MV::Scene::Sprite> searchSizer;
	};

}

#endif
