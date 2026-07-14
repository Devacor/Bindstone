#include "projectPanel.h"
#include "widgets.h"
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
			if (a_extension == ".scene") { return "S"; }
			if (a_extension == ".bindsnap") { return "P"; }
			if (a_extension == ".png" || a_extension == ".jpg") { return "img"; }
			if (a_extension == ".script" || a_extension == ".jai") { return "{}"; }
			if (a_extension == ".ttf") { return "F"; }
			if (a_extension == ".json") { return "J"; }
			return a_extension.empty() ? "?" : a_extension.substr(1);
		}
	}

	void ProjectPanel::build() {
		treeWidth = theme->tabWidth * 1.6f;
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
		background->color(theme->header());

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
		float width = std::max(120.0f, panelSize.width - treeWidth - theme->pad);
		if (auto background = footerHost->component<MV::Scene::Sprite>("footerBg", false)) {
			background->bounds(MV::size(width, footerHeight()));
		}
		float scale = theme->scaleFactor();
		if (auto sliderHost = footerHost->get("tileSlider", false)) {
			sliderHost->position(MV::point(width - 90.0f * scale - theme->pad, (footerHeight() - 8.0f * scale) * 0.5f));
		}
	}

	void ProjectPanel::update(double) {
		if (!footerHost || !contentNode) {
			return;
		}
		// The dock wheel-scrolls the whole content node; counter-scroll to pin the footer.
		float offset = theme->tabHeight + theme->pad - contentNode->position().y;
		footerHost->position(MV::point(treeWidth + theme->pad, offset + panelSize.height - footerHeight()));
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
			popup->attach<MV::Scene::Sprite>()->bounds(MV::BoxAABB<>(MV::point(-4.0f, -4.0f), MV::Size<>(fitted.width + 8.0f, fitted.height + 8.0f)))->color(theme->header());
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
			auto button = makeButton(breadcrumbHost, *services, *theme, MV::guid("crumb_"), MV::size(segmentWidth, theme->rowHeight - 2.0f), segment, [this, target]() {
				navigate(target);
			});
			button->owner()->position(MV::point(x, 0.0f));
			x += segmentWidth + 4.0f;
		}
	}

	void ProjectPanel::wireEntryInteractions(const std::shared_ptr<MV::Scene::Clickable>& a_click, const fs::path& a_target, bool a_isFolder, bool a_isPrefab) {
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
		if (a_isPrefab) {
			a_click->onDrag.connect("dragOut", [dragging](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<int>&, const MV::Point<int>&) {
				*dragging = true;
			});
			a_click->onRelease.connect("dropOut", [this, target, dragging](const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const MV::Point<MV::PointPrecision>&) {
				if (*dragging && dropPrefab) {
					dropPrefab(target.generic_string(), a_clickable->mouse().position());
				}
			});
		}
	}

	void ProjectPanel::rebuildTiles() {
		if (!tileHost) {
			return;
		}
		tileHost->clear();
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
			float rowWidth = std::max(120.0f, panelSize.width - treeWidth - theme->pad * 2.0f);
			float iconSize = theme->rowHeight - 4.0f;
			for (auto&& entry : entries) {
				bool isFolder = fs::is_directory(entry);
				std::string extension = entry.extension().string();
				bool isImage = extension == ".png" || extension == ".jpg";
				bool isPrefab = extension == ".bindsnap";

				auto row = tileHost->make(MV::guid("listRow_"));
				row->position(MV::point(0.0f, index * theme->rowHeight));
				row->attach<MV::Scene::Sprite>()->bounds(MV::size(rowWidth, theme->rowHeight - 1.0f))->color(index % 2 == 0 ? theme->panelAlt() : theme->panelBg());

				auto icon = row->make("icon");
				icon->position(MV::point(2.0f, 2.0f));
				if (isImage) {
					try {
						auto handle = services->get<MV::SharedTextures>()->file(entry.generic_string(), false)->makeHandle();
						icon->attach<MV::Scene::Sprite>()->bounds(MV::fitAspect(MV::cast<MV::PointPrecision>(handle->bounds().size()), MV::size(iconSize, iconSize)))->texture(handle);
					} catch (...) {
					}
				} else {
					MV::Color iconColor = isFolder ? MV::Color(0x8a6d3a) : tileColorForExtension(extension, *theme);
					icon->attach<MV::Scene::Sprite>()->bounds(MV::size(iconSize, iconSize))->color(iconColor);
				}

				auto label = makeLabel(row, *services, *theme, "caption", MV::size(rowWidth - iconSize - theme->pad * 2.0f, theme->rowHeight), entry.filename().string());
				label->owner()->position(MV::point(iconSize + theme->pad, 0.0f));

				auto rowClick = row->attach<MV::Scene::Clickable>(mouse)->bounds(MV::size(rowWidth, theme->rowHeight - 1.0f));
				wireEntryInteractions(rowClick, entry, isFolder, isPrefab);
				++index;
			}
			builtHeight = theme->rowHeight + theme->pad + index * theme->rowHeight + footerHeight();
			return;
		}

		float captionHeight = 14.0f * scale;
		float cellWidth = tileSize + 10.0f * scale;
		float cellHeight = tileSize + captionHeight + 10.0f * scale;
		float availableWidth = std::max(cellWidth, panelSize.width - treeWidth - theme->pad * 2.0f);
		int columns = std::max(1, static_cast<int>(availableWidth / cellWidth));

		for (auto&& entry : entries) {
			bool isFolder = fs::is_directory(entry);
			int column = index % columns;
			int row = index / columns;
			++index;

			auto tile = tileHost->make(MV::guid("tile_"));
			tile->position(MV::point(column * cellWidth, row * cellHeight));

			std::string extension = entry.extension().string();
			bool isImage = extension == ".png" || extension == ".jpg";
			bool isPrefab = extension == ".bindsnap";

			if (isImage) {
				tile->attach<MV::Scene::Sprite>()->bounds(MV::size(tileSize, tileSize))->color(theme->fieldBg());
				try {
					auto handle = services->get<MV::SharedTextures>()->file(entry.generic_string(), false)->makeHandle();
					tile->make("thumb")->attach<MV::Scene::Sprite>()->bounds(MV::fitAspect(MV::cast<MV::PointPrecision>(handle->bounds().size()), MV::size(tileSize, tileSize)))->texture(handle);
				} catch (...) {
				}
			} else {
				MV::Color tileColor = isFolder ? MV::Color(0x8a6d3a) : tileColorForExtension(extension, *theme);
				tile->attach<MV::Scene::Sprite>()->bounds(MV::size(tileSize, tileSize))->color(tileColor);
				std::string glyph = isFolder ? "[=]" : tileGlyphForExtension(extension);
				auto glyphLabel = makeLabel(tile, *services, *theme, "glyph", MV::size(tileSize, theme->rowHeight), glyph);
				glyphLabel->justification(MV::TextJustification::CENTER);
				glyphLabel->owner()->position(MV::point(0.0f, (tileSize - theme->rowHeight) * 0.5f));
			}

			auto caption = makeLabel(tile, *services, *theme, "caption", MV::size(cellWidth - 4.0f, captionHeight), entry.filename().string());
			caption->owner()->position(MV::point(0.0f, tileSize + 2.0f));
			caption->justification(MV::TextJustification::CENTER);

			auto tileClick = tile->attach<MV::Scene::Clickable>(mouse)->bounds(MV::size(tileSize, tileSize + captionHeight));
			wireEntryInteractions(tileClick, entry, isFolder, isPrefab);
		}
		builtHeight = theme->rowHeight + theme->pad + ((index + columns - 1) / std::max(1, columns)) * cellHeight + footerHeight();
	}

}
