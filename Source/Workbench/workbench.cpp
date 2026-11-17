#include "workbench.h"
#include "projectPanel.h"
#include "scriptPanel.h"
#include "fileDialog.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <thread>
#include <vector>

namespace Workbench {

	namespace {
		std::optional<float> numberFrom(const std::string& a_text) {
			float result = 0.0f;
			auto parsed = std::from_chars(a_text.data(), a_text.data() + a_text.size(), result);
			return parsed.ec == std::errc() && parsed.ptr == a_text.data() + a_text.size() ? std::optional<float>(result) : std::nullopt;
		}

		class ViewportPanel : public Panel {
		public:
			ViewportPanel() : Panel("Scene", true) {}
		};

		constexpr float minimumZoom = 0.05f;
		constexpr float maximumZoom = 20.0f;
	}

	App::App(Managers& a_managers, MV::TapDevice& a_mouse, const Theme& a_theme) :
		managers(a_managers),
		mouse(a_mouse),
		theme(a_theme),
		visor(MV::Scene::Node::make(a_managers.renderer, "workbenchVisor")),
		uiRoot(MV::Scene::Node::make(a_managers.renderer, "workbenchUi")) {

		visor->cameraId(99);
		viewportClip = visor->make("viewportClip");
		viewportStencil = viewportClip->attach<MV::Scene::Stencil>();
		viewportStencil->id("sceneClip");
		// made before the camera, so it draws behind the scene
		sceneGrid = std::make_unique<SceneGrid>(viewportClip->make("sceneGrid"), theme);
		viewCamera = viewportClip->make("viewCamera");
		sceneRoot = viewCamera->make("root");
		// Sibling after the camera: draws above the scene, stencil-clipped, never serialized, and
		// stays unscaled so handles keep pixel size at any zoom.
		gizmoOverlay = viewportClip->make("gizmoOverlay");
		gizmos = std::make_unique<GizmoLayer>(gizmoOverlay, mouse, history, theme, managers.textLibrary);
		gizmos->onHandlePressed = [this]() {
			if (panButton == PanButton::LEFT) {
				panButton = PanButton::NONE;
			}
		};
		gizmos->onEdited = [this]() {
			if (inspectorPanel) {
				inspectorPanel->inspect(selectedNode);
			}
		};

		cursorArrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
		cursorResizeH = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
		cursorResizeV = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);

		auto panWith = [this](PanButton a_button, jai::signal<MV::TapDevice::CallbackSignature>& a_down, jai::signal<MV::TapDevice::CallbackSignature>& a_up) {
			panReceivers.push_back(a_down.connect([this, a_button](MV::TapDevice& a_mouse) { beginPan(a_button, a_mouse.position()); }));
			panReceivers.push_back(a_up.connect([this, a_button](MV::TapDevice& a_mouse) { endPan(a_button, a_mouse.position()); }));
		};
		panWith(PanButton::LEFT, mouse.onLeftMouseDown, mouse.onLeftMouseUp);
		panWith(PanButton::MIDDLE, mouse.onMiddleMouseDown, mouse.onMiddleMouseUp);
		panWith(PanButton::RIGHT, mouse.onRightMouseDown, mouse.onRightMouseUp);
		panReceivers.push_back(mouse.onMove.connect([this](MV::TapDevice& a_mouse) {
			if (panButton == PanButton::NONE) {
				return;
			}
			auto current = a_mouse.position();
			if (std::abs(current.x - panStart.x) + std::abs(current.y - panStart.y) > 4) {
				panMoved = true;
			}
			viewCamera->translate(managers.renderer.worldFromScreen(current - panLast));
			panLast = current;
		}));

		mouse.trackHover(true);
		history.onAfterChange = [this]() {
			if (treePanel) {
				treePanel->refresh();
			}
			select(inScene(selectedNode) ? selectedNode : nullptr);
		};
		history.onMoved = [this]() {
			refreshTitle();
			if (sceneToolbar) {
				sceneToolbar->history().refresh();
			}
		};
		askAboutUnsavedChanges = [](const std::string& a_documentName) {
			const SDL_MessageBoxButtonData buttons[] {
				{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Save" },
				{ 0, 1, "Don't Save" },
				{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 2, "Cancel" }
			};
			std::string message = "Save changes to " + a_documentName + "?";
			SDL_MessageBoxData box { SDL_MESSAGEBOX_WARNING, SDL_GetKeyboardFocus(), "Bindstone Workbench", message.c_str(), 3, buttons, nullptr };
			int pressed = 2;
			if (SDL_ShowMessageBox(&box, &pressed) != 0) {
				return UnsavedChoice::CANCEL;
			}
			return pressed == 0 ? UnsavedChoice::SAVE : pressed == 1 ? UnsavedChoice::DISCARD : UnsavedChoice::CANCEL;
		};

		askToConfirm = [](const std::string& a_message) {
			const SDL_MessageBoxButtonData buttons[] {
				{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Okay" },
				{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "Cancel" }
			};
			SDL_MessageBoxData box { SDL_MESSAGEBOX_WARNING, SDL_GetKeyboardFocus(), "Bindstone Workbench", a_message.c_str(), 2, buttons, nullptr };
			int pressed = 1;
			return SDL_ShowMessageBox(&box, &pressed) == 0 && pressed == 0;
		};

		tooltips = std::make_unique<Tooltips>(managers.services, theme);
		managers.services.connect(tooltips.get());
		dock = std::make_unique<DockSpace>(uiRoot, managers.services, theme, focusRouter);
		auto tree = std::make_shared<SceneTreePanel>(history,
			[this]() { return sceneRoot; },
			[this](const std::shared_ptr<MV::Scene::Node>& a_node) { select(a_node); });
		auto inspector = std::make_shared<InspectorPanel>(history);
		auto viewport = std::make_shared<ViewportPanel>();
		auto file = std::make_shared<ScriptPanel>("File", "Assets/Workbench/filePanel.jai");
		file->onLoadScene = [this](const std::string& a_fileName) { requestLoadScene(a_fileName); };
		file->onSaveScene = [this](const std::string& a_fileName) { saveScene(a_fileName); };
		file->onError = [this](const std::string& a_message) { reportError(a_message); };
		auto project = std::make_shared<ProjectPanel>(
			[this](const std::string& a_fileName) {
				requestLoadScene(a_fileName);
			},
			[this](const std::string& a_fileName, const MV::Point<int>& a_screenPoint) {
				dropPrefab(a_fileName, a_screenPoint);
			});

		treePanel = static_cast<SceneTreePanel*>(dock->registerPanel(std::move(tree)));
		inspectorPanel = static_cast<InspectorPanel*>(dock->registerPanel(std::move(inspector)));
		inspectorPanel->onEditComponent = [this](const std::shared_ptr<MV::Scene::Component>& a_component) {
			gizmos->editComponent(gizmos->editedComponent() == a_component ? nullptr : a_component);
			inspectorPanel->inspect(selectedNode);
		};
		inspectorPanel->componentGizmoActive = [this](const std::shared_ptr<MV::Scene::Component>& a_component) {
			return gizmos->editedComponent() == a_component;
		};
		inspectorPanel->onParallaxPreview = [this](bool a_preview) { parallaxPreview(a_preview); };
		inspectorPanel->onSelectNode = [this](const std::shared_ptr<MV::Scene::Node>& a_node) { select(a_node); };
		treePanel->onNodeDroppedOutside = [this](const std::shared_ptr<MV::Scene::Node>& a_node, const MV::Point<int>& a_screenPoint) {
			inspectorPanel->dropNode(a_node, a_screenPoint);
		};
		inspectorPanel->parallaxPreviewActive = [this]() { return parallaxPreviewing(); };
		std::vector<SettingsPanel::Row> settingRows {
			{ "Undo steps", [this]() { return std::to_string(undoSteps()); }, [this](const std::string& a_text) {
				if (auto steps = numberFrom(a_text); steps && *steps >= 1.0f) {
					requestUndoSteps(static_cast<size_t>(*steps));
				}
			} },
			{ "Grid X", [this]() { return formatNumberCompact(sceneGridSize.width); }, [this](const std::string& a_text) {
				if (auto width = numberFrom(a_text); width && *width > 0.0f) {
					gridSize(MV::size(*width, sceneGridSize.height));
				}
			} },
			{ "Grid Y", [this]() { return formatNumberCompact(sceneGridSize.height); }, [this](const std::string& a_text) {
				if (auto height = numberFrom(a_text); height && *height > 0.0f) {
					gridSize(MV::size(sceneGridSize.width, *height));
				}
			} },
			{ "Rotation snap", [this]() { return formatNumberCompact(rotationSnap()); }, [this](const std::string& a_text) {
				if (auto degrees = numberFrom(a_text); degrees && *degrees > 0.0f) {
					rotationSnap(*degrees);
				}
			} }
		};
		settingsPanel = static_cast<SettingsPanel*>(dock->registerPanel(std::make_shared<SettingsPanel>(std::move(settingRows))));
		Panel* viewportPanel = dock->registerPanel(std::move(viewport));
		Panel* filePanel = dock->registerPanel(std::move(file));
		Panel* projectPanel = dock->registerPanel(std::move(project));

		menuPanels = { treePanel, filePanel, viewportPanel, projectPanel, inspectorPanel, settingsPanel };

		SceneToolbar::Actions toolbarActions;
		toolbarActions.onTool = [this](int a_toolIndex) { gizmoTool(static_cast<Tool>(a_toolIndex)); };
		toolbarActions.onSnapToggle = [this]() {
			gizmos->snapAlways = !gizmos->snapAlways;
			sceneToolbar->snap(gizmos->snapAlways);
		};
		toolbarActions.onGridToggle = [this]() {
			sceneGrid->visible(!sceneGrid->visible());
			sceneToolbar->grid(sceneGrid->visible());
		};
		toolbarActions.onFrame = [this]() { frameSelectionInViewport(); };
		sceneToolbar = std::make_unique<SceneToolbar>(uiRoot->make("sceneToolbar"), managers.services, theme, focusRouter, history, toolbarActions);
		dock->floatingHost(uiRoot->make("dockFloating"));
		gizmos->pointerOverScene = [this](const MV::Point<int>& a_screenPoint) {
			auto worldPoint = managers.renderer.worldFromScreen(a_screenPoint);
			return dock->viewportAt(worldPoint) && !sceneToolbar->contains(worldPoint);
		};
		sceneToolbar->tool(static_cast<int>(gizmos->tool()));
		sceneToolbar->grid(sceneGrid->visible());
		applyGridSize();
		gizmos->onEditing = [this]() {
			if (inspectorPanel) {
				inspectorPanel->inspect(selectedNode);
			}
		};
		dock->root(DockSpace::split(false, 0.18f,
			DockSpace::leaf({ treePanel }),
			DockSpace::split(false, 0.72f,
				DockSpace::split(true, 0.70f,
					DockSpace::leaf({ viewportPanel }),
					DockSpace::leaf({ projectPanel })),
				DockSpace::leaf({ inspectorPanel }))));
		// File's load and save are in the File menu and Settings opens from its own: both start closed
		defaultDockLayout = dock->describeLayout() + " X [File,Settings]";
		dock->applyLayout(defaultDockLayout);
		dock->onLayoutChanged = [this]() { writeSettingsFile(); };

		buildTitleBar();
		tooltips->host(uiRoot->make("tooltips"));
	}

	App::~App() {
		managers.services.disconnect<Tooltips>();
		SDL_SetCursor(SDL_GetDefaultCursor());
		for (auto&& cursor : { cursorArrow, cursorResizeH, cursorResizeV }) {
			if (cursor) {
				SDL_FreeCursor(cursor);
			}
		}
	}

	void App::gizmoTool(Tool a_tool) {
		gizmos->tool(a_tool);
		if (sceneToolbar) {
			sceneToolbar->tool(static_cast<int>(gizmos->tool()));
		}
	}

	void App::beginPan(PanButton a_button, const MV::Point<int>& a_screenPoint) {
		auto worldPoint = managers.renderer.worldFromScreen(a_screenPoint);
		if (sceneToolbar && sceneToolbar->contains(worldPoint)) {
			return;
		}
		if (panButton == PanButton::NONE && dock->viewportAt(worldPoint)) {
			panButton = a_button;
			panMoved = false;
			panStart = a_screenPoint;
			panLast = a_screenPoint;
		}
	}

	void App::endPan(PanButton a_button, const MV::Point<int>& a_screenPoint) {
		if (panButton != a_button) {
			return;
		}
		panButton = PanButton::NONE;
		if (a_button == PanButton::LEFT && !panMoved) {
			pickSceneNode(a_screenPoint);
		}
	}

	void App::pickSceneNode(const MV::Point<int>& a_screenPoint) {
		std::function<std::shared_ptr<MV::Scene::Node>(const std::shared_ptr<MV::Scene::Node>&)> pick =
			[&](const std::shared_ptr<MV::Scene::Node>& a_node) -> std::shared_ptr<MV::Scene::Node> {
			if (!a_node->visible()) {
				return nullptr;
			}
			std::vector<std::shared_ptr<MV::Scene::Node>> children(a_node->begin(), a_node->end());
			for (auto childIt = children.rbegin(); childIt != children.rend(); ++childIt) {
				if (auto hit = pick(*childIt)) {
					return hit;
				}
			}
			if (a_node != sceneRoot && a_node->screenBounds(false).contains(a_screenPoint)) {
				return a_node;
			}
			return nullptr;
		};
		if (auto hit = pick(sceneRoot)) {
			select(hit);
		}
	}

	bool App::inScene(const std::shared_ptr<MV::Scene::Node>& a_node) const {
		for (auto node = a_node; node; node = node->parent()) {
			if (node == sceneRoot) {
				return true;
			}
		}
		return false;
	}

	void App::reportError(const std::string& a_message) {
		++errorCount;
		if (a_message != lastErrorMessage) {
			lastErrorMessage = a_message;
			std::cerr << "[workbench] error: " << a_message << std::endl;
		}
	}

	void App::dropPrefab(const std::string& a_fileName, const MV::Point<int>& a_screenPoint) {
		auto worldPoint = managers.renderer.worldFromScreen(a_screenPoint);
		if (!dock->viewportAt(worldPoint)) {
			return;
		}
		try {
			auto prefab = MV::Scene::Node::loadJai(a_fileName, managers.services, false);
			auto dropPosition = sceneRoot->localFromScreen(a_screenPoint);
			auto scene = sceneRoot;
			sceneRoot->add(prefab);
			prefab->position(dropPosition);
			prefab->postLoadStep();
			forEachParallax(prefab, [](const std::shared_ptr<MV::Scene::Parallax>& a_parallax) { a_parallax->authoring(true); });
			history.record("place " + std::filesystem::path(a_fileName).filename().string(),
				[scene, prefab, dropPosition]() {
					scene->add(prefab);
					prefab->position(dropPosition);
				},
				[prefab]() {
					prefab->removeFromParent();
				});
			if (treePanel) {
				treePanel->refresh();
			}
			select(prefab);
			std::cout << "Workbench placed BindSnap: " << a_fileName << std::endl;
		} catch (const std::exception& a_error) {
			reportError("BindSnap drop failed (" + a_fileName + "): " + a_error.what());
		}
	}

	bool App::selectionIsRemovable() const {
		return selectedNode && selectedNode != sceneRoot && selectedNode->parent() && inScene(selectedNode);
	}

	namespace {
		std::string nodeLabel(const std::shared_ptr<MV::Scene::Node>& a_node) {
			return a_node->id().empty() ? std::string("(node)") : a_node->id();
		}
	}

	bool App::deleteSelected() {
		if (!selectionIsRemovable()) {
			return false;
		}
		auto node = selectedNode;
		auto parent = node->parent();
		select(nullptr);
		{
			auto deletion = history.group("delete " + nodeLabel(node));
			clearReferencesInto(node);
			parent->remove(node);
			history.record("remove " + nodeLabel(node),
				[parent, node]() { parent->remove(node, false); },
				[this, parent, node]() {
					parent->add(node, false);
					select(node);
				});
		}
		if (treePanel) {
			treePanel->refresh();
		}
		return true;
	}

	// What the rest of the scene holds of a subtree about to leave it would keep the subtree in the saved file.
	void App::clearReferencesInto(const std::shared_ptr<MV::Scene::Node>& a_leaving) {
		auto leaves = [&a_leaving](const std::shared_ptr<MV::Scene::Node>& a_node) {
			for (auto node = a_node; node; node = node->parent()) {
				if (node == a_leaving) {
					return true;
				}
			}
			return false;
		};
		std::function<void(const std::shared_ptr<MV::Scene::Node>&)> visit = [&](const std::shared_ptr<MV::Scene::Node>& a_node) {
			if (a_node == a_leaving) {
				return;
			}
			if (auto components = a_node->property_value<std::vector<std::shared_ptr<MV::Scene::Component>>>("childComponents")) {
				for (auto&& component : *components) {
					for (auto&& reference : InspectorPanel::nodeReferences(component)) {
						if (leaves(reference.target)) {
							reference.clear();
							history.record("clear reference", reference.clear, reference.restore);
						}
					}
				}
			}
			for (auto&& child : *a_node) {
				visit(child);
			}
		};
		visit(sceneRoot);
	}

	bool App::duplicateSelected() {
		if (!selectionIsRemovable()) {
			return false;
		}
		// a previewed layer is displaced: the copy is taken at rest
		parallaxPreview(false);
		auto original = selectedNode;
		auto parent = original->parent();
		try {
			auto copy = original->clone();
			forEachParallax(copy, [](const std::shared_ptr<MV::Scene::Parallax>& a_parallax) { a_parallax->authoring(true); });
			std::weak_ptr<MV::Scene::Node> weakOriginal = original;
			history.record("duplicate " + nodeLabel(original),
				[this, parent, copy]() {
					parent->add(copy, false);
					select(copy);
				},
				[this, parent, copy, weakOriginal]() {
					parent->remove(copy, false);
					select(weakOriginal.lock());
				});
			if (treePanel) {
				treePanel->refresh();
			}
			select(copy);
			return true;
		} catch (const std::exception& a_error) {
			reportError("duplicate failed (" + nodeLabel(original) + "): " + a_error.what());
			return false;
		}
	}

	bool App::requestUndoSteps(size_t a_steps) {
		const size_t steps = std::clamp<size_t>(a_steps, 1, 10000);
		if (steps < history.length()) {
			const size_t dropped = history.length() - steps;
			std::string message = "The history holds " + std::to_string(history.length()) + " steps. Keeping " + std::to_string(steps) + " drops " + std::to_string(dropped) + " of them, and they cannot be brought back.";
			if (!askToConfirm || !askToConfirm(message)) {
				return false;
			}
		}
		history.limit(steps);
		settingsChanged();
		return true;
	}

	void App::gridSize(const MV::Size<>& a_size) {
		sceneGridSize = MV::size(std::max(0.01f, a_size.width), std::max(0.01f, a_size.height));
		applyGridSize();
		settingsChanged();
	}

	void App::applyGridSize() {
		gizmos->snapSteps.moveX = sceneGridSize.width / 10.0f;
		gizmos->snapSteps.moveY = sceneGridSize.height / 10.0f;
		sceneGrid->spacing(MV::size(gizmos->snapSteps.moveX, gizmos->snapSteps.moveY));
	}

	void App::rotationSnap(float a_degrees) {
		gizmos->snapSteps.rotate = std::clamp(a_degrees, 0.01f, 180.0f);
		settingsChanged();
	}

	void App::settingsChanged() {
		writeSettingsFile();
		if (settingsPanel) {
			settingsPanel->refresh();
		}
	}

	bool App::dockLayout(const std::string& a_description) {
		if (!dock->applyLayout(a_description)) {
			return false;
		}
		writeSettingsFile();
		return true;
	}

	void App::resetDockLayout() {
		dockLayout(defaultDockLayout);
	}

	bool App::saveDockLayout(const std::string& a_fileName) {
		std::ofstream file(a_fileName, std::ios::trunc);
		file << dock->describeLayout() << "\n";
		if (!file) {
			reportError("the layout did not save to " + a_fileName);
			return false;
		}
		return true;
	}

	bool App::loadDockLayout(const std::string& a_fileName) {
		std::ifstream file(a_fileName);
		std::string description;
		if (!std::getline(file, description) || !dockLayout(description)) {
			reportError("no layout could be read from " + a_fileName);
			return false;
		}
		return true;
	}

	void App::showSettings() {
		dock->show(settingsPanel);
	}

	void App::showInspector() {
		dock->show(inspectorPanel);
	}

	// One "name = value" line per setting: read by hand so the file never depends on the scene archives.
	void App::useSettingsFile(const std::string& a_path) {
		settingsPath = a_path;
		std::ifstream file(settingsPath);
		std::string line;
		auto trimmed = [](std::string a_text) {
			a_text.erase(0, a_text.find_first_not_of(" \t\r"));
			a_text.erase(a_text.find_last_not_of(" \t\r") + 1);
			return a_text;
		};
		auto grid = sceneGridSize;
		while (std::getline(file, line)) {
			auto equals = line.find('=');
			if (equals == std::string::npos) {
				continue;
			}
			std::string name = trimmed(line.substr(0, equals));
			if (name == "layout") {
				dock->applyLayout(trimmed(line.substr(equals + 1)));
				continue;
			}
			auto value = numberFrom(trimmed(line.substr(equals + 1)));
			if (!value || *value <= 0.0f) {
				continue;
			}
			if (name == "undoSteps") {
				history.limit(std::clamp<size_t>(static_cast<size_t>(*value), 1, 10000));
			} else if (name == "gridX") {
				grid.width = *value;
			} else if (name == "gridY") {
				grid.height = *value;
			} else if (name == "rotationSnap") {
				gizmos->snapSteps.rotate = std::clamp(*value, 0.01f, 180.0f);
			}
		}
		sceneGridSize = grid;
		applyGridSize();
		if (settingsPanel) {
			settingsPanel->refresh();
		}
	}

	void App::writeSettingsFile() {
		if (settingsPath.empty()) {
			return;
		}
		std::ofstream file(settingsPath, std::ios::trunc);
		file << "undoSteps = " << history.limit() << "\n";
		file << "gridX = " << formatNumberCompact(sceneGridSize.width) << "\n";
		file << "gridY = " << formatNumberCompact(sceneGridSize.height) << "\n";
		file << "rotationSnap = " << formatNumberCompact(rotationSnap()) << "\n";
		file << "layout = " << dock->describeLayout() << "\n";
		if (!file) {
			reportError("the settings did not save to " + settingsPath);
		}
	}

	std::string App::documentName() const {
		return documentPath.empty() ? std::string("Untitled") : std::filesystem::path(documentPath).filename().string();
	}

	bool App::unsavedChangesResolved() {
		if (!history.modified()) {
			return true;
		}
		switch (askAboutUnsavedChanges ? askAboutUnsavedChanges(documentName()) : UnsavedChoice::DISCARD) {
			case UnsavedChoice::SAVE:
				return saveCurrentScene();
			case UnsavedChoice::DISCARD:
				return true;
			default:
				return false;
		}
	}

	void App::requestNewScene() {
		if (unsavedChangesResolved()) {
			newScene();
		}
	}

	void App::requestOpenScene() {
		if (unsavedChangesResolved()) {
			openSceneWithDialog();
		}
	}

	bool App::requestLoadScene(const std::string& a_fileName) {
		return unsavedChangesResolved() && loadScene(a_fileName);
	}

	void App::requestExit() {
		if (unsavedChangesResolved()) {
			done = true;
		}
	}

	void App::buildTitleBar() {
		auto& library = *managers.services.get<MV::TextLibrary>();
		auto bar = uiRoot->make("titleBar");
		auto sizer = bar->attach<MV::Scene::Sprite>();
		sizer->id("titleSizer");
		sizer->bounds(MV::size(1600.0f, theme.titleBarHeight));
		sizer->hide();
		auto titleBg = bar->attach<MV::Scene::Sprite>()->id("titleBg")->bounds(MV::size(1600.0f, theme.titleBarHeight))->color(theme.windowBg());
		titleBg->anchors().anchor(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(1.0f, 1.0f))).parent(sizer.self());
		auto menuHost = bar->make("menus");
		menuHost->position(MV::point(theme.px(6.0f), 0.0f));
		menuBar = std::make_unique<MenuBar>(menuHost, uiRoot->make("menuPopups"), managers.services, theme);
		menuBar->add("File", {
			{ "New Scene", "", [this]() { requestNewScene(); } },
			{ "Open...", "Ctrl+O", [this]() { requestOpenScene(); } },
			{ "Save", "Ctrl+S", [this]() { saveCurrentScene(); }, true },
			{ "Save As...", "Ctrl+Shift+S", [this]() { saveSceneWithDialog(); } },
			{ "Exit", "", [this]() { requestExit(); }, true } });
		auto named = [](const char* a_verb, const std::string& a_label) {
			return a_label.empty() ? std::string(a_verb) : a_verb + (" " + a_label);
		};
		menuBar->add("Edit", {
			{ "Undo", "Ctrl+Z", [this]() { history.undo(); }, false, [this, named]() { return named("Undo", history.undoLabel()); }, [this]() { return history.canUndo(); } },
			{ "Redo", "Ctrl+Y", [this]() { history.redo(); }, false, [this, named]() { return named("Redo", history.redoLabel()); }, [this]() { return history.canRedo(); } },
			{ "Duplicate", "Ctrl+D", [this]() { duplicateSelected(); }, true, {}, [this]() { return selectionIsRemovable(); } },
			{ "Delete", "Del", [this]() { deleteSelected(); }, false, {}, [this]() { return selectionIsRemovable(); } },
			{ "History", "", [this]() { sceneToolbar->historyShown(!sceneToolbar->historyShown()); }, true, [this]() { return std::string(sceneToolbar->historyShown() ? "Hide History" : "Show History"); } } });
		menuBar->add("Settings", {
			{ "Editor Settings", "", [this]() { showSettings(); } } });
		std::vector<MenuItem> windowItems;
		for (auto* panel : menuPanels) {
			windowItems.push_back({ panel->title(), "", [this, panel]() { dock->show(panel); }, false, [this, panel]() {
				return panel->title() + (dock->open(panel) ? "" : " (closed)");
			} });
		}
		windowItems.push_back({ "Save Layout...", "", [this]() {
			auto chosen = saveLayoutFileDialog(MV::playerPreferencesPath(), "workbench.bindlayout");
			if (!chosen.empty()) {
				saveDockLayout(chosen);
			}
		}, true });
		windowItems.push_back({ "Load Layout...", "", [this]() {
			auto chosen = openLayoutFileDialog(MV::playerPreferencesPath());
			if (!chosen.empty()) {
				loadDockLayout(chosen);
			}
		} });
		windowItems.push_back({ "Reset Layout", "", [this]() { resetDockLayout(); } });
		menuBar->add("Window", std::move(windowItems));

		// Scene-animation transport: pause/step/speed of the viewport's drawUpdate clock
		// (particle, spine, and shader-time preview).
		auto transport = bar->make("transport");
		const float transportStart = theme.px(6.0f) + menuBar->width() + theme.px(20.0f);
		transport->position(MV::point(transportStart, theme.pad));
		MV::Size<> transportSize(theme.titleButtonWidth, theme.titleBarHeight - theme.pad * 2.0f);
		auto pauseButton = makeIconButton(transport, managers.services, theme, "pause", transportSize, Icon::Pause, nullptr, "Pause or resume the scene's animation");
		std::weak_ptr<MV::Scene::Button> weakPause = pauseButton;
		pauseButton->onAccept.connect("toggle", [this, weakPause](const std::shared_ptr<MV::Scene::Clickable>&) {
			scenePaused = !scenePaused;
			if (auto locked = weakPause.lock()) {
				setButtonIcon(locked, managers.services, theme, scenePaused ? Icon::Play : Icon::Pause);
			}
		});
		auto stepButton = makeIconButton(transport, managers.services, theme, "step", transportSize, Icon::Step, [this]() {
			if (scenePaused) {
				sceneStepQueued = true;
			}
		}, "Advance one frame while paused");
		stepButton->owner()->position(MV::point(transportSize.width + theme.px(2.0f), 0.0f));
		auto speedButton = makeCycleButton(transport, managers.services, theme, "speed", transportSize,
			{ "1x", "2x", "4x", ".25x", ".5x" }, 0, [this](int a_index) {
				static constexpr float speeds[] = { 1.0f, 2.0f, 4.0f, 0.25f, 0.5f };
				sceneSpeed = speeds[a_index];
			});
		speedButton->owner()->position(MV::point((transportSize.width + theme.px(2.0f)) * 2.0f, 0.0f));
		tooltip(managers.services, speedButton, "Animation speed");
		transportButtons = { pauseButton->owner(), stepButton->owner(), speedButton->owner() };
		titleInteractiveWidth = transportStart + (transportSize.width + theme.px(2.0f)) * 3.0f;

		auto titleHost = bar->make("title");
		titleHost->position(MV::point(titleInteractiveWidth + theme.px(16.0f), 0.0f));
		titleText = makeLabel(titleHost, managers.services, theme, "text", MV::size(theme.px(420.0f), theme.titleBarHeight), "");
		refreshTitle();

		auto buttons = bar->make("buttons");
		MV::Size<> buttonSize(theme.titleButtonWidth, theme.titleBarHeight);
		auto minimizeButton = makeIconButton(buttons, managers.services, theme, "minimize", buttonSize, Icon::Minimize, [this]() {
			managers.renderer.window().minimize();
		}, "Minimize");
		auto maximizeButton = makeIconButton(buttons, managers.services, theme, "maximize", buttonSize, Icon::Maximize, [this]() {
			auto& window = managers.renderer.window();
			if (window.maximized()) {
				window.restore();
			} else {
				window.maximize();
			}
		}, "Maximize or restore");
		auto closeButton = makeIconButton(buttons, managers.services, theme, "close", buttonSize, Icon::Close, [this]() {
			requestExit();
		}, "Close", std::nullopt, MV::Color(0xc42b1c));
		minimizeButton->owner()->position(MV::point(0.0f, 0.0f));
		maximizeButton->owner()->position(MV::point(theme.titleButtonWidth, 0.0f));
		closeButton->owner()->position(MV::point(theme.titleButtonWidth * 2.0f, 0.0f));
	}

	void App::layoutTitleBar(const MV::Size<>& a_worldSize) {
		auto bar = uiRoot->get("titleBar", false);
		if (!bar) {
			return;
		}
		if (auto sizer = bar->component<MV::Scene::Sprite>("titleSizer", false)) {
			sizer->bounds(MV::size(a_worldSize.width, theme.titleBarHeight));
		}
		if (auto buttons = bar->get("buttons", false)) {
			buttons->position(MV::point(a_worldSize.width - theme.titleButtonWidth * 3.0f, 0.0f));
		}
	}

	void App::forEachParallax(const std::shared_ptr<MV::Scene::Node>& a_node, const std::function<void(const std::shared_ptr<MV::Scene::Parallax>&)>& a_visit) {
		if (auto parallax = a_node->component<MV::Scene::Parallax>(true, false)) {
			a_visit(parallax.self());
		}
		for (auto&& child : *a_node) {
			forEachParallax(child, a_visit);
		}
	}

	void App::parallaxPreview(bool a_preview) {
		for (auto&& weakParallax : previewedParallax) {
			if (auto parallax = weakParallax.lock()) {
				parallax->authoringPreview(false);
			}
		}
		previewedParallax.clear();
		if (a_preview && selectedNode) {
			forEachParallax(selectedNode, [this](const std::shared_ptr<MV::Scene::Parallax>& a_parallax) {
				if (a_parallax->enabled()) {
					a_parallax->authoringPreview(true);
					previewedParallax.push_back(a_parallax);
				}
			});
		}
	}

	void App::refreshTitle() {
		shownTitle = documentName() + (history.modified() ? "*" : "") + "  -  Bindstone Workbench";
		if (titleText) {
			titleText->text(shownTitle);
		}
	}

	void App::newScene() {
		history.clear();
		select(nullptr);
		viewCamera->remove(sceneRoot, false);
		sceneRoot = viewCamera->make("root");
		viewCamera->position(MV::Point<>());
		viewCamera->scale(MV::Scale(1.0f, 1.0f, 1.0f));
		documentPath.clear();
		if (treePanel) {
			treePanel->refresh();
		}
		refreshTitle();
	}

	void App::openSceneWithDialog() {
		auto chosen = openSceneFileDialog("Assets/Scenes");
		if (!chosen.empty()) {
			loadScene(chosen);
		}
	}

	bool App::saveSceneWithDialog() {
		auto chosen = saveSceneFileDialog("Assets/Scenes", documentPath.empty() ? std::string("untitled.scene") : std::filesystem::path(documentPath).filename().string());
		return !chosen.empty() && saveScene(chosen);
	}

	bool App::saveCurrentScene() {
		return documentPath.empty() ? saveSceneWithDialog() : saveScene(documentPath);
	}

	void App::select(const std::shared_ptr<MV::Scene::Node>& a_node) {
		if (a_node != selectedNode) {
			parallaxPreview(false);
		}
		selectedNode = a_node;
		gizmos->select(a_node);
		if (treePanel) {
			treePanel->showSelection(a_node);
		}
		if (inspectorPanel) {
			inspectorPanel->inspect(a_node);
		}
	}

	// A relative name is an asset: the file beside the working directory when there is one, else under Assets/.
	// The leading "./" keeps MV's file layer from redirecting a write to the player-preferences folder.
	std::string App::assetFile(const std::string& a_fileName) {
		if (std::filesystem::path(a_fileName).is_absolute() || a_fileName.starts_with("./") || a_fileName.starts_with(".\\")) {
			return a_fileName;
		}
		std::error_code ignored;
		return std::filesystem::exists(a_fileName, ignored) ? "./" + a_fileName : "./Assets/" + a_fileName;
	}

	bool App::loadScene(const std::string& a_fileName) {
		try {
			auto assetPath = assetFile(a_fileName);
			std::error_code ignored;
			auto newRoot = MV::Scene::Node::load(std::filesystem::exists(assetPath, ignored) ? assetPath : a_fileName, managers.services, false);
			history.clear();
			select(nullptr);
			viewCamera->remove(sceneRoot, false);
			sceneRoot = newRoot;
			viewCamera->add(sceneRoot);
			sceneRoot->postLoadStep();
			forEachParallax(sceneRoot, [](const std::shared_ptr<MV::Scene::Parallax>& a_parallax) { a_parallax->authoring(true); });
			if (treePanel) {
				treePanel->refresh();
			}
			frameSceneInViewport();
			documentPath = a_fileName;
			refreshTitle();
			std::cout << "Workbench loaded: " << a_fileName << std::endl;
			return true;
		} catch (const std::exception& a_error) {
			reportError("load failed (" + a_fileName + "): " + a_error.what());
			return false;
		}
	}

	bool App::saveScene(const std::string& a_fileName) {
		// a previewed layer is displaced: it goes back to rest for the write
		auto previewLayers = [this](bool a_preview) {
			for (auto&& weakParallax : previewedParallax) {
				if (auto parallax = weakParallax.lock()) {
					parallax->authoringPreview(a_preview);
				}
			}
		};
		bool result = true;
		previewLayers(false);
		try {
			auto assetPath = assetFile(a_fileName);
			sceneRoot->saveJai(assetPath, managers.services, false);
			documentPath = a_fileName;
			history.markSaved();
			std::cout << "Workbench saved: " << assetPath << std::endl;
		} catch (const std::exception& a_error) {
			reportError("save failed (" + a_fileName + "): " + a_error.what());
			result = false;
		}
		previewLayers(true);
		return result;
	}

	// Zooms so the node's bounds fill a_fill of the view and centres them; a node without bounds is centred at the zoom in use.
	void App::frameNode(const DockRect& a_view, const std::shared_ptr<MV::Scene::Node>& a_node, float a_fill, float a_maximumZoom) {
		float zoom = viewCamera->scale().x;
		viewCamera->position(MV::Point<>());
		viewCamera->scale(MV::Scale(1.0f, 1.0f, 1.0f));
		auto local = a_node->bounds();
		MV::BoxAABB<> bounds(a_node->worldFromLocal(local.topLeftPoint()));
		for (auto&& corner : { local.topRightPoint(), local.bottomRightPoint(), local.bottomLeftPoint() }) {
			bounds.expandWith(a_node->worldFromLocal(corner));
		}
		auto size = bounds.size();
		auto center = a_node->worldPosition();
		if (size.width >= 1.0f || size.height >= 1.0f) {
			zoom = std::clamp(std::min(a_view.size.width / std::max(size.width, 1.0f), a_view.size.height / std::max(size.height, 1.0f)) * a_fill, minimumZoom, a_maximumZoom);
			center = bounds.centerPoint();
		}
		viewCamera->scale(MV::Scale(zoom, zoom, 1.0f));
		viewCamera->position(MV::point(
			a_view.position.x + a_view.size.width * 0.5f - center.x * zoom,
			a_view.position.y + a_view.size.height * 0.5f - center.y * zoom));
	}

	MV::Point<int> App::transportScreenPoint(int a_index) const {
		auto button = transportButtons[static_cast<size_t>(a_index)].lock();
		return button ? button->screenBounds().centerPoint() : MV::Point<int>();
	}

	bool App::update(double a_dt) {
		lastDelta = a_dt;
		tooltips->update(a_dt);
		lastSceneDelta = scenePaused ? 0.0 : a_dt * sceneSpeed;
		if (scenePaused && sceneStepQueued) {
			lastSceneDelta = 1.0 / 60.0;
			sceneStepQueued = false;
		}
		if (!focusRouter.hasActive()) {
			updateCameraKeys(a_dt);
		}
		auto worldSize = managers.renderer.world().size();
		if (worldSize.width != lastWorldSize.width || worldSize.height != lastWorldSize.height) {
			lastWorldSize = worldSize;
			dock->layout(DockRect{ MV::point(0.0f, theme.titleBarHeight), MV::Size<>(worldSize.width, worldSize.height - theme.titleBarHeight) });
			layoutTitleBar(worldSize);
		}
		dock->update(a_dt);
		syncViewportClip();
		if ((sceneFramePending || selectionFramePending) && !frameAwaitsDraw) {
			auto view = dock->viewportRect();
			if (view.size.width > 0.0f && view.size.height > 0.0f) {
				if (selectionFramePending && selectedNode) {
					frameNode(view, selectedNode, 0.7f, maximumZoom);
				} else {
					frameNode(view, sceneRoot, 0.9f, 1.0f);
				}
				sceneFramePending = false;
				selectionFramePending = false;
			}
		}
		gizmos->update();

		auto hint = dock->cursorHintAt(managers.renderer.worldFromScreen(mouse.position()));
		if (hint != activeCursor) {
			activeCursor = hint;
			switch (hint) {
			case DockSpace::CursorHint::ResizeHorizontal:
				SDL_SetCursor(cursorResizeH);
				break;
			case DockSpace::CursorHint::ResizeVertical:
				SDL_SetCursor(cursorResizeV);
				break;
			default:
				SDL_SetCursor(cursorArrow);
				break;
			}
		}

		managers.pool.run_posted();
		return !done;
	}

	void App::syncViewportClip() {
		auto view = dock->viewportRect();
		if (view.size.width <= 0.0f || view.size.height <= 0.0f) {
			return;
		}
		sceneToolbar->position(MV::point(view.position.x + theme.px(8.0f), view.position.y + theme.px(8.0f)));
		sceneToolbar->viewWidth(view.size.width - theme.px(16.0f));
		sceneGrid->update(viewCamera->position(), viewCamera->scale().x, MV::BoxAABB<>(view.position, view.size));
		if (view.position.x != lastViewportRect.position.x || view.position.y != lastViewportRect.position.y ||
			view.size.width != lastViewportRect.size.width || view.size.height != lastViewportRect.size.height) {
			lastViewportRect = view;
			viewportStencil->bounds(MV::BoxAABB<>(view.position,
				MV::point(view.position.x + view.size.width, view.position.y + view.size.height)));
		}
	}

	void App::updateCameraKeys(double a_dt) {
		const Uint8* keys = SDL_GetKeyboardState(nullptr);
		float speed = static_cast<float>(600.0 * a_dt);
		MV::Point<> delta;
		if (keys[SDL_SCANCODE_LEFT]) { delta.x += speed; }
		if (keys[SDL_SCANCODE_RIGHT]) { delta.x -= speed; }
		if (keys[SDL_SCANCODE_UP]) { delta.y += speed; }
		if (keys[SDL_SCANCODE_DOWN]) { delta.y -= speed; }
		if (delta.x != 0.0f || delta.y != 0.0f) {
			viewCamera->translate(delta);
		}
	}

	bool App::handleShortcutKey(SDL_Keycode a_key) {
		switch (a_key) {
			case SDLK_q:
				gizmoTool(Tool::SELECT);
			break;
			case SDLK_w:
				gizmoTool(Tool::MOVE);
			break;
			case SDLK_e:
				gizmoTool(Tool::ROTATE);
			break;
			case SDLK_r:
				gizmoTool(Tool::SCALE);
			break;
			case SDLK_f:
				frameSelectionInViewport();
			break;
			case SDLK_DELETE:
				deleteSelected();
			break;
			default:
				return false;
		}
		return true;
	}

	void App::handleInput() {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (managers.renderer.handleEvent(event)) {
				continue;
			}
			if (event.type == SDL_QUIT) {
				requestExit();
				continue;
			}
			menuBar->handleInput(event);
			if (focusRouter.handleEvent(event)) {
				continue;
			}
			if (event.type == SDL_KEYDOWN && (SDL_GetModState() & KMOD_CTRL)) {
				if (event.key.keysym.sym == SDLK_s) {
					if (SDL_GetModState() & KMOD_SHIFT) {
						saveSceneWithDialog();
					} else {
						saveCurrentScene();
					}
					continue;
				}
				if (event.key.keysym.sym == SDLK_o) {
					requestOpenScene();
					continue;
				}
				if (event.key.keysym.sym == SDLK_d) {
					duplicateSelected();
					continue;
				}
				if (event.key.keysym.sym == SDLK_z) {
					if (SDL_GetModState() & KMOD_SHIFT) {
						history.redo();
					} else {
						history.undo();
					}
					continue;
				}
				if (event.key.keysym.sym == SDLK_y) {
					history.redo();
					continue;
				}
			}
			if (event.type == SDL_KEYDOWN && !event.key.repeat && !(SDL_GetModState() & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) && handleShortcutKey(event.key.keysym.sym)) {
				continue;
			}
			if (event.type == SDL_MOUSEWHEEL) {
				auto worldPoint = managers.renderer.worldFromScreen(mouse.position());
				if (dock->viewportAt(worldPoint)) {
					float zoom = 1.0f + 0.1f * static_cast<float>(event.wheel.y);
					auto current = viewCamera->scale();
					float newScale = std::clamp(current.x * zoom, minimumZoom, maximumZoom);
					auto position = viewCamera->position();
					// Keep the point under the cursor stationary while zooming.
					MV::Point<> local((worldPoint.x - position.x) / current.x, (worldPoint.y - position.y) / current.y);
					viewCamera->position(MV::point(worldPoint.x - local.x * newScale, worldPoint.y - local.y * newScale, position.z));
					viewCamera->scale(MV::Scale(newScale, newScale, current.z));
				} else {
					dock->wheelScroll(worldPoint, static_cast<float>(event.wheel.y));
				}
				continue;
			}
			dock->handleInput(event);
			mouse.updateTouch(event, managers.renderer.window().drawableSize());
		}
		mouse.update();
	}

	void App::render() {
		managers.renderer.clearScreen();
		if (lastSceneDelta > 0.0) {
			visor->drawUpdate(lastSceneDelta);
		} else {
			visor->draw();
		}
		frameAwaitsDraw = false;
		uiRoot->drawUpdate(lastDelta);
		managers.renderer.updateScreen();
	}

	template <typename Step>
	void App::guarded(const char* a_stage, Step&& a_step) {
		try {
			a_step();
		} catch (const std::exception& a_error) {
			reportError(std::string(a_stage) + ": " + a_error.what());
		}
	}

	int App::run() {
		managers.timer.start();
		managers.timer.delta("workbenchTick");
		while (!done) {
			guarded("update", [this]() { update(managers.timer.delta("workbenchTick")); });
			guarded("input", [this]() { handleInput(); });
			guarded("render", [this]() { render(); });
			std::this_thread::yield();
		}
		return 0;
	}

	int App::runFrames(int a_frameCount, const std::string& a_screenshotPath) {
		managers.timer.start();
		managers.timer.delta("workbenchTick");
		for (int frame = 0; frame < a_frameCount && !done; ++frame) {
			guarded("update", [this]() { update(managers.timer.delta("workbenchTick")); });
			guarded("input", [this]() { handleInput(); });
			guarded("render", [&]() {
				if (!a_screenshotPath.empty() && frame == a_frameCount - 1) {
					managers.renderer.clearScreen();
					visor->drawUpdate(lastDelta);
					uiRoot->drawUpdate(lastDelta);
					captureScreenshot(a_screenshotPath);
					managers.renderer.updateScreen();
				} else {
					render();
				}
			});
		}
		std::cout << "Workbench smoke completed (" << a_frameCount << " frames, " << errorCount << " errors)" << std::endl;
		return errorCount == 0 ? 0 : 1;
	}

	void App::captureScreenshot(const std::string& a_path) {
		auto size = managers.renderer.window().drawableSize();
		SDL_Surface* surface = SDL_CreateRGBSurface(0, size.width, size.height, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
		std::vector<uint8_t> pixels(static_cast<size_t>(size.width) * size.height * 4);
		glReadPixels(0, 0, size.width, size.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		auto* target = static_cast<uint8_t*>(surface->pixels);
		for (int y = 0; y < size.height; ++y) {
			std::memcpy(target + static_cast<size_t>(y) * surface->pitch,
				pixels.data() + static_cast<size_t>(size.height - 1 - y) * size.width * 4,
				static_cast<size_t>(size.width) * 4);
		}
		SDL_SaveBMP(surface, a_path.c_str());
		SDL_FreeSurface(surface);
		std::cout << "Workbench screenshot: " << a_path << std::endl;
	}

}
