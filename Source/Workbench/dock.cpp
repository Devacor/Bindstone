#include "dock.h"
#include "widgets.h"
#include <algorithm>
#include <cmath>

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
		attachRoundedRect(dragGhost, services, theme, MV::size(theme.tabWidth, theme.tabHeight), theme.tabActive());
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
				float tabGap = 2.0f;
				float tabWidth = std::min(theme.tabWidth, std::max(40.0f, (a_rect.size.width - theme.pad) / tabCount - tabGap));
				MV::Size<> tabSize(tabWidth, theme.tabHeight);
				size_t tabIndex = 0;
				for (auto&& tab : *tabBar) {
					tab->position(MV::point(static_cast<float>(tabIndex) * (tabWidth + tabGap), 0.0f));
					if (auto tabBg = tab->component<MV::Scene::Sprite>("tabBg", false)) {
						tabBg->bounds(tabSize);
					}
					if (auto label = tab->component<MV::Scene::Text>(true, false)) {
						label->bounds({ MV::Point<>(), tabSize });
					}
					if (auto clickable = tab->component<MV::Scene::Clickable>(true, false)) {
						clickable->bounds(tabSize);
					}
					++tabIndex;
				}
			}
			refreshLeafVisuals(a_node);
			if (!a_node->panels.empty()) {
				a_node->panels[static_cast<size_t>(a_node->active)]->resized(MV::Size<>(contentSize.width - theme.pad * 2.0f, contentSize.height - theme.pad * 2.0f));
			}
			return;
		}

		float extent = a_node->vertical ? a_rect.size.height : a_rect.size.width;
		float minExtent = a_node->vertical ? 80.0f : 120.0f;
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
		chromeRoot->clear();

		std::function<void(DockNode*)> walk = [&](DockNode* a_node) {
			if (a_node->isLeaf()) {
				buildLeafChrome(a_node);
				return;
			}
			buildSplitterChrome(a_node);
			walk(a_node->a.get());
			walk(a_node->b.get());
		};
		walk(rootNode.get());
	}

	void DockSpace::buildLeafChrome(DockNode* a_leaf) {
		auto& mouse = *services.get<MV::TapDevice>();
		auto& library = *services.get<MV::TextLibrary>();

		auto leafNode = chromeRoot->make(MV::guid("dockLeaf_"));
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
		auto bg = attachRoundedRect(body, services, theme, MV::size(theme.tabWidth, theme.tabHeight), theme.panelBg());
		bg->id("bg");
		bg->anchors().anchor(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(1.0f, 1.0f))).offset(MV::BoxAABB<>(MV::point(0.0f, theme.tabHeight), MV::Point<>())).parent(sizer.self());
		auto clip = body->attach<MV::Scene::Stencil>();
		clip->id("clip");
		clip->anchors().anchor(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(1.0f, 1.0f))).offset(MV::BoxAABB<>(MV::point(0.0f, theme.tabHeight), MV::Point<>())).parent(sizer.self());

		for (auto&& panel : a_leaf->panels) {
			panel->content()->parent(body);
			panel->content()->position(MV::Point<>(theme.pad, theme.tabHeight + theme.pad - scrollOffsets[panel]));
		}

		auto tabStrip = leafNode->make("tabStrip");
		auto stripBg = tabStrip->attach<MV::Scene::Sprite>()->id("stripBg")->bounds(MV::size(theme.tabWidth, theme.tabHeight))->color(theme.tabStrip());
		stripBg->anchors().anchor(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(1.0f, 0.0f))).offset(MV::BoxAABB<>(MV::Point<>(), MV::point(0.0f, theme.tabHeight))).parent(sizer.self());

		auto tabBar = leafNode->make("tabs");
		for (size_t i = 0; i < a_leaf->panels.size(); ++i) {
			Panel* panel = a_leaf->panels[i];
			auto tab = tabBar->make(MV::guid("tab_"));
			tab->position(MV::Point<>(static_cast<float>(i) * (theme.tabWidth + 2.0f), 0.0f));
			MV::Size<> tabSize(theme.tabWidth, theme.tabHeight);
			attachRoundedRect(tab, services, theme, tabSize, theme.tabIdle())->id("tabBg");
			auto label = tab->attach<MV::Scene::Text>(library, theme.labelFont);
			label->bounds({ MV::Point<>(), tabSize });
			label->justification(MV::TextJustification::CENTER)->wrapping(MV::TextWrapMethod::NONE, tabSize.width)->minimumLineHeight(tabSize.height)->text(panel->title());

			auto clickable = tab->attach<MV::Scene::Clickable>(mouse)->bounds(tabSize);
			int index = static_cast<int>(i);
			clickable->onAccept.connect("activate", [this, a_leaf, index](const std::shared_ptr<MV::Scene::Clickable>&) {
				activatePanel(a_leaf, index);
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
				if (auto tabBg = tab->component<MV::Scene::Sprite>("tabBg", false)) {
					tabBg->color(static_cast<int>(index) == a_leaf->active ? theme.tabActive() : theme.tabIdle());
				}
				++index;
			}
		}

		for (size_t i = 0; i < a_leaf->panels.size(); ++i) {
			a_leaf->panels[i]->content()->visible(static_cast<int>(i) == a_leaf->active);
		}
		activePanel->content()->position(MV::Point<>(theme.pad, theme.tabHeight + theme.pad - scrollOffsets[activePanel]));

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

	void DockSpace::activatePanel(DockNode* a_leaf, int a_index) {
		a_leaf->active = a_index;
		refreshLeafVisuals(a_leaf);
		layout(lastArea);
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
			auto rect = zoneRect(leafRects[target], zone);
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
		if (target == source && (zone == DropZone::Center || source->panels.size() == 1)) {
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
			collapseIfEmpty(source);
		}
		structureDirty = true;
		layout(lastArea);
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
			if (!leaf->panels.empty() && leaf->panels[static_cast<size_t>(leaf->active)]->transparentWhenActive()) {
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
		float viewHeight = leafRects[leaf].size.height - theme.tabHeight - theme.pad * 2.0f;
		float maxOffset = std::max(0.0f, activePanel->contentHeight() - viewHeight);
		float& offset = scrollOffsets[activePanel];
		offset = std::clamp(offset - a_amount * theme.rowHeight * 3.0f, 0.0f, maxOffset);
		activePanel->content()->position(MV::Point<>(theme.pad, theme.tabHeight + theme.pad - offset));
		return true;
	}

	DockNode* DockSpace::leafUnder(const MV::Point<>& a_worldPoint, DropZone& a_zone) const {
		for (auto&& [node, rect] : leafRects) {
			if (!rect.contains(a_worldPoint)) {
				continue;
			}
			float relativeX = (a_worldPoint.x - rect.position.x) / std::max(1.0f, rect.size.width);
			float relativeY = (a_worldPoint.y - rect.position.y) / std::max(1.0f, rect.size.height);
			if (relativeX < 0.25f) {
				a_zone = DropZone::Left;
			} else if (relativeX > 0.75f) {
				a_zone = DropZone::Right;
			} else if (relativeY < 0.25f) {
				a_zone = DropZone::Top;
			} else if (relativeY > 0.75f) {
				a_zone = DropZone::Bottom;
			} else {
				a_zone = DropZone::Center;
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
	}

	void DockSpace::handleInput(SDL_Event& a_event) {
		for (auto&& panel : ownedPanels) {
			panel->handleInput(a_event);
		}
	}

}
