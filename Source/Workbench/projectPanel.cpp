#include "projectPanel.h"
#include "widgets.h"
#include "icons.h"
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

namespace Workbench {

	namespace {
		std::vector<fs::path> sortedEntries(const fs::path& a_folder, bool a_directories) {
			std::vector<fs::path> result;
			std::error_code errorCode;
			for (auto&& entry : fs::directory_iterator(a_folder, errorCode)) {
				if (entry.is_directory() == a_directories) {
					result.push_back(entry.path());
				}
			}
			std::sort(result.begin(), result.end());
			return result;
		}

		MV::Color tileColorForExtension(const std::string& a_extension, const Theme& a_theme) {
			if (a_extension == ".scene") { return a_theme.accent(); }
			if (a_extension == ".bindsnap") { return MV::Color(0x2e8b7a); }
			if (a_extension == ".png" || a_extension == ".jpg") { return MV::Color(0x6a5a2e); }
			if (a_extension == ".script" || a_extension == ".jai") { return MV::Color(0x5a3d78); }
			return a_theme.panelAlt();
		}

		std::string tileGlyphForExtension(const std::string& a_extension) {
			if (a_extension == ".scene") { return "scene"; }
			if (a_extension == ".bindsnap") { return "snap"; }
			if (a_extension == ".png" || a_extension == ".jpg") { return "img"; }
			if (a_extension == ".script" || a_extension == ".jai") { return "{}"; }
			if (a_extension == ".ttf") { return "F"; }
			if (a_extension == ".json") { return "J"; }
			return a_extension.empty() ? "?" : a_extension.substr(1);
		}
	}

	void ProjectPanel::build() {
		treeWidth = theme->px(180.0f);
		TreeView<std::string>::Adapter adapter;
		adapter.children = [](const std::string& a_folder) {
			std::vector<std::string> result;
			for (auto&& child : sortedEntries(a_folder, true)) {
				result.push_back(child.generic_string());
			}
			return result;
		};
		adapter.label = [](const std::string& a_folder) {
			return fs::path(a_folder).filename().string();
		};
		adapter.hasChildren = [](const std::string& a_folder) {
			return !sortedEntries(a_folder, true).empty();
		};

		auto treeHost = contentNode->make("folders");
		folderTree = std::make_unique<TreeView<std::string>>(treeHost, *services, *theme, adapter);
		folderTree->onSelect = [this](const std::string& a_folder) {
			navigate(fs::path(a_folder));
		};
		folderTree->root(std::string("Assets"), true);

		breadcrumbHost = contentNode->make("breadcrumb");
		breadcrumbHost->position(MV::point(treeWidth + theme->pad, 0.0f));
		tileHost = contentNode->make("tiles");
		tileHost->position(MV::point(treeWidth + theme->pad, theme->rowHeight + theme->pad));
		buildFooter();

		navigate(currentFolder);
	}

	void ProjectPanel::buildFooter() {
		footerHost = contentNode->make("footer");
		auto background = footerHost->attach<MV::Scene::Sprite>();
		background->id("footerBg");
		background->bounds(MV::size(panelSize.width, footerHeight()));
		background->texture(shadeHandle(*services))->color(theme->header());
		footerHost->attach<MV::Scene::Sprite>()->id("footerEdge")->bounds(MV::size(panelSize.width, theme->px(1.0f)))->color(theme->windowBg());

		float scale = theme->scaleFactor();
		auto& mouse = *services->get<MV::TapDevice>();
		auto sliderHost = footerHost->make("tileSlider");
		auto slider = sliderHost->attach<MV::Scene::Slider>(mouse);
		slider->bounds(MV::size(90.0f * scale, 8.0f * scale));
		slider->color(theme->fieldBg());
		slider->handle(MV::Scene::Node::make(slider->owner()->renderer(), "handle")->attach<MV::Scene::Sprite>()->bounds(MV::size(12.0f * scale, 12.0f * scale))->texture(roundedRectHandle(*services, *theme))->color(theme->buttonIdle())->owner());
		slider->onPercentChange.connect("resizeTiles", [this](const std::shared_ptr<MV::Scene::Slider>& a_slider) {
			tilePercent = a_slider->percent();
			// Fires per mouse-move tick; rebuilding re-walks the directory, so only on real change.
			if (static_cast<int>(tileSizeFor()) != lastBuiltTileSize || listModeFor() != lastBuiltListMode) {
				rebuildTiles();
			}
		});
		slider->percent(tilePercent, false);
		layoutFooter();
	}

	void ProjectPanel::layoutFooter() {
		if (!footerHost) {
			return;
		}
		float width = panelSize.width + theme->pad * 2.0f;
		if (auto background = footerHost->component<MV::Scene::Sprite>("footerBg", false)) {
			background->bounds(MV::size(width, footerHeight()));
		}
		if (auto edge = footerHost->component<MV::Scene::Sprite>("footerEdge", false)) {
			edge->bounds(MV::size(width, theme->px(1.0f)));
		}
		float scale = theme->scaleFactor();
		if (auto sliderHost = footerHost->get("tileSlider", false)) {
			sliderHost->position(MV::point(width - 90.0f * scale - theme->pad * 3.0f, (footerHeight() - 8.0f * scale) * 0.5f));
		}
	}

	// The names in the folder shown, to tell when something outside the editor changes it.
	std::string ProjectPanel::folderListing() const {
		std::string result;
		std::error_code failed;
		for (auto&& entry : fs::directory_iterator(currentFolder, failed)) {
			result += entry.path().filename().string();
			result += '\n';
		}
		return result;
	}

	void ProjectPanel::update(double a_dt) {
		if (!footerHost || !contentNode) {
			return;
		}
		listingWait += a_dt;
		if (rebuildWanted && !renaming()) {
			rebuildWanted = false;
			rebuildTiles();
		} else if (listingWait >= 0.5 && !renaming()) {
			listingWait = 0.0;
			if (folderListing() != shownListing) {
				refresh();
			}
		}
		// The dock wheel-scrolls the whole content node; counter-scroll to pin the footer.
		float offset = scrolled();
		footerHost->position(MV::point(-theme->pad, offset + panelSize.height - footerHeight() + theme->pad));
	}

	void ProjectPanel::resized(const MV::Size<>& a_size) {
		panelSize = a_size;
		if (folderTree) {
			folderTree->width(treeWidth - theme->pad);
		}
		layoutFooter();
		rebuildTiles();
	}

	void ProjectPanel::navigate(const fs::path& a_folder) {
		currentFolder = a_folder;
		rebuildBreadcrumb();
		rebuildTiles();
	}

	void ProjectPanel::refresh() {
		if (folderTree) {
			folderTree->rebuild();
		}
		rebuildTiles();
	}

	std::string ProjectPanel::folderAt(const MV::Point<int>& a_screenPoint) const {
		if (auto* folder = folderTree ? folderTree->itemAt(a_screenPoint) : nullptr) {
			return *folder;
		}
		for (auto&& entry : shownEntries) {
			auto node = entry.node.lock();
			if (entry.folder && node && node->screenFromLocal(node->bounds(false)).contains(a_screenPoint)) {
				return entry.path.generic_string();
			}
		}
		return currentFolder.generic_string();
	}

	void ProjectPanel::markDropFolder(const std::string& a_folder) {
		if (folderTree) {
			folderTree->markDropTarget(a_folder.empty() ? nullptr : &a_folder);
		}
		bool onTile = false;
		for (auto&& entry : shownEntries) {
			if (auto mark = entry.dropMark.lock()) {
				const bool marked = !a_folder.empty() && entry.path.generic_string() == a_folder;
				if (marked) {
					mark->show();
				} else {
					mark->hide();
				}
				onTile = onTile || marked;
			}
		}
		const bool wholeFolder = !a_folder.empty() && !onTile && a_folder == currentFolder.generic_string() && contentNode;
		if (!wholeFolder && dropAreaMark) {
			dropAreaMark->removeFromParent();
			dropAreaMark.reset();
		} else if (wholeFolder && !dropAreaMark) {
			const float edge = theme->px(2.0f);
			const float top = theme->rowHeight + theme->pad;
			MV::Color wash = theme->dropHint();
			wash.A = 0.12f;
			dropAreaMark = contentNode->make("dropArea");
			dropAreaMark->position(MV::point(treeWidth + theme->pad - edge, scrolled() + top - edge));
			const MV::Size<> area(std::max(theme->px(40.0f), panelSize.width - treeWidth - theme->pad * 2.0f + edge * 2.0f), std::max(theme->px(20.0f), panelSize.height - top - footerHeight() + edge));
			dropAreaMark->attach<MV::Scene::Sprite>()->bounds(area)->color(wash);
			// a frame of four thin sprites: the tiles show through the middle
			const MV::BoxAABB<> sides[] {
				MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::size(area.width, edge)),
				MV::BoxAABB<>(MV::point(0.0f, area.height - edge), MV::size(area.width, edge)),
				MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::size(edge, area.height)),
				MV::BoxAABB<>(MV::point(area.width - edge, 0.0f), MV::size(edge, area.height)) };
			for (auto&& side : sides) {
				dropAreaMark->attach<MV::Scene::Sprite>()->bounds(side)->color(theme->accent());
			}
		}
	}

	std::optional<fs::path> ProjectPanel::entryAt(const MV::Point<int>& a_screenPoint) const {
		for (auto&& entry : shownEntries) {
			auto node = entry.node.lock();
			if (node && node->screenFromLocal(node->bounds(false)).contains(a_screenPoint)) {
				return entry.path;
			}
		}
		return std::nullopt;
	}

	bool ProjectPanel::beginRename(const fs::path& a_entry) {
		for (auto&& entry : shownEntries) {
			auto node = entry.node.lock();
			auto caption = entry.caption.lock();
			if (!node || !caption || entry.path != a_entry) {
				continue;
			}
			caption->hide();
			const fs::path target = entry.path;
			const std::string extension = entry.folder ? std::string() : target.extension().string();
			auto field = makeTextField(node, *services, *theme, *focus, "rename", MV::size(entry.captionSize.width, theme->fieldHeight), entry.folder ? target.filename().string() : target.stem().string(), [this, target, extension](const std::string& a_typed) {
				rebuildWanted = true;
				if (onRename && !a_typed.empty() && a_typed + extension != target.filename().string()) {
					onRename(target, a_typed + extension);
				}
			}, true);
			auto box = field->owner()->parent();
			box->position(caption->position());
			renameField = field;
			if (auto clickable = box->component<MV::Scene::Clickable>(false, false)) {
				// leaving the field without Enter puts the caption back
				clickable->onFocusLeave.connect("endRename", [this](const std::shared_ptr<MV::Scene::Clickable>&) {
					rebuildWanted = true;
					renameField.reset();
				});
				clickable->focus();
			}
			return true;
		}
		return false;
	}

	MV::Point<int> ProjectPanel::tileAreaScreenPoint() const {
		return contentNode->screenFromLocal(MV::point(panelSize.width - theme->pad * 6.0f, scrolled() + panelSize.height - footerHeight() - theme->pad * 2.0f));
	}

	std::optional<MV::Point<int>> ProjectPanel::entryScreenPoint(const std::string& a_name) const {
		for (auto&& entry : shownEntries) {
			auto node = entry.node.lock();
			if (node && entry.path.filename().string() == a_name) {
				return node->screenFromLocal(node->bounds(false)).centerPoint();
			}
		}
		return std::nullopt;
	}

	void ProjectPanel::openImagePreview(const std::string& a_fileName) {
		if (!popupNode) {
			return;
		}
		popupNode->clear();
		try {
			auto handle = services->get<MV::SharedTextures>()->file(a_fileName, false)->makeHandle();
			auto worldSize = contentNode->renderer().world().size();
			MV::Size<> maxSize(worldSize.width * 0.6f, worldSize.height * 0.6f);
			auto fitted = MV::fitAspect(MV::cast<MV::PointPrecision>(handle->bounds().size()), maxSize);
			auto popup = popupNode->make("imagePreview");
			popup->position(MV::point((worldSize.width - fitted.width) * 0.5f, (worldSize.height - fitted.height) * 0.5f));
			popup->attach<MV::Scene::Sprite>()->bounds(MV::BoxAABB<>(MV::point(-theme->pad, -theme->pad), MV::Size<>(fitted.width + theme->pad * 2.0f, fitted.height + theme->pad * 2.0f)))->color(theme->header());
			popup->attach<MV::Scene::Sprite>()->bounds(fitted)->texture(handle);
			auto& mouse = *services->get<MV::TapDevice>();
			popup->attach<MV::Scene::Clickable>(mouse)->bounds(fitted)->onAccept.connect("close", [](const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
				a_clickable->owner()->removeFromParent();
			});
		} catch (const std::exception& a_error) {
			std::cerr << "Workbench image preview failed (" << a_fileName << "): " << a_error.what() << std::endl;
		}
	}

	void ProjectPanel::rebuildBreadcrumb() {
		if (!breadcrumbHost) {
			return;
		}
		breadcrumbHost->clear();
		float x = 0.0f;
		fs::path accumulated;
		for (auto&& part : currentFolder) {
			accumulated /= part;
			std::string segment = part.string();
			fs::path target = accumulated;
			float scale = theme->scaleFactor();
			float segmentWidth = std::max(30.0f * scale, segment.size() * 7.0f * scale + 12.0f * scale);
			auto button = makeButton(breadcrumbHost, *services, *theme, MV::guid("crumb_"), MV::size(segmentWidth, theme->rowHeight), segment, [this, target]() {
				navigate(target);
			});
			button->owner()->position(MV::point(x, 0.0f));
			x += segmentWidth + theme->pad;
		}
	}

	void ProjectPanel::wireEntryInteractions(const std::shared_ptr<MV::Scene::Clickable>& a_click, const fs::path& a_target, bool a_isFolder, bool a_draggable) {
		fs::path target = a_target;
		auto lastClick = std::make_shared<std::chrono::steady_clock::time_point>();
		auto dragging = std::make_shared<bool>(false);
		a_click->onAccept.connect("open", [this, target, a_isFolder, lastClick, dragging](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (*dragging) {
				*dragging = false;
				return;
			}
			auto now = std::chrono::steady_clock::now();
			bool doubleClick = std::chrono::duration_cast<std::chrono::milliseconds>(now - *lastClick).count() < 350;
			*lastClick = now;
			if (!doubleClick) {
				return;
			}
			if (a_isFolder) {
				navigate(target);
			} else {
				std::string extension = target.extension().string();
				if ((extension == ".scene" || extension == ".bindsnap") && openScene) {
					openScene(target.generic_string());
				} else if (extension == ".png" || extension == ".jpg") {
					openImagePreview(target.generic_string());
				}
			}
		});
		if (a_draggable) {
			a_click->onDrag.connect("dragOut", [dragging](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<int>&, const MV::Point<int>&) {
				*dragging = true;
			});
			a_click->onRelease.connect("dropOut", [this, target, dragging](const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const MV::Point<MV::PointPrecision>&) {
				if (*dragging && dropFile) {
					dropFile(target.generic_string(), a_clickable->mouse().position());
				}
			});
		}
	}

	void ProjectPanel::rebuildTiles() {
		if (!tileHost) {
			return;
		}
		tileHost->clear();
		shownEntries.clear();
		renameField.reset();
		shownListing = folderListing();
		if (dropAreaMark) {
			dropAreaMark->removeFromParent();
			dropAreaMark.reset();
		}
		float scale = theme->scaleFactor();
		float tileSize = tileSizeFor();
		bool listMode = listModeFor();
		lastBuiltTileSize = static_cast<int>(tileSize);
		lastBuiltListMode = listMode;

		std::vector<fs::path> entries = sortedEntries(currentFolder, true);
		auto files = sortedEntries(currentFolder, false);
		entries.insert(entries.end(), files.begin(), files.end());

		auto& mouse = *services->get<MV::TapDevice>();
		int index = 0;

		if (listMode) {
			float rowWidth = std::max(theme->px(120.0f), panelSize.width - treeWidth - theme->pad * 2.0f);
			float iconSize = theme->rowHeight - theme->px(4.0f);
			for (auto&& entry : entries) {
				bool isFolder = fs::is_directory(entry);
				std::string extension = entry.extension().string();
				bool isImage = extension == ".png" || extension == ".jpg";
				// a scene is placed in another as a BindSnap is: which a file is called is convention
				bool draggable = extension == ".bindsnap" || extension == ".scene" || !MV::shaderIdOfFile(entry.string()).empty();

				auto row = tileHost->make(MV::guid("listRow_"));
				row->position(MV::point(0.0f, index * theme->rowHeight));
				row->attach<MV::Scene::Sprite>()->bounds(MV::size(rowWidth, theme->rowHeight))->color(index % 2 == 0 ? theme->panelAlt() : theme->panelBg());

				auto icon = row->make("icon");
				icon->position(MV::point(theme->px(2.0f), theme->px(2.0f)));
				if (isImage) {
					try {
						auto handle = services->get<MV::SharedTextures>()->file(entry.generic_string(), false)->makeHandle();
						icon->attach<MV::Scene::Sprite>()->bounds(MV::fitAspect(MV::cast<MV::PointPrecision>(handle->bounds().size()), MV::size(iconSize, iconSize)))->texture(handle);
					} catch (...) {
					}
				} else if (isFolder) {
					icon->attach<MV::Scene::Sprite>()->bounds(MV::size(iconSize, iconSize))->texture(folderHandle(*services));
				} else {
					icon->attach<MV::Scene::Sprite>()->bounds(MV::size(iconSize, iconSize))->color(tileColorForExtension(extension, *theme));
				}

				auto label = makeLabel(row, *services, *theme, "caption", MV::size(rowWidth - iconSize - theme->pad * 2.0f, theme->rowHeight), entry.filename().string());
				label->owner()->position(MV::point(iconSize + theme->pad, 0.0f));

				std::shared_ptr<MV::Scene::Node> dropMark;
				if (isFolder) {
					dropMark = row->make("dropMark");
					dropMark->attach<MV::Scene::Sprite>()->bounds(MV::size(rowWidth, theme->rowHeight))->color(theme->dropHint());
					dropMark->hide();
				}
				shownEntries.push_back({ entry, isFolder, row, dropMark, label->owner(), MV::size(rowWidth - iconSize - theme->pad * 2.0f, theme->rowHeight) });

				auto rowClick = row->attach<MV::Scene::Clickable>(mouse)->bounds(MV::size(rowWidth, theme->rowHeight));
				hoverTint(rowClick, row->component<MV::Scene::Sprite>(false, false).self(), index % 2 == 0 ? theme->panelAlt() : theme->panelBg(), theme->rowHover());
				wireEntryInteractions(rowClick, entry, isFolder, draggable);
				++index;
			}
			builtHeight = theme->rowHeight + theme->pad + index * theme->rowHeight + footerHeight();
			return;
		}

		float captionHeight = 18.0f * scale;
		float cellWidth = tileSize + 10.0f * scale;
		float cellHeight = tileSize + captionHeight + 10.0f * scale;
		float availableWidth = std::max(cellWidth, panelSize.width - treeWidth - theme->pad * 2.0f);
		int columns = std::max(1, static_cast<int>(availableWidth / cellWidth));
		cellWidth = std::floor(availableWidth / static_cast<float>(columns));
		float tileInset = std::floor((cellWidth - tileSize) * 0.5f);

		for (auto&& entry : entries) {
			bool isFolder = fs::is_directory(entry);
			int column = index % columns;
			int row = index / columns;
			++index;

			auto tile = tileHost->make(MV::guid("tile_"));
			tile->position(MV::point(column * cellWidth + tileInset, row * cellHeight));
			auto underPointer = tile->make("hover");
			underPointer->position(MV::point(-theme->px(4.0f), -theme->px(4.0f)));
			auto hoverBack = attachRoundedRect(underPointer, *services, *theme, MV::size(tileSize + theme->px(8.0f), tileSize + captionHeight + theme->px(8.0f)), theme->rowHover());

			std::shared_ptr<MV::Scene::Node> dropMark;
			if (isFolder) {
				dropMark = tile->make("dropMark");
				dropMark->position(MV::point(-theme->px(4.0f), -theme->px(4.0f)));
				attachRoundedRect(dropMark, *services, *theme, MV::size(tileSize + theme->px(8.0f), tileSize + captionHeight + theme->px(8.0f)), theme->dropHint());
				dropMark->hide();
			}
			const size_t shownIndex = shownEntries.size();
			shownEntries.push_back({ entry, isFolder, tile, dropMark });

			std::string extension = entry.extension().string();
			bool isImage = extension == ".png" || extension == ".jpg";
			bool draggable = extension == ".bindsnap" || extension == ".scene" || !MV::shaderIdOfFile(entry.string()).empty();

			if (isImage) {
				attachOutlinedRect(tile, *services, *theme, MV::size(tileSize, tileSize), theme->fieldBg(), theme->fieldOutline());
				try {
					auto handle = services->get<MV::SharedTextures>()->file(entry.generic_string(), false)->makeHandle();
					tile->make("thumb")->attach<MV::Scene::Sprite>()->bounds(MV::fitAspect(MV::cast<MV::PointPrecision>(handle->bounds().size()), MV::size(tileSize, tileSize)))->texture(handle);
				} catch (...) {
				}
			} else if (isFolder) {
				tile->attach<MV::Scene::Sprite>()->bounds(MV::size(tileSize, tileSize))->texture(folderHandle(*services));
			} else {
				// a page in the file type's colour, with its short name on it
				auto pageColor = tileColorForExtension(extension, *theme);
				tile->make("page")->attach<MV::Scene::Sprite>()->bounds(MV::size(tileSize, tileSize))->texture(iconHandle(*services, *theme, Icon::Document, 128))->color(MV::Color(std::min(1.0f, pageColor.R + 0.18f), std::min(1.0f, pageColor.G + 0.18f), std::min(1.0f, pageColor.B + 0.18f), 1.0f));
				auto glyphLabel = makeLabel(tile, *services, *theme, "glyph", MV::size(tileSize, theme->rowHeight), tileGlyphForExtension(extension));
				glyphLabel->justification(MV::TextJustification::CENTER);
				glyphLabel->owner()->position(MV::point(0.0f, (tileSize - theme->rowHeight) * 0.58f));
			}

			auto caption = makeLabel(tile, *services, *theme, "caption", MV::size(cellWidth - theme->pad, captionHeight), entry.filename().string());
			caption->owner()->position(MV::point(-tileInset + theme->pad * 0.5f, tileSize + theme->px(2.0f)));
			caption->justification(MV::TextJustification::CENTER);
			shownEntries[shownIndex].caption = caption->owner();
			shownEntries[shownIndex].captionSize = MV::size(cellWidth - theme->pad, captionHeight);

			auto tileClick = tile->attach<MV::Scene::Clickable>(mouse)->bounds(MV::size(tileSize, tileSize + captionHeight));
			hoverShow(tileClick, hoverBack);
			wireEntryInteractions(tileClick, entry, isFolder, draggable);
		}
		builtHeight = theme->rowHeight + theme->pad + ((index + columns - 1) / std::max(1, columns)) * cellHeight + footerHeight();
	}

}
