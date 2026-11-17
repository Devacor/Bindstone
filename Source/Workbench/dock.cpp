#include "dock.h"
#include "widgets.h"
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
						if (static_cast<int>(index) == a_leaf->active) {
							sprite->show();
						} else {
							sprite->hide();
						}
					}
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
		if (auto* leaf = findLeafContaining(a_panel, rootNode.get())) {
			auto found = std::find(leaf->panels.begin(), leaf->panels.end(), a_panel);
			activatePanel(leaf, static_cast<int>(found - leaf->panels.begin()));
		}
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
				while (at < text.size() && (std::isdigit(static_cast<unsigned char>(text[at])) || text[at] == '.')) {
					++at;
				}
				if (start == at) {
					failed = true;
					return 0.0f;
				}
				return std::stof(text.substr(start, at - start));
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
					if (!take('[')) {
						return nullptr;
					}
					size_t close = text.find(']', at);
					if (close == std::string::npos) {
						failed = true;
						return nullptr;
					}
					size_t start = at;
					while (start <= close) {
						size_t end = std::min(text.find(',', start), close);
						auto* panel = panelTitled(text.substr(start, end - start));
						if (panel && std::find(placed.begin(), placed.end(), panel) == placed.end()) {
							result->panels.push_back(panel);
							placed.push_back(panel);
						}
						start = end + 1;
					}
					at = close + 1;
					if (result->panels.empty()) {
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
		reader.skipSpaces();
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
		return tab ? tab->screenBounds().centerPoint() : MV::Point<int>();
	}

	MV::Point<int> DockSpace::dropScreenPoint(const std::string& a_panelTitle, DropZone a_zone) const {
		auto* leaf = findLeafContaining(panelTitled(a_panelTitle), rootNode.get());
		auto found = leafRects.find(leaf);
		if (found == leafRects.end()) {
			return {};
		}
		auto zone = zoneRect(found->second, a_zone);
		return uiRoot->renderer().screenFromWorld(MV::point(zone.position.x + zone.size.width * 0.5f, zone.position.y + zone.size.height * 0.5f));
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
		layoutChanged();
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
