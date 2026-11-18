#include "dock.h"
#include "widgets.h"
#include "icons.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace Workbench {

	namespace {
		float lengthBetween(const MV::Point<int>& a_a, const MV::Point<int>& a_b) {
			float dx = static_cast<float>(a_a.x - a_b.x);
			float dy = static_cast<float>(a_a.y - a_b.y);
			return std::sqrt(dx * dx + dy * dy);
		}
	}

	DockSpace::DockSpace(const std::shared_ptr<MV::Scene::Node>& a_uiRoot, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus) :
		uiRoot(a_uiRoot),
		services(a_services),
		theme(a_theme),
		focus(a_focus) {

		chromeRoot = uiRoot->make("dockChrome");
		panelPool = uiRoot->make("dockPanelPool");
		panelPool->hide();
		overlayRoot = uiRoot->make("dockOverlay");
		popupLayer = overlayRoot->make("popups");

		auto& library = *services.get<MV::TextLibrary>();
		dragGhost = overlayRoot->make("dragGhost");
		attachRoundedRect(dragGhost, services, theme, MV::size(theme.tabWidth, theme.tabHeight), theme.buttonIdle());
		auto ghostLabel = dragGhost->attach<MV::Scene::Text>(library, theme.labelFont);
		ghostLabel->bounds({ MV::Point<>(), MV::size(theme.tabWidth, theme.tabHeight) });
		ghostLabel->justification(MV::TextJustification::CENTER)->wrapping(MV::TextWrapMethod::NONE, theme.tabWidth)->minimumLineHeight(theme.tabHeight);
		dragGhost->hide();

		auto hintNode = overlayRoot->make("dropHint");
		dropHintSprite = hintNode->attach<MV::Scene::Sprite>()->color(theme.dropHint());
		hintNode->hide();
	}

	Panel* DockSpace::registerPanel(std::shared_ptr<Panel> a_panel) {
		auto contentNode = panelPool->make("content_" + a_panel->title());
		a_panel->dockAttach(contentNode, popupLayer, services, theme, focus);
		ownedPanels.push_back(std::move(a_panel));
		return ownedPanels.back().get();
	}

	std::unique_ptr<DockNode> DockSpace::leaf(std::vector<Panel*> a_panels) {
		auto node = std::make_unique<DockNode>();
		node->panels = std::move(a_panels);
		return node;
	}

	std::unique_ptr<DockNode> DockSpace::split(bool a_vertical, float a_ratio, std::unique_ptr<DockNode> a_a, std::unique_ptr<DockNode> a_b) {
		auto node = std::make_unique<DockNode>();
		node->vertical = a_vertical;
		node->ratio = a_ratio;
		node->a = std::move(a_a);
		node->b = std::move(a_b);
		return node;
	}

	void DockSpace::root(std::unique_ptr<DockNode> a_root) {
		rootNode = std::move(a_root);
		structureDirty = true;
	}

	void DockSpace::layout(const MV::Size<>& a_worldSize) {
		layout(DockRect{ MV::Point<>(), a_worldSize });
	}

	void DockSpace::layout(const DockRect& a_area) {
		if (!rootNode) {
			return;
		}
		lastArea = a_area;
		if (structureDirty) {
			rebuildChrome();
			structureDirty = false;
		}
		leafRects.clear();
		nodeRects.clear();
		gripRects.clear();
		layoutNode(rootNode.get(), a_area);
		for (auto&& floatingLeaf : floats) {
			layoutFloat(floatingLeaf);
		}
	}

	void DockSpace::floatingHost(const std::shared_ptr<MV::Scene::Node>& a_host) {
		floatRoot = a_host;
		// the drag ghost, the drop hint and the panels' popups stay above a floating panel
		overlayRoot->depth(a_host->depth() + 0.5f);
		structureDirty = true;
	}

	DockSpace::FloatingLeaf* DockSpace::floatOf(const DockNode* a_leaf) {
		for (auto&& floatingLeaf : floats) {
			if (floatingLeaf.leaf.get() == a_leaf) {
				return &floatingLeaf;
			}
		}
		return nullptr;
	}

	const DockSpace::FloatingLeaf* DockSpace::floatOf(const DockNode* a_leaf) const {
		return const_cast<DockSpace*>(this)->floatOf(a_leaf);
	}

	DockNode* DockSpace::leafContaining(Panel* a_panel) const {
		if (auto* docked = findLeafContaining(a_panel, rootNode.get())) {
			return docked;
		}
		for (auto&& floatingLeaf : floats) {
			if (findLeafContaining(a_panel, floatingLeaf.leaf.get())) {
				return floatingLeaf.leaf.get();
			}
		}
		return nullptr;
	}

	bool DockSpace::open(Panel* a_panel) const {
		return leafContaining(a_panel) != nullptr;
	}

	bool DockSpace::floating(Panel* a_panel) const {
		return floatOf(leafContaining(a_panel)) != nullptr;
	}

	DockRect DockSpace::worldRectOf(const DockRect& a_fraction) const {
		// a floating panel keeps a usable size and never leaves its tab bar outside the dock
		MV::Size<> size(std::clamp(a_fraction.size.width * lastArea.size.width, theme.px(180.0f), std::max(theme.px(180.0f), lastArea.size.width)),
			std::clamp(a_fraction.size.height * lastArea.size.height, theme.px(120.0f), std::max(theme.px(120.0f), lastArea.size.height)));
		MV::Point<> position(lastArea.position.x + a_fraction.position.x * lastArea.size.width, lastArea.position.y + a_fraction.position.y * lastArea.size.height);
		position.x = std::clamp(position.x, lastArea.position.x - size.width + theme.px(60.0f), lastArea.position.x + lastArea.size.width - theme.px(60.0f));
		position.y = std::clamp(position.y, lastArea.position.y, lastArea.position.y + std::max(0.0f, lastArea.size.height - theme.tabHeight));
		return DockRect { MV::point(std::round(position.x), std::round(position.y)), MV::size(std::round(size.width), std::round(size.height)) };
	}

	DockRect DockSpace::fractionAround(const MV::Point<>& a_worldPoint) const {
		const float width = std::max(1.0f, lastArea.size.width);
		const float height = std::max(1.0f, lastArea.size.height);
		DockRect result;
		result.size = MV::size(std::min(0.9f, theme.px(380.0f) / width), std::min(0.9f, theme.px(420.0f) / height));
		// the pointer ends up on the new panel's tab
		result.position = MV::point((a_worldPoint.x - lastArea.position.x - theme.tabWidth * 0.5f) / width, (a_worldPoint.y - lastArea.position.y - theme.tabHeight * 0.5f) / height);
		return result;
	}

	void DockSpace::raise(DockNode* a_leaf) {
		auto found = std::find_if(floats.begin(), floats.end(), [&](const FloatingLeaf& a_float) { return a_float.leaf.get() == a_leaf; });
		if (found == floats.end()) {
			return;
		}
		std::rotate(found, found + 1, floats.end());
		if (auto chrome = leafChrome.find(a_leaf); chrome != leafChrome.end()) {
			chrome->second->depth(++raisedDepth);
		}
	}

	void DockSpace::detach(Panel* a_panel) {
		DockNode* leaf = leafContaining(a_panel);
		if (!leaf) {
			return;
		}
		leaf->panels.erase(std::remove(leaf->panels.begin(), leaf->panels.end(), a_panel), leaf->panels.end());
		if (!leaf->panels.empty()) {
			leaf->active = std::clamp(leaf->active, 0, static_cast<int>(leaf->panels.size()) - 1);
		}
		a_panel->content()->parent(panelPool);
		if (leaf->panels.empty()) {
			if (floatOf(leaf)) {
				leafRects.erase(leaf);
				leafChrome.erase(leaf);
				floats.erase(std::find_if(floats.begin(), floats.end(), [&](const FloatingLeaf& a_float) { return a_float.leaf.get() == leaf; }));
			} else {
				collapseIfEmpty(leaf);
			}
		}
		structureDirty = true;
	}

	bool DockSpace::canClose(Panel* a_panel) const {
		DockNode* leaf = leafContaining(a_panel);
		return leaf && !(leaf == rootNode.get() && leaf->panels.size() == 1);
	}

	bool DockSpace::canFloat(Panel* a_panel) const {
		DockNode* leaf = leafContaining(a_panel);
		return canClose(a_panel) && !(floatOf(leaf) && leaf->panels.size() == 1);
	}

	bool DockSpace::floatPanel(Panel* a_panel) {
		if (!canFloat(a_panel)) {
			return false;
		}
		auto found = leafRects.find(leafContaining(a_panel));
		auto over = found == leafRects.end() ? lastArea : found->second;
		auto fraction = fractionAround(MV::point(over.position.x + over.size.width * 0.5f, over.position.y + over.size.height * 0.5f));
		detach(a_panel);
		floats.push_back({ leaf({ a_panel }), fraction });
		structureDirty = true;
		layout(lastArea);
		layoutChanged();
		return true;
	}

	Panel* DockSpace::tabPanelOf(const std::shared_ptr<MV::Scene::Node>& a_node) const {
		for (auto node = a_node; node; node = node->parent()) {
			for (auto&& [panel, tab] : tabNodes) {
				if (tab.lock() == node) {
					return panel;
				}
			}
		}
		return nullptr;
	}

	bool DockSpace::closePanel(Panel* a_panel) {
		DockNode* leaf = leafContaining(a_panel);
		if (!canClose(a_panel)) {
			return false;
		}
		closedBeside.erase(a_panel);
		for (auto&& neighbour : leaf->panels) {
			if (neighbour != a_panel) {
				closedBeside[a_panel] = neighbour;
				break;
			}
		}
		detach(a_panel);
		closedPanels.push_back(a_panel);
		layout(lastArea);
		layoutChanged();
		return true;
	}

	void DockSpace::buildFloatChrome(FloatingLeaf& a_float) {
		auto& mouse = *services.get<MV::TapDevice>();
		DockNode* leaf = a_float.leaf.get();
		buildLeafChrome(leaf, floatRoot ? floatRoot : chromeRoot);
		auto leafNode = leafChrome[leaf];
		leafNode->depth(++raisedDepth);

		// beneath the leaf's own pieces: a shadow, an outline, and a Clickable that keeps a press on the panel from
		// reaching whatever it covers
		auto shadow = leafNode->make("floatShadow");
		shadow->depth(-3.0f);
		shadow->position(MV::point(theme.px(3.0f), theme.px(4.0f)));
		attachRoundedRect(shadow, services, theme, MV::size(theme.tabWidth, theme.tabHeight), MV::Color(0.0f, 0.0f, 0.0f, 0.45f));
		auto outline = leafNode->make("floatOutline");
		outline->depth(-2.0f);
		outline->position(MV::point(-theme.px(1.0f), -theme.px(1.0f)));
		outline->attach<MV::Scene::Sprite>()->color(theme.fieldOutline());
		auto blocker = leafNode->make("floatBlocker");
		blocker->depth(-1.0f);
		blocker->attach<MV::Scene::Clickable>(mouse)->onPress.connect("raise", [this, leaf](const std::shared_ptr<MV::Scene::Clickable>&) {
			raise(leaf);
		});

		// the bare part of the tab bar moves the panel
		if (auto strip = leafNode->get("tabStrip", false)) {
			auto mover = strip->attach<MV::Scene::Clickable>(mouse);
			mover->id("mover");
			mover->onPress.connect("raise", [this, leaf](const std::shared_ptr<MV::Scene::Clickable>&) {
				raise(leaf);
			});
			mover->onDrag.connect("move", [this, leaf](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<int>&, const MV::Point<int>& a_delta) {
				if (auto* moving = floatOf(leaf); moving && lastArea.size.width > 0.0f && lastArea.size.height > 0.0f) {
					moving->fraction.position.x += static_cast<float>(a_delta.x) / lastArea.size.width;
					moving->fraction.position.y += static_cast<float>(a_delta.y) / lastArea.size.height;
					layoutFloat(*moving);
				}
			});
			mover->onRelease.connect("moved", [this](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<MV::PointPrecision>&) {
				layoutChanged();
			});
		}

		auto corner = leafNode->make("floatCorner");
		auto resize = corner->attach<MV::Scene::Clickable>(mouse)->bounds(MV::size(theme.px(14.0f), theme.px(14.0f)));
		resize->color(theme.scrollThumb());
		resize->texture(roundedRectHandle(services, theme));
		resize->show();
		hoverTint(resize, resize, theme.scrollThumb(), theme.accent());
		resize->onDrag.connect("resize", [this, leaf](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<int>&, const MV::Point<int>& a_delta) {
			if (auto* sizing = floatOf(leaf); sizing && lastArea.size.width > 0.0f && lastArea.size.height > 0.0f) {
				auto current = worldRectOf(sizing->fraction);
				sizing->fraction.size.width = (current.size.width + static_cast<float>(a_delta.x)) / lastArea.size.width;
				sizing->fraction.size.height = (current.size.height + static_cast<float>(a_delta.y)) / lastArea.size.height;
				layoutFloat(*sizing);
			}
		});
		resize->onRelease.connect("resized", [this](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<MV::PointPrecision>&) {
			layoutChanged();
		});
	}

	void DockSpace::layoutFloat(FloatingLeaf& a_float) {
		auto rect = worldRectOf(a_float.fraction);
		layoutNode(a_float.leaf.get(), rect);
		auto chrome = leafChrome.find(a_float.leaf.get());
		if (chrome == leafChrome.end()) {
			return;
		}
		auto leafNode = chrome->second;
		if (auto shadow = leafNode->get("floatShadow", false)) {
			shadow->component<MV::Scene::Sprite>(true, false)->bounds(rect.size);
		}
		if (auto outline = leafNode->get("floatOutline", false)) {
			outline->component<MV::Scene::Sprite>(true, false)->bounds(MV::size(rect.size.width + theme.px(2.0f), rect.size.height + theme.px(2.0f)));
		}
		if (auto blocker = leafNode->get("floatBlocker", false)) {
			blocker->component<MV::Scene::Clickable>(true, false)->bounds(rect.size);
		}
		if (auto strip = leafNode->get("tabStrip", false)) {
			if (auto mover = strip->component<MV::Scene::Clickable>("mover", false)) {
				mover->bounds(MV::size(rect.size.width, theme.tabHeight));
			}
		}
		if (auto corner = leafNode->get("floatCorner", false)) {
			corner->position(MV::point(rect.size.width - theme.px(14.0f), rect.size.height - theme.px(14.0f)));
		}
	}

	DockSpace::CursorHint DockSpace::cursorHintAt(const MV::Point<>& a_worldPoint) const {
		for (auto&& [split, rect] : gripRects) {
			if (rect.contains(a_worldPoint)) {
				return split->vertical ? CursorHint::ResizeVertical : CursorHint::ResizeHorizontal;
			}
		}
		return CursorHint::None;
	}

	void DockSpace::layoutNode(DockNode* a_node, const DockRect& a_rect) {
		nodeRects[a_node] = a_rect;
		if (a_node->isLeaf()) {
			leafRects[a_node] = a_rect;
			auto chromeIt = leafChrome.find(a_node);
			if (chromeIt == leafChrome.end()) {
				return;
			}
			auto leafNode = chromeIt->second;
			leafNode->position(a_rect.position);

			MV::Size<> contentSize(a_rect.size.width, a_rect.size.height - theme.tabHeight);
			if (auto sizer = leafNode->component<MV::Scene::Sprite>("sizer", false)) {
				sizer->bounds(MV::BoxAABB<>(a_rect.size));
			}
			if (auto tabBar = leafNode->get("tabs", false)) {
				size_t tabCount = std::max<size_t>(1, a_node->panels.size());
				float tabGap = theme.px(2.0f);
				float tabWidth = std::min(theme.tabWidth, std::max(theme.px(40.0f), (a_rect.size.width - theme.pad) / tabCount - tabGap));
				MV::Size<> tabSize(tabWidth, theme.tabHeight);
				size_t tabIndex = 0;
				for (auto&& tab : *tabBar) {
					tab->position(MV::point(static_cast<float>(tabIndex) * (tabWidth + tabGap), 0.0f));
					if (auto tabBg = tab->component<MV::Scene::Sprite>("tabBg", false)) {
						tabBg->bounds(tabSize);
					}
					if (auto tabFoot = tab->component<MV::Scene::Sprite>("tabFoot", false)) {
						tabFoot->bounds(MV::BoxAABB<>(MV::point(0.0f, tabSize.height * 0.5f), MV::size(tabSize.width, tabSize.height * 0.5f)));
					}
					if (auto label = tab->component<MV::Scene::Text>(true, false)) {
						label->bounds({ MV::Point<>(), tabSize });
					}
					if (auto clickable = tab->component<MV::Scene::Clickable>(true, false)) {
						clickable->bounds(tabSize);
					}
					if (auto close = tab->get("close", false)) {
						close->position(MV::point(tabSize.width - theme.px(18.0f), std::round((tabSize.height - theme.px(14.0f)) * 0.5f)));
					}
					++tabIndex;
				}
			}
			refreshLeafVisuals(a_node);
			if (!a_node->panels.empty()) {
				Panel* activePanel = a_node->panels[static_cast<size_t>(a_node->active)];
				float gutter = activePanel->transparentWhenActive() ? 0.0f : scrollGutter();
				activePanel->resized(MV::Size<>(contentSize.width - theme.pad * 2.0f - gutter, contentSize.height - theme.pad * 2.0f));
			}
			syncScroll(a_node);
			return;
		}

		float extent = a_node->vertical ? a_rect.size.height : a_rect.size.width;
		float minExtent = theme.px(a_node->vertical ? 80.0f : 120.0f);
		float first = std::clamp(extent * a_node->ratio - theme.splitterSize * 0.5f, minExtent, std::max(minExtent, extent - minExtent - theme.splitterSize));
		float second = std::max(theme.tabHeight, extent - first - theme.splitterSize);

		DockRect rectA = a_rect;
		DockRect rectB = a_rect;
		DockRect grip = a_rect;
		if (a_node->vertical) {
			rectA.size.height = first;
			rectB.position.y = a_rect.position.y + first + theme.splitterSize;
			rectB.size.height = second;
			grip.position.y = a_rect.position.y + first;
			grip.size.height = theme.splitterSize;
		} else {
			rectA.size.width = first;
			rectB.position.x = a_rect.position.x + first + theme.splitterSize;
			rectB.size.width = second;
			grip.position.x = a_rect.position.x + first;
			grip.size.width = theme.splitterSize;
		}

		gripRects[a_node] = grip;
		auto chromeIt = leafChrome.find(a_node);
		if (chromeIt != leafChrome.end()) {
			chromeIt->second->position(grip.position);
			if (auto gripSprite = chromeIt->second->component<MV::Scene::Clickable>("grip", false)) {
				gripSprite->bounds(MV::BoxAABB<>(grip.size));
			}
		}
		layoutNode(a_node->a.get(), rectA);
		layoutNode(a_node->b.get(), rectB);
	}

	void DockSpace::rebuildChrome() {
		for (auto&& panel : ownedPanels) {
			panel->content()->parent(panelPool);
		}
		leafChrome.clear();
		tabNodes.clear();
		chromeRoot->clear();
		if (floatRoot) {
			floatRoot->clear();
		}
		raisedDepth = 0.0f;

		std::function<void(DockNode*)> walk = [&](DockNode* a_node) {
			if (a_node->isLeaf()) {
				buildLeafChrome(a_node, chromeRoot);
				return;
			}
			buildSplitterChrome(a_node);
			walk(a_node->a.get());
			walk(a_node->b.get());
		};
		walk(rootNode.get());
		for (auto&& floatingLeaf : floats) {
			buildFloatChrome(floatingLeaf);
		}
	}

	void DockSpace::buildLeafChrome(DockNode* a_leaf, const std::shared_ptr<MV::Scene::Node>& a_parent) {
		auto& mouse = *services.get<MV::TapDevice>();
		auto& library = *services.get<MV::TextLibrary>();

		auto leafNode = a_parent->make(MV::guid("dockLeaf_"));
		leafChrome[a_leaf] = leafNode;

		// Hidden sizer spans the leaf rect; chrome pieces hang off it with anchors so a single
		// bounds write per layout stretches everything (the ScreenScaler idiom).
		auto sizer = leafNode->attach<MV::Scene::Sprite>();
		sizer->id("sizer");
		sizer->bounds(MV::size(theme.tabWidth, theme.tabHeight));
		sizer->hide();

		// Panel content lives in an inner body so the Stencil clips it WITHOUT clipping the
		// tab bar (ancestor stencils gate both drawing and Clickable hit tests).
		auto body = leafNode->make("body");
		auto bg = body->attach<MV::Scene::Sprite>()->bounds(MV::size(theme.tabWidth, theme.tabHeight))->color(theme.panelBg());
		bg->id("bg");
		bg->anchors().anchor(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(1.0f, 1.0f))).offset(MV::BoxAABB<>(MV::point(0.0f, theme.tabHeight), MV::Point<>())).parent(sizer.self());
		auto clip = body->attach<MV::Scene::Stencil>();
		clip->id("clip");
		clip->anchors().anchor(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(1.0f, 1.0f))).offset(MV::BoxAABB<>(MV::point(0.0f, theme.tabHeight), MV::Point<>())).parent(sizer.self());

		for (auto&& panel : a_leaf->panels) {
			panel->content()->parent(body);
			panel->content()->position(MV::Point<>(theme.pad, theme.tabHeight + theme.pad - scrollOffsets[panel]));
		}

		auto scrollThumb = leafNode->make("scrollThumb")->attach<MV::Scene::Clickable>(mouse);
		scrollThumb->id("thumb");
		scrollThumb->texture(roundedRectHandle(services, theme))->color(theme.scrollThumb());
		scrollThumb->hide();
		hoverTint(scrollThumb.self(), scrollThumb.self(), theme.scrollThumb(), theme.fieldOutlineHover());
		scrollThumb->onDrag.connect("scroll", [this, a_leaf](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<int>&, const MV::Point<int>& a_delta) {
			auto rectIt = leafRects.find(a_leaf);
			if (rectIt == leafRects.end() || a_leaf->panels.empty()) {
				return;
			}
			Panel* activePanel = a_leaf->panels[static_cast<size_t>(a_leaf->active)];
			float viewHeight = viewHeightOf(rectIt->second);
			if (viewHeight > 0.0f) {
				scrollOffsets[activePanel] += static_cast<float>(a_delta.y) * activePanel->contentHeight() / viewHeight;
				syncScroll(a_leaf);
			}
		});

		auto tabStrip = leafNode->make("tabStrip");
		auto stripBg = tabStrip->attach<MV::Scene::Sprite>()->id("stripBg")->bounds(MV::size(theme.tabWidth, theme.tabHeight))->color(theme.windowBg());
		stripBg->anchors().anchor(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(1.0f, 0.0f))).offset(MV::BoxAABB<>(MV::Point<>(), MV::point(0.0f, theme.tabHeight))).parent(sizer.self());

		auto tabBar = leafNode->make("tabs");
		for (size_t i = 0; i < a_leaf->panels.size(); ++i) {
			Panel* panel = a_leaf->panels[i];
			auto tab = tabBar->make(MV::guid("tab_"));
			tabNodes[panel] = tab;
			tab->position(MV::Point<>(static_cast<float>(i) * (theme.tabWidth + theme.px(2.0f)), 0.0f));
			MV::Size<> tabSize(theme.tabWidth, theme.tabHeight);
			// rounded top, square foot: the active tab reads as one shape with the panel below it
			attachRoundedRect(tab, services, theme, tabSize, theme.panelBg())->id("tabBg");
			tab->attach<MV::Scene::Sprite>()->id("tabFoot")->bounds(MV::BoxAABB<>(MV::point(0.0f, tabSize.height * 0.5f), MV::size(tabSize.width, tabSize.height * 0.5f)))->color(theme.panelBg());
			auto label = tab->attach<MV::Scene::Text>(library, theme.labelFont);
			label->bounds({ MV::Point<>(), tabSize });
			label->justification(MV::TextJustification::CENTER)->wrapping(MV::TextWrapMethod::NONE, tabSize.width)->minimumLineHeight(tabSize.height)->text(panel->title());

			auto clickable = tab->attach<MV::Scene::Clickable>(mouse)->bounds(tabSize);
			int index = static_cast<int>(i);
			clickable->onAccept.connect("activate", [this, a_leaf, index](const std::shared_ptr<MV::Scene::Clickable>&) {
				activatePanel(a_leaf, index);
			});
			// a tab behind the front one shows its shape under the pointer
			std::weak_ptr<MV::Scene::Node> weakTab = tab;
			clickable->onHoverEnter.connect("hover", [this, a_leaf, index, weakTab](const std::shared_ptr<MV::Scene::Clickable>&) {
				auto hoveredTab = weakTab.lock();
				if (hoveredTab && index != a_leaf->active) {
					for (auto&& part : { "tabBg", "tabFoot" }) {
						if (auto sprite = hoveredTab->component<MV::Scene::Sprite>(part, false)) {
							sprite->color(theme.header());
							sprite->show();
						}
					}
				}
			});
			clickable->onHoverLeave.connect("hover", [this, a_leaf, index, weakTab](const std::shared_ptr<MV::Scene::Clickable>&) {
				auto hoveredTab = weakTab.lock();
				if (hoveredTab) {
					for (auto&& part : { "tabBg", "tabFoot" }) {
						if (auto sprite = hoveredTab->component<MV::Scene::Sprite>(part, false)) {
							sprite->color(theme.panelBg());
							if (index != a_leaf->active) {
								sprite->hide();
							}
						}
					}
				}
			});
			clickable->onDrag.connect("tabDrag", [this, a_leaf, panel](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<int>& a_start, const MV::Point<int>&) {
				if (!drag.active) {
					beginTabDrag(panel, a_leaf);
					drag.startScreen = a_start;
				}
				updateTabDrag();
			});
			clickable->onRelease.connect("tabDrop", [this](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<MV::PointPrecision>&) {
				if (drag.active) {
					endTabDrag(true);
				}
			});

			// shown on the tab in front; a child of the tab, so it takes the press before the tab does
			auto close = tab->make("close");
			const float closeSize = theme.px(14.0f);
			auto closeGlyph = close->attach<MV::Scene::Sprite>()->bounds(MV::size(closeSize, closeSize))->texture(iconHandle(services, theme, Icon::Close))->color(theme.glyph());
			auto closeClick = close->attach<MV::Scene::Clickable>(mouse);
			closeClick->onAccept.connect("close", [this, panel](const std::shared_ptr<MV::Scene::Clickable>&) {
				closePanel(panel);
			});
			hoverTint(closeClick.self(), closeGlyph, theme.glyph(), MV::Color(1.0f, 1.0f, 1.0f, 1.0f));
			tooltip(services, closeClick.self(), "Close " + panel->title());
		}
	}

	void DockSpace::buildSplitterChrome(DockNode* a_split) {
		auto& mouse = *services.get<MV::TapDevice>();
		auto gripNode = chromeRoot->make(MV::guid("dockGrip_"));
		leafChrome[a_split] = gripNode;
		auto grip = gripNode->attach<MV::Scene::Clickable>(mouse);
		grip->id("grip");
		grip->color(theme.splitter());
		grip->show();
		hoverTint(grip.self(), grip.self(), theme.splitter(), theme.accent());
		grip->onDrag.connect("resize", [this, a_split](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<int>&, const MV::Point<int>& a_delta) {
			auto rectIt = nodeRects.find(a_split);
			if (rectIt == nodeRects.end()) {
				return;
			}
			float extent = a_split->vertical ? rectIt->second.size.height : rectIt->second.size.width;
			if (extent <= 0.0f) {
				return;
			}
			float deltaAlongAxis = static_cast<float>(a_split->vertical ? a_delta.y : a_delta.x);
			a_split->ratio = std::clamp(a_split->ratio + deltaAlongAxis / extent, 0.1f, 0.9f);
			layout(lastArea);
		});
		grip->onRelease.connect("resized", [this](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<MV::PointPrecision>&) {
			layoutChanged();
		});
	}

	void DockSpace::refreshLeafVisuals(DockNode* a_leaf) {
		auto chromeIt = leafChrome.find(a_leaf);
		if (chromeIt == leafChrome.end() || a_leaf->panels.empty()) {
			return;
		}
		auto leafNode = chromeIt->second;
		a_leaf->active = std::clamp(a_leaf->active, 0, static_cast<int>(a_leaf->panels.size()) - 1);
		Panel* activePanel = a_leaf->panels[static_cast<size_t>(a_leaf->active)];

		auto tabBar = leafNode->get("tabs", false);
		if (tabBar) {
			size_t index = 0;
			for (auto&& tab : *tabBar) {
				for (auto&& part : { "tabBg", "tabFoot" }) {
					if (auto sprite = tab->component<MV::Scene::Sprite>(part, false)) {
						sprite->color(theme.panelBg());
						if (static_cast<int>(index) == a_leaf->active) {
							sprite->show();
						} else {
							sprite->hide();
						}
					}
				}
				if (auto close = tab->get("close", false)) {
					bool front = static_cast<int>(index) == a_leaf->active;
					close->visible(front);
					// a hidden Clickable still takes presses
					close->component<MV::Scene::Clickable>(true, false)->bounds(front ? MV::size(theme.px(14.0f), theme.px(14.0f)) : MV::Size<>());
				}
				++index;
			}
		}

		for (size_t i = 0; i < a_leaf->panels.size(); ++i) {
			a_leaf->panels[i]->content()->visible(static_cast<int>(i) == a_leaf->active);
		}
		syncScroll(a_leaf);

		auto body = leafNode->get("body", false);
		if (body) {
			if (auto bg = body->component<MV::Scene::Sprite>("bg", false)) {
				if (activePanel->transparentWhenActive()) {
					bg->hide();
				} else {
					bg->show();
				}
			}
		}
	}

	void DockSpace::show(Panel* a_panel) {
		if (auto* leaf = leafContaining(a_panel)) {
			auto found = std::find(leaf->panels.begin(), leaf->panels.end(), a_panel);
			raise(leaf);
			activatePanel(leaf, static_cast<int>(found - leaf->panels.begin()));
			return;
		}
		auto closed = std::find(closedPanels.begin(), closedPanels.end(), a_panel);
		if (closed == closedPanels.end()) {
			return;
		}
		closedPanels.erase(closed);
		auto beside = closedBeside.find(a_panel);
		DockNode* home = beside == closedBeside.end() ? nullptr : leafContaining(beside->second);
		closedBeside.erase(a_panel);
		if (home) {
			home->panels.push_back(a_panel);
			home->active = static_cast<int>(home->panels.size()) - 1;
		} else {
			// a window's worth in the middle of the dock
			DockRect fraction { MV::point(0.3f, 0.2f), MV::size(0.4f, 0.5f) };
			if (lastArea.size.width > 0.0f && lastArea.size.height > 0.0f) {
				fraction.size = MV::size(std::min(0.9f, theme.px(380.0f) / lastArea.size.width), std::min(0.9f, theme.px(320.0f) / lastArea.size.height));
				fraction.position = MV::point((1.0f - fraction.size.width) * 0.5f, (1.0f - fraction.size.height) * 0.4f);
			}
			floats.push_back({ leaf({ a_panel }), fraction });
		}
		structureDirty = true;
		layout(lastArea);
		layoutChanged();
	}

	void DockSpace::activatePanel(DockNode* a_leaf, int a_index) {
		bool changed = a_leaf->active != a_index;
		a_leaf->active = a_index;
		refreshLeafVisuals(a_leaf);
		layout(lastArea);
		if (changed) {
			layoutChanged();
		}
	}

	void DockSpace::layoutChanged() {
		if (onLayoutChanged) {
			onLayoutChanged();
		}
	}

	std::shared_ptr<MV::Scene::Node> DockSpace::panelContentContaining(const std::shared_ptr<MV::Scene::Node>& a_node) const {
		for (auto node = a_node; node; node = node->parent()) {
			for (auto&& panel : ownedPanels) {
				if (panel->content() == node) {
					return node;
				}
			}
		}
		return nullptr;
	}

	Panel* DockSpace::panelTitled(const std::string& a_title) const {
		for (auto&& panel : ownedPanels) {
			if (panel->title() == a_title) {
				return panel.get();
			}
		}
		return nullptr;
	}

	std::string DockSpace::describeLayout() const {
		std::ostringstream out;
		std::function<void(const DockNode*)> write = [&](const DockNode* a_node) {
			if (a_node->isLeaf()) {
				out << "L " << a_node->active << " [";
				for (size_t index = 0; index < a_node->panels.size(); ++index) {
					out << (index > 0 ? "," : "") << a_node->panels[index]->title();
				}
				out << "]";
			} else {
				out << (a_node->vertical ? "V " : "H ") << a_node->ratio << " (";
				write(a_node->a.get());
				out << ") (";
				write(a_node->b.get());
				out << ")";
			}
		};
		if (rootNode) {
			write(rootNode.get());
		}
		for (auto&& floatingLeaf : floats) {
			out << " F " << floatingLeaf.fraction.position.x << " " << floatingLeaf.fraction.position.y << " " << floatingLeaf.fraction.size.width << " " << floatingLeaf.fraction.size.height << " (";
			write(floatingLeaf.leaf.get());
			out << ")";
		}
		if (!closedPanels.empty()) {
			out << " X [";
			for (size_t index = 0; index < closedPanels.size(); ++index) {
				out << (index > 0 ? "," : "") << closedPanels[index]->title();
			}
			out << "]";
		}
		return out.str();
	}

	namespace {
		// Reads the text describeLayout writes. A leaf none of whose titles is known reads as no node at all, and a
		// split left with one side becomes that side.
		struct LayoutReader {
			const std::string& text;
			std::function<Panel*(const std::string&)> panelTitled;
			size_t at = 0;
			bool failed = false;
			std::vector<Panel*> placed;

			void skipSpaces() {
				while (at < text.size() && text[at] == ' ') {
					++at;
				}
			}

			bool take(char a_expected) {
				skipSpaces();
				if (at < text.size() && text[at] == a_expected) {
					++at;
					return true;
				}
				failed = true;
				return false;
			}

			float number() {
				skipSpaces();
				size_t start = at;
				while (at < text.size() && (std::isdigit(static_cast<unsigned char>(text[at])) || text[at] == '.' || text[at] == '-' || text[at] == 'e')) {
					++at;
				}
				if (start == at) {
					failed = true;
					return 0.0f;
				}
				try {
					return std::stof(text.substr(start, at - start));
				} catch (const std::exception&) {
					failed = true;
					return 0.0f;
				}
			}

			// The titles between the brackets that name panels not placed yet.
			std::vector<Panel*> titles() {
				std::vector<Panel*> result;
				if (!take('[')) {
					return result;
				}
				size_t close = text.find(']', at);
				if (close == std::string::npos) {
					failed = true;
					return result;
				}
				size_t start = at;
				while (start <= close) {
					size_t end = std::min(text.find(',', start), close);
					auto* panel = panelTitled(text.substr(start, end - start));
					if (panel && std::find(placed.begin(), placed.end(), panel) == placed.end()) {
						result.push_back(panel);
						placed.push_back(panel);
					}
					start = end + 1;
				}
				at = close + 1;
				return result;
			}

			std::unique_ptr<DockNode> node() {
				skipSpaces();
				if (at >= text.size()) {
					failed = true;
					return nullptr;
				}
				char kind = text[at++];
				if (kind == 'L') {
					auto result = std::make_unique<DockNode>();
					int active = static_cast<int>(number());
					result->panels = titles();
					if (failed || result->panels.empty()) {
						return nullptr;
					}
					result->active = std::clamp(active, 0, static_cast<int>(result->panels.size()) - 1);
					return result;
				}
				if (kind == 'H' || kind == 'V') {
					auto result = std::make_unique<DockNode>();
					result->vertical = kind == 'V';
					result->ratio = std::clamp(number(), 0.1f, 0.9f);
					if (!take('(')) {
						return nullptr;
					}
					auto first = node();
					if (failed || !take(')') || !take('(')) {
						return nullptr;
					}
					auto second = node();
					if (failed || !take(')')) {
						return nullptr;
					}
					if (!first || !second) {
						return first ? std::move(first) : std::move(second);
					}
					result->a = std::move(first);
					result->b = std::move(second);
					return result;
				}
				failed = true;
				return nullptr;
			}
		};
	}

	bool DockSpace::applyLayout(const std::string& a_description) {
		LayoutReader reader { a_description, [this](const std::string& a_title) { return panelTitled(a_title); } };
		auto read = reader.node();
		std::vector<FloatingLeaf> readFloats;
		std::vector<Panel*> readClosed;
		reader.skipSpaces();
		while (!reader.failed && read && reader.at < a_description.size()) {
			char kind = a_description[reader.at++];
			if (kind == 'F') {
				DockRect fraction;
				fraction.position.x = reader.number();
				fraction.position.y = reader.number();
				fraction.size.width = reader.number();
				fraction.size.height = reader.number();
				if (!reader.take('(')) {
					break;
				}
				auto floated = reader.node();
				if (reader.failed || !reader.take(')') || (floated && !floated->isLeaf())) {
					reader.failed = true;
					break;
				}
				if (floated) {
					readFloats.push_back({ std::move(floated), fraction });
				}
			} else if (kind == 'X') {
				readClosed = reader.titles();
			} else {
				reader.failed = true;
			}
			reader.skipSpaces();
		}
		if (reader.failed || !read || reader.at != a_description.size()) {
			return false;
		}
		DockNode* firstLeaf = read.get();
		while (!firstLeaf->isLeaf()) {
			firstLeaf = firstLeaf->a.get();
		}
		for (auto&& panel : ownedPanels) {
			if (std::find(reader.placed.begin(), reader.placed.end(), panel.get()) == reader.placed.end()) {
				firstLeaf->panels.push_back(panel.get());
			}
		}
		endTabDrag(false);
		rootNode = std::move(read);
		floats = std::move(readFloats);
		closedPanels = std::move(readClosed);
		closedBeside.clear();
		leafChrome.clear();
		leafRects.clear();
		nodeRects.clear();
		gripRects.clear();
		structureDirty = true;
		if (lastArea.size.width > 0.0f && lastArea.size.height > 0.0f) {
			layout(lastArea);
		}
		return true;
	}

	MV::Point<int> DockSpace::tabScreenPoint(const std::string& a_panelTitle) const {
		auto found = tabNodes.find(panelTitled(a_panelTitle));
		auto tab = found == tabNodes.end() ? nullptr : found->second.lock();
		if (!tab) {
			return {};
		}
		// left of centre: clear of the close button at the tab's right end
		auto box = tab->screenBounds();
		return MV::Point<int>(box.minPoint.x + (box.maxPoint.x - box.minPoint.x) / 3, (box.minPoint.y + box.maxPoint.y) / 2);
	}

	MV::Point<int> DockSpace::dropScreenPoint(const std::string& a_panelTitle, DropZone a_zone) const {
		auto found = leafRects.find(leafContaining(panelTitled(a_panelTitle)));
		if (found == leafRects.end()) {
			return {};
		}
		auto& rect = found->second;
		if (a_zone == DropZone::Center) {
			return uiRoot->renderer().screenFromWorld(MV::point(rect.position.x + rect.size.width * 0.5f, rect.position.y + theme.tabHeight * 0.5f));
		}
		if (a_zone == DropZone::Float) {
			return uiRoot->renderer().screenFromWorld(MV::point(rect.position.x + rect.size.width * 0.5f, rect.position.y + rect.size.height * 0.5f));
		}
		auto zone = zoneRect(rect, a_zone);
		return uiRoot->renderer().screenFromWorld(MV::point(zone.position.x + zone.size.width * 0.5f, zone.position.y + zone.size.height * 0.5f));
	}

	MV::Point<int> DockSpace::closeButtonScreenPoint(const std::string& a_panelTitle) const {
		auto found = tabNodes.find(panelTitled(a_panelTitle));
		auto tab = found == tabNodes.end() ? nullptr : found->second.lock();
		auto close = tab ? tab->get("close", false) : nullptr;
		return close ? close->screenBounds().centerPoint() : MV::Point<int>();
	}

	DockRect DockSpace::floatRect(const std::string& a_panelTitle) const {
		auto found = leafRects.find(leafContaining(panelTitled(a_panelTitle)));
		return found == leafRects.end() || !floatOf(found->first) ? DockRect {} : found->second;
	}

	MV::Point<int> DockSpace::floatBarScreenPoint(const std::string& a_panelTitle) const {
		auto rect = floatRect(a_panelTitle);
		return uiRoot->renderer().screenFromWorld(MV::point(rect.position.x + rect.size.width - theme.px(40.0f), rect.position.y + theme.tabHeight * 0.5f));
	}

	MV::Point<int> DockSpace::floatCornerScreenPoint(const std::string& a_panelTitle) const {
		auto rect = floatRect(a_panelTitle);
		return uiRoot->renderer().screenFromWorld(MV::point(rect.position.x + rect.size.width - theme.px(7.0f), rect.position.y + rect.size.height - theme.px(7.0f)));
	}

	MV::Point<int> DockSpace::rootSplitterScreenPoint() const {
		auto found = gripRects.find(rootNode.get());
		if (found == gripRects.end()) {
			return {};
		}
		return uiRoot->renderer().screenFromWorld(MV::point(found->second.position.x + found->second.size.width * 0.5f, found->second.position.y + found->second.size.height * 0.5f));
	}

	void DockSpace::beginTabDrag(Panel* a_panel, DockNode* a_sourceLeaf) {
		drag = DragState{};
		drag.active = true;
		drag.panel = a_panel;
		drag.sourceLeaf = a_sourceLeaf;
	}

	void DockSpace::updateTabDrag() {
		auto& mouse = *services.get<MV::TapDevice>();
		if (!drag.passedThreshold) {
			if (lengthBetween(mouse.position(), drag.startScreen) < 8.0f) {
				return;
			}
			drag.passedThreshold = true;
			if (auto label = dragGhost->component<MV::Scene::Text>(false, false)) {
				label->text(drag.panel->title());
			}
			dragGhost->show();
		}
		auto worldPoint = uiRoot->renderer().worldFromScreen(mouse.position());
		dragGhost->position(worldPoint + MV::point(6.0f, 6.0f));

		DropZone zone = DropZone::None;
		DockNode* target = leafUnder(worldPoint, zone);
		auto hintNode = overlayRoot->get("dropHint", false);
		if (target && zone != DropZone::None && hintNode) {
			auto rect = zone == DropZone::Float ? worldRectOf(fractionAround(worldPoint)) : zoneRect(leafRects[target], zone);
			hintNode->position(rect.position);
			dropHintSprite->bounds(MV::BoxAABB<>(rect.size));
			hintNode->show();
		} else if (hintNode) {
			hintNode->hide();
		}
	}

	void DockSpace::endTabDrag(bool a_commit) {
		bool moved = drag.passedThreshold;
		Panel* panel = drag.panel;
		DockNode* source = drag.sourceLeaf;

		dragGhost->hide();
		if (auto hintNode = overlayRoot->get("dropHint", false)) {
			hintNode->hide();
		}

		if (!a_commit || !moved || !panel) {
			drag = DragState{};
			return;
		}

		auto& mouse = *services.get<MV::TapDevice>();
		auto worldPoint = uiRoot->renderer().worldFromScreen(mouse.position());
		DropZone zone = DropZone::None;
		DockNode* target = leafUnder(worldPoint, zone);
		drag = DragState{};

		if (!target || zone == DropZone::None) {
			return;
		}
		// the main area always keeps a panel
		bool lastDocked = source == rootNode.get() && source->panels.size() == 1;
		if (zone == DropZone::Float) {
			if (lastDocked) {
				return;
			}
			auto fraction = fractionAround(worldPoint);
			if (auto* alreadyFloating = floatOf(source); alreadyFloating && source->panels.size() == 1) {
				alreadyFloating->fraction.position = fraction.position;
				raise(source);
			} else {
				detach(panel);
				floats.push_back({ leaf({ panel }), fraction });
				structureDirty = true;
			}
			layout(lastArea);
			layoutChanged();
			return;
		}
		if (target == source && (zone == DropZone::Center || source->panels.size() == 1)) {
			return;
		}
		if (lastDocked && floatOf(target)) {
			return;
		}

		// Detach WITHOUT collapsing yet: collapsing the source leaf can free the sibling node
		// that `target` points at. Insertion happens in place; collapse runs last.
		if (source) {
			source->panels.erase(std::remove(source->panels.begin(), source->panels.end(), panel), source->panels.end());
			if (!source->panels.empty()) {
				source->active = std::clamp(source->active, 0, static_cast<int>(source->panels.size()) - 1);
			}
			panel->content()->parent(panelPool);
		}
		if (zone == DropZone::Center) {
			target->panels.push_back(panel);
			target->active = static_cast<int>(target->panels.size()) - 1;
		} else {
			auto existing = std::make_unique<DockNode>();
			existing->panels = std::move(target->panels);
			existing->active = target->active;
			auto incoming = leaf({ panel });

			target->panels.clear();
			target->active = 0;
			target->vertical = (zone == DropZone::Top || zone == DropZone::Bottom);
			target->ratio = 0.3f;
			if (zone == DropZone::Left || zone == DropZone::Top) {
				target->a = std::move(incoming);
				target->b = std::move(existing);
			} else {
				target->ratio = 0.7f;
				target->a = std::move(existing);
				target->b = std::move(incoming);
			}
		}
		if (source && source != target && source->panels.empty() && source->isLeaf()) {
			if (floatOf(source)) {
				leafRects.erase(source);
				leafChrome.erase(source);
				floats.erase(std::find_if(floats.begin(), floats.end(), [&](const FloatingLeaf& a_float) { return a_float.leaf.get() == source; }));
			} else {
				collapseIfEmpty(source);
			}
		}
		structureDirty = true;
		layout(lastArea);
		layoutChanged();
	}

	Panel* DockSpace::panelShowingAt(const MV::Point<>& a_worldPoint) const {
		DropZone zone = DropZone::None;
		DockNode* leaf = leafUnder(a_worldPoint, zone);
		if (!leaf || leaf->panels.empty()) {
			return nullptr;
		}
		auto rect = leafRects.find(leaf);
		if (rect == leafRects.end() || a_worldPoint.y < rect->second.position.y + theme.tabHeight) {
			return nullptr;
		}
		return leaf->panels[static_cast<size_t>(leaf->active)];
	}

	bool DockSpace::viewportAt(const MV::Point<>& a_worldPoint) const {
		DropZone zone = DropZone::None;
		DockNode* leaf = leafUnder(a_worldPoint, zone);
		if (!leaf || leaf->panels.empty()) {
			return false;
		}
		return leaf->panels[static_cast<size_t>(leaf->active)]->transparentWhenActive();
	}

	DockRect DockSpace::viewportRect() const {
		for (auto&& [leaf, rect] : leafRects) {
			if (!floatOf(leaf) && !leaf->panels.empty() && leaf->panels[static_cast<size_t>(leaf->active)]->transparentWhenActive()) {
				return DockRect{ MV::point(rect.position.x, rect.position.y + theme.tabHeight),
					MV::Size<>(rect.size.width, rect.size.height - theme.tabHeight) };
			}
		}
		return DockRect{};
	}

	bool DockSpace::wheelScroll(const MV::Point<>& a_worldPoint, float a_amount) {
		DropZone zone = DropZone::None;
		DockNode* leaf = leafUnder(a_worldPoint, zone);
		if (!leaf || leaf->panels.empty()) {
			return false;
		}
		Panel* activePanel = leaf->panels[static_cast<size_t>(leaf->active)];
		if (activePanel->transparentWhenActive()) {
			return false;
		}
		scrollOffsets[activePanel] -= a_amount * theme.rowHeight * 3.0f;
		syncScroll(leaf);
		return true;
	}

	void DockSpace::syncScroll(DockNode* a_leaf) {
		auto rectIt = leafRects.find(a_leaf);
		auto chromeIt = leafChrome.find(a_leaf);
		if (rectIt == leafRects.end() || chromeIt == leafChrome.end() || a_leaf->panels.empty()) {
			return;
		}
		Panel* activePanel = a_leaf->panels[static_cast<size_t>(a_leaf->active)];
		float viewHeight = viewHeightOf(rectIt->second);
		float contentHeight = activePanel->contentHeight();
		float maxOffset = std::max(0.0f, contentHeight - viewHeight);
		float& offset = scrollOffsets[activePanel];
		offset = std::clamp(offset, 0.0f, maxOffset);
		activePanel->content()->position(MV::Point<>(theme.pad, theme.tabHeight + theme.pad - offset));

		auto thumbNode = chromeIt->second->get("scrollThumb", false);
		auto thumb = thumbNode ? thumbNode->component<MV::Scene::Clickable>("thumb", false) : MV::Scene::SafeComponent<MV::Scene::Clickable>();
		if (!thumb) {
			return;
		}
		if (maxOffset <= 0.0f || viewHeight <= 0.0f || activePanel->transparentWhenActive()) {
			thumb->bounds(MV::size(0.0f, 0.0f));   // a hidden Clickable still takes presses
			thumb->hide();
			return;
		}
		float trackHeight = viewHeight + theme.pad * 2.0f - theme.px(4.0f);
		float thumbHeight = std::max(theme.px(24.0f), trackHeight * viewHeight / contentHeight);
		float thumbWidth = theme.px(6.0f);
		thumb->bounds(MV::size(thumbWidth, thumbHeight));
		thumb->show();
		thumbNode->position(MV::point(
			rectIt->second.size.width - thumbWidth - theme.px(2.0f),
			theme.tabHeight + theme.px(2.0f) + (trackHeight - thumbHeight) * (offset / maxOffset)));
	}

	DockNode* DockSpace::leafUnder(const MV::Point<>& a_worldPoint, DropZone& a_zone) const {
		// floating panels cover the dock: the one in front first. A tab joins one on its tab bar and floats anywhere else on it.
		for (auto floatingLeaf = floats.rbegin(); floatingLeaf != floats.rend(); ++floatingLeaf) {
			auto found = leafRects.find(floatingLeaf->leaf.get());
			if (found != leafRects.end() && found->second.contains(a_worldPoint)) {
				a_zone = a_worldPoint.y < found->second.position.y + theme.tabHeight ? DropZone::Center : DropZone::Float;
				return found->first;
			}
		}
		for (auto&& [node, rect] : leafRects) {
			if (floatOf(node) || !rect.contains(a_worldPoint)) {
				continue;
			}
			float relativeX = (a_worldPoint.x - rect.position.x) / std::max(1.0f, rect.size.width);
			float relativeY = (a_worldPoint.y - rect.position.y) / std::max(1.0f, rect.size.height);
			if (a_worldPoint.y < rect.position.y + theme.tabHeight) {
				a_zone = DropZone::Center;
			} else if (relativeX < 0.25f) {
				a_zone = DropZone::Left;
			} else if (relativeX > 0.75f) {
				a_zone = DropZone::Right;
			} else if (relativeY < 0.25f) {
				a_zone = DropZone::Top;
			} else if (relativeY > 0.75f) {
				a_zone = DropZone::Bottom;
			} else {
				a_zone = DropZone::Float;
			}
			return node;
		}
		a_zone = DropZone::None;
		return nullptr;
	}

	DockRect DockSpace::zoneRect(const DockRect& a_leafRect, DropZone a_zone) const {
		DockRect rect = a_leafRect;
		switch (a_zone) {
		case DropZone::Left:
			rect.size.width *= 0.3f;
			break;
		case DropZone::Right:
			rect.position.x += a_leafRect.size.width * 0.7f;
			rect.size.width *= 0.3f;
			break;
		case DropZone::Top:
			rect.size.height *= 0.3f;
			break;
		case DropZone::Bottom:
			rect.position.y += a_leafRect.size.height * 0.7f;
			rect.size.height *= 0.3f;
			break;
		default:
			break;
		}
		return rect;
	}

	void DockSpace::removePanelFromTree(Panel* a_panel) {
		DockNode* owner = findLeafContaining(a_panel, rootNode.get());
		if (!owner) {
			return;
		}
		owner->panels.erase(std::remove(owner->panels.begin(), owner->panels.end(), a_panel), owner->panels.end());
		if (!owner->panels.empty()) {
			owner->active = std::clamp(owner->active, 0, static_cast<int>(owner->panels.size()) - 1);
		}
		a_panel->content()->parent(panelPool);
		collapseIfEmpty(owner);
	}

	DockNode* DockSpace::findLeafContaining(Panel* a_panel, DockNode* a_node) const {
		if (!a_node) {
			return nullptr;
		}
		if (a_node->isLeaf()) {
			auto found = std::find(a_node->panels.begin(), a_node->panels.end(), a_panel);
			return found != a_node->panels.end() ? a_node : nullptr;
		}
		if (auto inA = findLeafContaining(a_panel, a_node->a.get())) {
			return inA;
		}
		return findLeafContaining(a_panel, a_node->b.get());
	}

	DockNode* DockSpace::findParent(DockNode* a_child, DockNode* a_node) const {
		if (!a_node || a_node->isLeaf()) {
			return nullptr;
		}
		if (a_node->a.get() == a_child || a_node->b.get() == a_child) {
			return a_node;
		}
		if (auto inA = findParent(a_child, a_node->a.get())) {
			return inA;
		}
		return findParent(a_child, a_node->b.get());
	}

	void DockSpace::collapseIfEmpty(DockNode* a_leaf) {
		if (!a_leaf->panels.empty() || !a_leaf->isLeaf()) {
			return;
		}
		DockNode* parent = findParent(a_leaf, rootNode.get());
		if (!parent) {
			return;
		}
		std::unique_ptr<DockNode> sibling = (parent->a.get() == a_leaf) ? std::move(parent->b) : std::move(parent->a);
		parent->a.reset();
		parent->b.reset();
		parent->vertical = sibling->vertical;
		parent->ratio = sibling->ratio;
		parent->panels = std::move(sibling->panels);
		parent->active = sibling->active;
		parent->a = std::move(sibling->a);
		parent->b = std::move(sibling->b);
	}

	void DockSpace::update(double a_dt) {
		for (auto&& panel : ownedPanels) {
			panel->update(a_dt);
		}
		// a panel's content changes height when it rebuilds
		for (auto&& [leaf, rect] : leafRects) {
			syncScroll(leaf);
		}
	}

	void DockSpace::handleInput(SDL_Event& a_event) {
		for (auto&& panel : ownedPanels) {
			panel->handleInput(a_event);
		}
	}

}
