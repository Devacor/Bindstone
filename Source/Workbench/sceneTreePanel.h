#ifndef _WORKBENCH_SCENETREEPANEL_H_
#define _WORKBENCH_SCENETREEPANEL_H_

#include "commands.h"
#include "dock.h"
#include "treeView.h"

namespace Workbench {

	class SceneTreePanel : public Panel {
	public:
		SceneTreePanel(CommandHistory& a_history, std::function<std::shared_ptr<MV::Scene::Node>()> a_sceneGetter, std::function<void(const std::shared_ptr<MV::Scene::Node>&)> a_onSelect) :
			Panel("Hierarchy"),
			history(&a_history),
			sceneGetter(std::move(a_sceneGetter)),
			onSelectCallback(std::move(a_onSelect)) {
		}

		void refresh();
		std::shared_ptr<MV::Scene::Node> selected() const;

		void selectNode(const std::shared_ptr<MV::Scene::Node>& a_node) {
			if (tree && a_node) {
				tree->select(a_node.get());
			}
		}

		void resized(const MV::Size<>& a_size) override {
			if (searchSizer) {
				searchSizer->bounds(MV::size(std::max(60.0f, a_size.width - theme->pad), theme->fieldHeight));
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
		std::unique_ptr<TreeView<MV::Scene::Node*>> tree;
		std::shared_ptr<MV::Scene::Node> treeHost;
		std::shared_ptr<MV::Scene::Text> searchText;
		MV::Scene::SafeComponent<MV::Scene::Sprite> searchSizer;
	};

}

#endif
