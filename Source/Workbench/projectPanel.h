#ifndef _WORKBENCH_PROJECTPANEL_H_
#define _WORKBENCH_PROJECTPANEL_H_

#include <filesystem>
#include <optional>
#include "dock.h"
#include "treeView.h"

namespace Workbench {

	// Unity-style Project window over Assets/: folder tree left, tile grid right, breadcrumb
	// top; double-click navigates folders and loads .scene/.bindsnap files into the editor.
	// A footer slider scales the tiles; at its minimum the grid becomes a list. A BindSnap's tile drags out into
	// the scene, and a Hierarchy row let go on the panel is saved into the folder it is over (App::saveAsBindSnap).
	class ProjectPanel : public Panel {
	public:
		ProjectPanel(std::function<void(const std::string&)> a_openScene, std::function<void(const std::string&, const MV::Point<int>&)> a_dropPrefab) :
			Panel("Project"),
			openScene(std::move(a_openScene)),
			dropPrefab(std::move(a_dropPrefab)) {
		}

		void resized(const MV::Size<>& a_size) override;
		void update(double a_dt) override;

		const std::filesystem::path& shownFolder() const { return currentFolder; }
		void showFolder(const std::filesystem::path& a_folder) { navigate(a_folder); }
		// Reads the folders and the shown folder's files again, after something wrote or removed one.
		void refresh();
		// The folder something let go at a point over this panel goes into: a folder's row in the tree or its
		// tile, else the folder shown.
		std::string folderAt(const MV::Point<int>& a_screenPoint) const;
		// Marks that folder while a drag is over it: its row, its tile, or all of the tiles for the folder shown.
		// Empty clears it.
		void markDropFolder(const std::string& a_folder);
		// A place among the tiles away from the first of them, and the middle of a file's or folder's tile by name.
		MV::Point<int> tileAreaScreenPoint() const;
		std::optional<MV::Point<int>> entryScreenPoint(const std::string& a_name) const;

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
		// How far the dock has wheel-scrolled the content.
		float scrolled() const { return theme->tabHeight + theme->pad - contentNode->position().y; }

		struct ShownEntry {
			std::filesystem::path path;
			bool folder = false;
			std::weak_ptr<MV::Scene::Node> node;
			std::weak_ptr<MV::Scene::Node> dropMark;
		};
		std::vector<ShownEntry> shownEntries;
		std::shared_ptr<MV::Scene::Node> dropAreaMark;
		float tileSizeFor() const { return theme->px(48.0f + 112.0f * tilePercent); }
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
