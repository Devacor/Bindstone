#ifndef _WORKBENCH_PROJECTPANEL_H_
#define _WORKBENCH_PROJECTPANEL_H_

#include <filesystem>
#include "dock.h"
#include "treeView.h"

namespace Workbench {

	// Unity-style Project window over Assets/: folder tree left, tile grid right, breadcrumb
	// top; double-click navigates folders and loads .scene/.bindsnap files into the editor.
	// A footer slider scales the tiles; at its minimum the grid becomes a list.
	class ProjectPanel : public Panel {
	public:
		ProjectPanel(std::function<void(const std::string&)> a_openScene, std::function<void(const std::string&, const MV::Point<int>&)> a_dropPrefab) :
			Panel("Project"),
			openScene(std::move(a_openScene)),
			dropPrefab(std::move(a_dropPrefab)) {
		}

		void resized(const MV::Size<>& a_size) override;
		void update(double a_dt) override;

	protected:
		void build() override;

	private:
		void navigate(const std::filesystem::path& a_folder);
		void openImagePreview(const std::string& a_fileName);
		void rebuildBreadcrumb();
		void rebuildTiles();
		void buildFooter();
		void layoutFooter();
		void wireEntryInteractions(const std::shared_ptr<MV::Scene::Clickable>& a_click, const std::filesystem::path& a_target, bool a_isFolder, bool a_isPrefab);
		float footerHeight() const { return theme->rowHeight + theme->pad; }
		float tileSizeFor() const { return (32.0f + 80.0f * tilePercent) * theme->scaleFactor(); }
		bool listModeFor() const { return tilePercent < 0.12f; }

		std::function<void(const std::string&)> openScene;
		std::function<void(const std::string&, const MV::Point<int>&)> dropPrefab;
		std::unique_ptr<TreeView<std::string>> folderTree;
		std::shared_ptr<MV::Scene::Node> breadcrumbHost;
		std::shared_ptr<MV::Scene::Node> tileHost;
		std::shared_ptr<MV::Scene::Node> footerHost;
		std::filesystem::path currentFolder = "Assets";
		MV::Size<> panelSize{ 600.0f, 200.0f };
		float treeWidth = 170.0f;
		float tilePercent = 0.4f;   // mix(32, 112) at 0.4 = the original 64px tiles
		int lastBuiltTileSize = 0;
		bool lastBuiltListMode = false;
	};

}

#endif
