#ifndef _WORKBENCH_TREEVIEW_H_
#define _WORKBENCH_TREEVIEW_H_

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "widgets.h"

namespace Workbench {

	// Unity-style tree: full-width rows, foldout triangles, selection highlight, zebra rows.
	// Model is adapter-based so the same widget serves the scene hierarchy and folder trees.
	// TItem must be usable as a map key and comparable (pointer or path string).
	template <typename TItem>
	class TreeView {
	public:
		struct Adapter {
			std::function<std::vector<TItem>(const TItem&)> children;
			std::function<std::string(const TItem&)> label;
			std::function<bool(const TItem&)> hasChildren;
		};

		TreeView(const std::shared_ptr<MV::Scene::Node>& a_host, MV::Services& a_services, const Theme& a_theme, Adapter a_adapter) :
			host(a_host), services(a_services), theme(a_theme), adapter(std::move(a_adapter)) {
			hoverReceiver = services.get<MV::TapDevice>()->onMove.connect("treeHover", [this](MV::TapDevice& a_mouse) {
				updateHover(a_mouse.position());
			});
		}

		enum class RowZone { Onto, Before, After };

		std::function<void(const TItem&)> onSelect;
		std::function<void(const TItem&)> onDoubleClick;
		// When set, rows become draggable: drop onto a row reparents, drop near an edge reorders.
		std::function<void(const TItem& a_dragged, const TItem& a_target, RowZone a_zone)> onReparent;
		// A row dragged and released off the tree: another panel may take it.
		std::function<void(const TItem& a_dragged, const MV::Point<int>& a_screenPoint)> onDropOutside;

		void root(const TItem& a_root, bool a_showRoot = true) {
			rootItem = a_root;
			hasRoot = true;
			showRoot = a_showRoot;
			rebuild();
		}

		void width(float a_width) {
			if (std::abs(a_width - rowWidth) > 0.5f) {
				rowWidth = a_width;
				rebuild();
			}
		}

		// Case-insensitive label filter: rows show when they or any descendant match; matching
		// branches are force-expanded. Empty string clears.
		void filter(const std::string& a_query) {
			filterQuery = a_query;
			std::transform(filterQuery.begin(), filterQuery.end(), filterQuery.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			rebuild();
		}

		// The item whose row is at a point on screen, and the middle of an item's row.
		const TItem* itemAt(const MV::Point<int>& a_screenPoint) const {
			int index = rowAt(a_screenPoint);
			return index < 0 ? nullptr : &rowItems[static_cast<size_t>(index)];
		}

		std::optional<MV::Point<int>> rowScreenPoint(const TItem& a_item) const {
			for (size_t index = 0; index < rowItems.size(); ++index) {
				if (rowItems[index] == a_item) {
					return host->screenFromLocal(MV::point(rowWidth * 0.5f, (static_cast<float>(index) + 0.5f) * theme.rowHeight));
				}
			}
			return std::nullopt;
		}

		void select(const TItem& a_item) {
			selected = a_item;
			hasSelection = true;
			rebuild();
			if (onSelect) {
				onSelect(a_item);
			}
		}

		// Highlights a row without firing onSelect: the owner of the selection already knows.
		void showSelection(const TItem* a_item) {
			if (a_item ? (hasSelection && selected == *a_item) : !hasSelection) {
				return;
			}
			hasSelection = a_item != nullptr;
			selected = a_item ? *a_item : TItem{};
			rebuild();
		}

		// Returns true when the item was not already marked expanded; the caller rebuilds.
		bool expand(const TItem& a_item) {
			auto found = expanded.find(a_item);
			if (found != expanded.end() && found->second) {
				return false;
			}
			expanded[a_item] = true;
			return true;
		}

		// Drops expansion, selection and drag state for items the model no longer holds.
		void retainState(const std::function<bool(const TItem&)>& a_alive) {
			std::erase_if(expanded, [&](const auto& a_entry) { return !a_alive(a_entry.first); });
			if (hasSelection && !a_alive(selected)) {
				hasSelection = false;
				selected = TItem{};
			}
			if (rowDragActive && !a_alive(dragItem)) {
				rowDragActive = false;
				dragItem = TItem{};
			}
		}

		const TItem* selection() const { return hasSelection ? &selected : nullptr; }
		float height() const { return builtHeight; }

		void rebuild() {
			host->clear();
			builtHeight = 0.0f;
			rowIndex = 0;
			hoverIndex = -1;
			rowBackgrounds.clear();
			rowBaseColors.clear();
			rowItems.clear();
			if (hasRoot) {
				if (showRoot) {
					addRow(rootItem, 0);
				} else {
					for (auto&& child : adapter.children(rootItem)) {
						addRow(child, 0);
					}
				}
			}
			auto marker = host->make("insertMarker");
			marker->attach<MV::Scene::Sprite>()->bounds(MV::size(rowWidth, theme.px(2.0f)))->color(theme.accent());
			marker->hide();
		}

	private:
		int rowAt(const MV::Point<int>& a_screenPoint) const {
			auto local = host->localFromScreen(a_screenPoint);
			if (local.x < 0.0f || local.x > rowWidth || local.y < 0.0f) {
				return -1;
			}
			int index = static_cast<int>(local.y / theme.rowHeight);
			return index < static_cast<int>(rowItems.size()) ? index : -1;
		}

		void restoreRowColor(int a_index) {
			if (a_index >= 0 && a_index < static_cast<int>(rowBackgrounds.size())) {
				rowBackgrounds[static_cast<size_t>(a_index)]->color(rowBaseColors[static_cast<size_t>(a_index)]);
			}
		}

		void updateHover(const MV::Point<int>& a_screenPoint) {
			if (rowDragActive) {
				updateDragFeedback(a_screenPoint);
				return;
			}
			int index = rowAt(a_screenPoint);
			if (index == hoverIndex) {
				return;
			}
			restoreRowColor(hoverIndex);
			hoverIndex = index;
			if (hoverIndex >= 0 && !(hasSelection && rowItems[static_cast<size_t>(hoverIndex)] == selected)) {
				rowBackgrounds[static_cast<size_t>(hoverIndex)]->color(theme.rowHover());
			}
		}

		RowZone zoneWithinRow(const MV::Point<int>& a_screenPoint, int a_row) const {
			float localY = host->localFromScreen(a_screenPoint).y - a_row * theme.rowHeight;
			if (localY < theme.rowHeight * 0.25f) {
				return RowZone::Before;
			}
			if (localY > theme.rowHeight * 0.75f) {
				return RowZone::After;
			}
			return RowZone::Onto;
		}

		void updateDragFeedback(const MV::Point<int>& a_screenPoint) {
			restoreRowColor(hoverIndex);
			hoverIndex = -1;
			auto marker = host->get("insertMarker", false);
			int target = rowAt(a_screenPoint);
			if (target < 0 || rowItems[static_cast<size_t>(target)] == dragItem) {
				if (marker) { marker->hide(); }
				return;
			}
			RowZone zone = zoneWithinRow(a_screenPoint, target);
			if (zone == RowZone::Onto) {
				if (marker) { marker->hide(); }
				rowBackgrounds[static_cast<size_t>(target)]->color(theme.dropHint());
				hoverIndex = target;
			} else if (marker) {
				float markerY = (zone == RowZone::Before ? target : target + 1) * theme.rowHeight - theme.px(1.0f);
				marker->position(MV::point(0.0f, markerY));
				marker->show();
			}
		}

		void finishDrag(const MV::Point<int>& a_screenPoint) {
			int target = rowAt(a_screenPoint);
			restoreRowColor(hoverIndex);
			hoverIndex = -1;
			if (auto marker = host->get("insertMarker", false)) {
				marker->hide();
			}
			TItem dragged = dragItem;
			rowDragActive = false;
			if (target >= 0 && !(rowItems[static_cast<size_t>(target)] == dragged) && onReparent) {
				onReparent(dragged, rowItems[static_cast<size_t>(target)], zoneWithinRow(a_screenPoint, target));
			} else if (target < 0 && onDropOutside) {
				onDropOutside(dragged, a_screenPoint);
			}
		}

		bool labelMatches(const TItem& a_item) const {
			std::string label = adapter.label(a_item);
			std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return label.find(filterQuery) != std::string::npos;
		}

		bool subtreeMatches(const TItem& a_item) const {
			if (labelMatches(a_item)) {
				return true;
			}
			for (auto&& child : adapter.children(a_item)) {
				if (subtreeMatches(child)) {
					return true;
				}
			}
			return false;
		}

		void addRow(const TItem& a_item, int a_depth) {
			if (!filterQuery.empty() && !subtreeMatches(a_item)) {
				return;
			}
			auto& mouse = *services.get<MV::TapDevice>();
			bool children = adapter.hasChildren(a_item);
			bool isExpanded = filterQuery.empty()
				? (expanded.count(a_item) ? expanded.at(a_item) : (a_depth < 1))
				: true;
			bool isSelected = hasSelection && selected == a_item;
			bool zebra = (rowIndex++ % 2) == 1;

			auto row = host->make(MV::guid("row_"));
			row->position(MV::point(0.0f, builtHeight));

			MV::Color rowColor = isSelected ? theme.selection() : (zebra ? theme.panelBg() : theme.panelAlt());
			auto rowBackground = row->attach<MV::Scene::Sprite>()->bounds(MV::size(rowWidth, theme.rowHeight))->color(rowColor);
			rowBackgrounds.push_back(rowBackground);
			rowBaseColors.push_back(rowColor);
			rowItems.push_back(a_item);

			float indent = static_cast<float>(a_depth) * theme.treeIndent + theme.pad;
			if (children) {
				auto triangleHost = row->make("fold");
				triangleHost->position(MV::point(indent, 0.0f));
				makeExpanderArrow(triangleHost, services, theme, MV::size(theme.px(16.0f), theme.rowHeight), isExpanded);
				TItem item = a_item;
				triangleHost->attach<MV::Scene::Clickable>(mouse)->bounds(MV::size(theme.px(16.0f), theme.rowHeight))->onAccept.connect("toggle", [this, item, isExpanded](const std::shared_ptr<MV::Scene::Clickable>&) {
					expanded[item] = !isExpanded;
					rebuild();
				});
			}

			float labelX = indent + theme.px(18.0f);
			auto label = makeLabel(row, services, theme, "label", MV::size(std::max(theme.px(40.0f), rowWidth - labelX - theme.pad), theme.rowHeight), adapter.label(a_item));
			label->owner()->position(MV::point(labelX, 0.0f));

			TItem item = a_item;
			auto rowClick = row->attach<MV::Scene::Clickable>(mouse);
			rowClick->bounds(MV::size(rowWidth, theme.rowHeight));
			rowClick->onAccept.connect("select", [this, item](const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
				if (rowDragActive) {
					return;
				}
				auto now = std::chrono::steady_clock::now();
				bool doubleClick = hasSelection && selected == item &&
					std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClickTime).count() < 350;
				lastClickTime = now;
				if (doubleClick && onDoubleClick) {
					onDoubleClick(item);
				} else {
					select(item);
				}
			});
			rowClick->onDrag.connect("rowDrag", [this, item](const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const MV::Point<int>& a_start, const MV::Point<int>&) {
				if (!onReparent) {
					return;
				}
				auto current = a_clickable->mouse().position();
				if (!rowDragActive && std::abs(current.x - a_start.x) + std::abs(current.y - a_start.y) < 6) {
					return;
				}
				rowDragActive = true;
				dragItem = item;
				updateDragFeedback(current);
			});
			rowClick->onRelease.connect("rowDrop", [this](const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const MV::Point<MV::PointPrecision>&) {
				if (!rowDragActive) {
					return;
				}
				finishDrag(a_clickable->mouse().position());
			});

			builtHeight += theme.rowHeight;

			if (children && isExpanded) {
				for (auto&& child : adapter.children(a_item)) {
					addRow(child, a_depth + 1);
				}
			}
		}

		std::shared_ptr<MV::Scene::Node> host;
		MV::Services& services;
		const Theme& theme;
		Adapter adapter;

		TItem rootItem{};
		bool hasRoot = false;
		bool showRoot = true;
		TItem selected{};
		bool hasSelection = false;
		std::map<TItem, bool> expanded;
		std::string filterQuery;
		float rowWidth = 220.0f;
		float builtHeight = 0.0f;
		int rowIndex = 0;
		std::chrono::steady_clock::time_point lastClickTime{};

		std::vector<std::shared_ptr<MV::Scene::Sprite>> rowBackgrounds;
		std::vector<MV::Color> rowBaseColors;
		std::vector<TItem> rowItems;
		int hoverIndex = -1;
		bool rowDragActive = false;
		TItem dragItem{};
		MV::TapDevice::SignalType hoverReceiver;
	};

}

#endif
