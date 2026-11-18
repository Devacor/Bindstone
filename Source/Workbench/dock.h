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
		// Where a dragged tab lands on a panel: its tab bar (Center) joins it as a tab, an edge splits it, and the
		// middle (Float) leaves the tab floating there.
		enum class DropZone { None, Center, Left, Right, Top, Bottom, Float };

		DockSpace(const std::shared_ptr<MV::Scene::Node>& a_uiRoot, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus);

		Panel* registerPanel(std::shared_ptr<Panel> a_panel);
		static std::unique_ptr<DockNode> leaf(std::vector<Panel*> a_panels);
		static std::unique_ptr<DockNode> split(bool a_vertical, float a_ratio, std::unique_ptr<DockNode> a_a, std::unique_ptr<DockNode> a_b);
		void root(std::unique_ptr<DockNode> a_root);
		// Brings a panel's tab to the front of its leaf, and brings a closed panel back: beside the panel it was
		// closed next to, or floating when that one is gone.
		void show(Panel* a_panel);
		// Takes a panel out of the layout until it is shown again. False for the last panel docked in the main area.
		bool closePanel(Panel* a_panel);
		bool open(Panel* a_panel) const;
		bool floating(Panel* a_panel) const;
		// Lifts a panel out of its leaf to float over where it was. False for the last panel docked in the main
		// area and for one already floating by itself.
		bool floatPanel(Panel* a_panel);
		bool canFloat(Panel* a_panel) const;
		bool canClose(Panel* a_panel) const;
		// The panel whose tab a node is part of; null for a node of anything else.
		Panel* tabPanelOf(const std::shared_ptr<MV::Scene::Node>& a_node) const;
		Panel* panelTitled(const std::string& a_title) const;
		// The content node of the panel a node is in, for keeping Tab inside one panel; null outside every panel.
		std::shared_ptr<MV::Scene::Node> panelContentContaining(const std::shared_ptr<MV::Scene::Node>& a_node) const;
		// Floating panels are drawn in this node, so they sit above whatever joined the UI before it.
		void floatingHost(const std::shared_ptr<MV::Scene::Node>& a_host);

		// The arrangement as one line of text: each split with its ratio, each leaf with its tabs by title and
		// which one is in front. "H 0.18 (L 0 [Hierarchy,File]) (L 0 [Scene])".
		std::string describeLayout() const;
		// Rebuilds the arrangement from such a line. A title it does not know is skipped and a panel it does not
		// name joins the first leaf, so every panel is always somewhere. False, changing nothing, when the text
		// does not read as a layout.
		bool applyLayout(const std::string& a_description);
		// After a tab is dropped somewhere new, a splitter is let go, or another tab is brought to the front.
		std::function<void()> onLayoutChanged;

		// Where on screen a panel's tab is, the middle of a drop zone of the leaf a panel is in, and the splitter
		// of the outermost split.
		MV::Point<int> tabScreenPoint(const std::string& a_panelTitle) const;
		MV::Point<int> dropScreenPoint(const std::string& a_panelTitle, DropZone a_zone) const;
		MV::Point<int> rootSplitterScreenPoint() const;
		// A panel's tab close button, and for a floating panel the bare part of its tab bar (which moves it), its
		// resize corner and its rectangle in world units.
		MV::Point<int> closeButtonScreenPoint(const std::string& a_panelTitle) const;
		MV::Point<int> floatBarScreenPoint(const std::string& a_panelTitle) const;
		MV::Point<int> floatCornerScreenPoint(const std::string& a_panelTitle) const;
		DockRect floatRect(const std::string& a_panelTitle) const;

		void layout(const MV::Size<>& a_worldSize);
		void layout(const DockRect& a_area);
		void update(double a_dt);
		void handleInput(SDL_Event& a_event);

		enum class CursorHint { None, ResizeHorizontal, ResizeVertical };
		// Which resize cursor (if any) belongs at this point — splitter grips report their axis.
		CursorHint cursorHintAt(const MV::Point<>& a_worldPoint) const;

		// The panel in front at a point, below its tab bar; null on a tab bar or off every panel.
		Panel* panelShowingAt(const MV::Point<>& a_worldPoint) const;
		// True when the given UI-world point sits over the transparent viewport leaf.
		bool viewportAt(const MV::Point<>& a_worldPoint) const;
		// Content rect (below the tab strip) of the transparent viewport leaf; zero-size when unlaid.
		DockRect viewportRect() const;
		// Wheel-scrolls the panel under the point; returns false when no scrollable leaf is there.
		bool wheelScroll(const MV::Point<>& a_worldPoint, float a_amount);

	private:
		void rebuildChrome();
		void layoutNode(DockNode* a_node, const DockRect& a_rect);
		void buildLeafChrome(DockNode* a_leaf, const std::shared_ptr<MV::Scene::Node>& a_parent);
		void buildSplitterChrome(DockNode* a_split);
		void refreshLeafVisuals(DockNode* a_leaf);
		// Clamps the active panel's scroll offset to its content and sizes the leaf's scroll thumb to match.
		void syncScroll(DockNode* a_leaf);
		float scrollGutter() const { return theme.px(10.0f); }
		float viewHeightOf(const DockRect& a_leafRect) const { return a_leafRect.size.height - theme.tabHeight - theme.pad * 2.0f; }
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
		void layoutChanged();

		// A leaf held over the dock at a rectangle given in fractions of the dock's area.
		struct FloatingLeaf {
			std::unique_ptr<DockNode> leaf;
			DockRect fraction;
		};
		FloatingLeaf* floatOf(const DockNode* a_leaf);
		const FloatingLeaf* floatOf(const DockNode* a_leaf) const;
		DockNode* leafContaining(Panel* a_panel) const;
		void buildFloatChrome(FloatingLeaf& a_float);
		void layoutFloat(FloatingLeaf& a_float);
		DockRect fractionAround(const MV::Point<>& a_worldPoint) const;
		DockRect worldRectOf(const DockRect& a_fraction) const;
		void raise(DockNode* a_leaf);
		// Takes the panel out of its leaf and tidies what that leaves behind; the panel's content waits in the pool.
		void detach(Panel* a_panel);

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
		std::vector<FloatingLeaf> floats;
		std::vector<Panel*> closedPanels;
		std::map<Panel*, Panel*> closedBeside;
		std::shared_ptr<MV::Scene::Node> floatRoot;
		float raisedDepth = 0.0f;
		std::map<DockNode*, DockRect> leafRects;
		std::map<DockNode*, DockRect> nodeRects;
		std::map<DockNode*, DockRect> gripRects;
		std::map<DockNode*, std::shared_ptr<MV::Scene::Node>> leafChrome;
		std::map<Panel*, float> scrollOffsets;
		std::map<Panel*, std::weak_ptr<MV::Scene::Node>> tabNodes;
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
