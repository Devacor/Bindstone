#ifndef _WORKBENCH_DOCK_H_
#define _WORKBENCH_DOCK_H_

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "MV/Render/package.h"
#include "theme.h"
#include "focus.h"

namespace Workbench {

	class DockSpace;

	class Panel {
	public:
		explicit Panel(std::string a_title, bool a_transparentWhenActive = false)
			: panelTitle(std::move(a_title)), transparent(a_transparentWhenActive) {
		}
		virtual ~Panel() = default;

		const std::string& title() const { return panelTitle; }
		bool transparentWhenActive() const { return transparent; }
		std::shared_ptr<MV::Scene::Node> content() const { return contentNode; }

		void dockAttach(const std::shared_ptr<MV::Scene::Node>& a_content, const std::shared_ptr<MV::Scene::Node>& a_popups, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus) {
			contentNode = a_content;
			popupNode = a_popups;
			services = &a_services;
			theme = &a_theme;
			focus = &a_focus;
			build();
		}

		// Overlay layer above the dock chrome: color pickers and other popups escape clipping here.
		std::shared_ptr<MV::Scene::Node> popups() const { return popupNode; }

		virtual void resized(const MV::Size<>& a_size) { (void)a_size; }
		virtual void update(double a_dt) { (void)a_dt; }
		virtual void handleInput(SDL_Event& a_event) { (void)a_event; }

		// Vertical extent of built content; drives wheel-scroll clamping.
		float contentHeight() const { return builtHeight; }

	protected:
		virtual void build() {}

		float builtHeight = 0.0f;

		std::string panelTitle;
		bool transparent = false;
		std::shared_ptr<MV::Scene::Node> contentNode;
		std::shared_ptr<MV::Scene::Node> popupNode;
		MV::Services* services = nullptr;
		const Theme* theme = nullptr;
		FocusRouter* focus = nullptr;
	};

	struct DockRect {
		MV::Point<> position;
		MV::Size<> size;
		bool contains(const MV::Point<>& a_point) const {
			return a_point.x >= position.x && a_point.y >= position.y &&
				a_point.x <= position.x + size.width && a_point.y <= position.y + size.height;
		}
	};

	// Leaf when panels is non-empty; split when both children are set.
	struct DockNode {
		bool vertical = false;
		float ratio = 0.5f;
		std::unique_ptr<DockNode> a;
		std::unique_ptr<DockNode> b;
		std::vector<Panel*> panels;
		int active = 0;

		bool isLeaf() const { return !a; }
	};

	class DockSpace {
	public:
		enum class DropZone { None, Center, Left, Right, Top, Bottom };

		DockSpace(const std::shared_ptr<MV::Scene::Node>& a_uiRoot, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus);

		Panel* registerPanel(std::shared_ptr<Panel> a_panel);
		static std::unique_ptr<DockNode> leaf(std::vector<Panel*> a_panels);
		static std::unique_ptr<DockNode> split(bool a_vertical, float a_ratio, std::unique_ptr<DockNode> a_a, std::unique_ptr<DockNode> a_b);
		void root(std::unique_ptr<DockNode> a_root);

		void layout(const MV::Size<>& a_worldSize);
		void layout(const DockRect& a_area);
		void update(double a_dt);
		void handleInput(SDL_Event& a_event);

		enum class CursorHint { None, ResizeHorizontal, ResizeVertical };
		// Which resize cursor (if any) belongs at this point — splitter grips report their axis.
		CursorHint cursorHintAt(const MV::Point<>& a_worldPoint) const;

		// True when the given UI-world point sits over the transparent viewport leaf.
		bool viewportAt(const MV::Point<>& a_worldPoint) const;
		// Content rect (below the tab strip) of the transparent viewport leaf; zero-size when unlaid.
		DockRect viewportRect() const;
		// Wheel-scrolls the panel under the point; returns false when no scrollable leaf is there.
		bool wheelScroll(const MV::Point<>& a_worldPoint, float a_amount);

	private:
		void rebuildChrome();
		void layoutNode(DockNode* a_node, const DockRect& a_rect);
		void buildLeafChrome(DockNode* a_leaf);
		void buildSplitterChrome(DockNode* a_split);
		void refreshLeafVisuals(DockNode* a_leaf);
		void activatePanel(DockNode* a_leaf, int a_index);

		void beginTabDrag(Panel* a_panel, DockNode* a_sourceLeaf);
		void updateTabDrag();
		void endTabDrag(bool a_commit);
		DockNode* leafUnder(const MV::Point<>& a_worldPoint, DropZone& a_zone) const;
		DockRect zoneRect(const DockRect& a_leafRect, DropZone a_zone) const;

		void removePanelFromTree(Panel* a_panel);
		DockNode* findLeafContaining(Panel* a_panel, DockNode* a_node) const;
		DockNode* findParent(DockNode* a_child, DockNode* a_node) const;
		void collapseIfEmpty(DockNode* a_leaf);

		std::shared_ptr<MV::Scene::Node> uiRoot;
		std::shared_ptr<MV::Scene::Node> chromeRoot;
		std::shared_ptr<MV::Scene::Node> panelPool;
		std::shared_ptr<MV::Scene::Node> overlayRoot;
		std::shared_ptr<MV::Scene::Node> popupLayer;
		MV::Services& services;
		const Theme& theme;
		FocusRouter& focus;

		std::vector<std::shared_ptr<Panel>> ownedPanels;
		std::unique_ptr<DockNode> rootNode;
		std::map<DockNode*, DockRect> leafRects;
		std::map<DockNode*, DockRect> nodeRects;
		std::map<DockNode*, DockRect> gripRects;
		std::map<DockNode*, std::shared_ptr<MV::Scene::Node>> leafChrome;
		std::map<Panel*, float> scrollOffsets;
		DockRect lastArea;
		bool structureDirty = true;

		struct DragState {
			bool active = false;
			bool passedThreshold = false;
			Panel* panel = nullptr;
			DockNode* sourceLeaf = nullptr;
			MV::Point<int> startScreen;
		};
		DragState drag;
		std::shared_ptr<MV::Scene::Node> dragGhost;
		std::shared_ptr<MV::Scene::Sprite> dropHintSprite;
	};

}

#endif
