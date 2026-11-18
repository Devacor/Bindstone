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

		managers.services.connect(&bindSnapTracking);
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
		// after the pan's own receivers, so a right drag across the scene has ended as a pan by the time this looks
		panReceivers.push_back(mouse.onRightMouseDown.connect([this](MV::TapDevice& a_mouse) { contextPress = a_mouse.position(); }));
		panReceivers.push_back(mouse.onRightMouseUp.connect([this](MV::TapDevice& a_mouse) {
			auto released = a_mouse.position();
			if (std::abs(released.x - contextPress.x) + std::abs(released.y - contextPress.y) <= 4) {
				openContextMenu(released);
			}
		}));
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

		controllerReceiver = controller.onButtonDown.connect([this](MV::ControllerDevice&, MV::ControllerButton a_button) {
			if (a_button != MV::ControllerButton::EAST) {
				return;
			}
			if (menuBar && menuBar->isOpen()) {
				menuBar->close();
			} else if (focusRouter.hasActive()) {
				focusRouter.cancel();
			} else {
				mouse.focusClear();
			}
		});

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

		messageLog = std::make_shared<MessageLog>();
		// the engine logs from its loader threads too: its lines are posted and join the log on the main thread
		for (auto severity : { MV::Severity::Info, MV::Severity::Warning, MV::Severity::Error }) {
			MV::Logger::instance()->listen(severity, [weakLog = std::weak_ptr<MessageLog>(messageLog)](MV::Severity a_severity, const MV::LogData&, const std::string& a_message) {
				if (auto log = weakLog.lock()) {
					log->post(a_severity == MV::Severity::Error ? MessageLevel::FAILURE : a_severity == MV::Severity::Warning ? MessageLevel::WARNING : MessageLevel::INFO, a_message);
				}
			});
		}
		messageLog->onChange = [this]() {
			if (consolePanel) {
				consolePanel->logChanged();
			}
			statusStale = true;
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
		inspectorPanel->onRemoveComponent = [this](const std::shared_ptr<MV::Scene::Component>& a_component) { removeComponent(a_component); };
		inspectorPanel->onAddComponent = [this](const MV::Point<int>& a_screenPoint) { menuBar->openContext(a_screenPoint, addComponentItems()); };
		treePanel->onNodeDroppedOutside = [this](const std::shared_ptr<MV::Scene::Node>& a_node, const MV::Point<int>& a_screenPoint) {
			if (inspectorPanel->dropNode(a_node, a_screenPoint)) {
				return;
			}
			if (projectPanel && dock->panelShowingAt(managers.renderer.worldFromScreen(a_screenPoint)) == projectPanel) {
				nodeDroppedOnProject(a_node, a_screenPoint);
			}
		};
		treePanel->canReparent = [this](const std::shared_ptr<MV::Scene::Node>& a_node, const std::shared_ptr<MV::Scene::Node>& a_newParent) {
			return !contentLocked(a_node, "move") && !contentLocked(a_newParent, "move a node under");
		};
		treePanel->onNodeDragged = [this](const std::shared_ptr<MV::Scene::Node>& a_node, const MV::Point<int>& a_screenPoint, bool a_outside) {
			std::string folder;
			std::string note;
			if (a_outside && projectPanel && dock->panelShowingAt(managers.renderer.worldFromScreen(a_screenPoint)) == projectPanel) {
				folder = projectPanel->folderAt(a_screenPoint);
				auto file = bindSnapFileFor(a_node, folder);
				switch (bindSnapDropKind(a_node, file)) {
					case BindSnapDrop::SAVE:
						note = "Save as " + file.generic_string();
					break;
					case BindSnapDrop::APPLY:
						note = "Apply to " + file.generic_string();
					break;
					default:
						note = "Replace or New Asset: " + file.generic_string();
				}
			}
			if (projectPanel) {
				projectPanel->markDropFolder(folder);
			}
			tooltips->dragNote(note);
		};
		treePanel->onNodeDragEnded = [this]() {
			if (projectPanel) {
				projectPanel->markDropFolder("");
			}
			tooltips->dragNote("");
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
			} },
			{ "Controller cursor", [this]() { return std::string(controllerCursor() ? "on" : "off"); }, [this](const std::string& a_text) {
				if (a_text == "on" || a_text == "off") {
					controllerCursor(a_text == "on");
				}
			} }
		};
		settingsPanel = static_cast<SettingsPanel*>(dock->registerPanel(std::make_shared<SettingsPanel>(std::move(settingRows))));
		consolePanel = static_cast<ConsolePanel*>(dock->registerPanel(std::make_shared<ConsolePanel>(messageLog)));
		statusBar = uiRoot->make("statusBar");
		Panel* viewportPanel = dock->registerPanel(std::move(viewport));
		Panel* filePanel = dock->registerPanel(std::move(file));
		projectPanel = static_cast<ProjectPanel*>(dock->registerPanel(std::move(project)));
		projectPanel->onRename = [this](const std::filesystem::path& a_entry, const std::string& a_newName) { renameAsset(a_entry, a_newName); };
		discardFile = [](const std::filesystem::path& a_entry) { return moveToRecycleBin(a_entry.string()); };

		menuPanels = { treePanel, filePanel, viewportPanel, projectPanel, inspectorPanel, settingsPanel, consolePanel };

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
					DockSpace::leaf({ projectPanel, consolePanel })),
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
		managers.services.disconnect<MV::Scene::BindSnapTracking>();
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
		messageLog->add(MessageLevel::FAILURE, a_message);
		if (a_message != lastErrorMessage) {
			lastErrorMessage = a_message;
			std::cerr << "[workbench] error: " << a_message << std::endl;
		}
	}

	namespace {
		std::string unusedChildId(const std::shared_ptr<MV::Scene::Node>& a_parent, const std::string& a_base);
		std::string nodeLabel(const std::shared_ptr<MV::Scene::Node>& a_node);

		// A BindSnap's file as a linked node names it: what Node::make takes, with no "./" or "Assets/" before it.
		std::string bindSnapSourceName(const std::filesystem::path& a_file) {
			return MV::Scene::BindSnapInstance::sourceName(a_file.generic_string());
		}

		// Whether a node, or one under it, is linked to a file, its content built or not.
		bool namesFile(const std::shared_ptr<MV::Scene::Node>& a_node, const std::string& a_sourceName, bool a_itself = true) {
			if (a_itself) {
				auto link = a_node->component<MV::Scene::BindSnapInstance>(false, false);
				if (link && bindSnapSourceName(link->source()) == a_sourceName) {
					return true;
				}
			}
			for (auto&& child : *a_node) {
				if (namesFile(child, a_sourceName)) {
					return true;
				}
			}
			return false;
		}

		// The link on a node whose file's content is on it.
		std::shared_ptr<MV::Scene::BindSnapInstance> linkOn(const std::shared_ptr<MV::Scene::Node>& a_node) {
			if (!a_node) {
				return nullptr;
			}
			auto found = a_node->component<MV::Scene::BindSnapInstance>(false, false);
			return found && found->built() ? found.self() : nullptr;
		}

		// Every link on a node and below it, the deepest first.
		void collectLinks(const std::shared_ptr<MV::Scene::Node>& a_node, std::vector<std::shared_ptr<MV::Scene::BindSnapInstance>>& a_links) {
			for (auto&& child : *a_node) {
				collectLinks(child, a_links);
			}
			if (auto link = linkOn(a_node)) {
				a_links.push_back(link);
			}
		}

		void captureLinks(const std::shared_ptr<MV::Scene::Node>& a_node) {
			std::vector<std::shared_ptr<MV::Scene::BindSnapInstance>> links;
			collectLinks(a_node, links);
			for (auto&& link : links) {
				link->captureOverrides();
			}
		}
	}

	void App::dropPrefab(const std::string& a_fileName, const MV::Point<int>& a_screenPoint) {
		auto worldPoint = managers.renderer.worldFromScreen(a_screenPoint);
		std::shared_ptr<MV::Scene::Node> parent;
		bool atPoint = false;
		if (treePanel && dock->panelShowingAt(worldPoint) == treePanel) {
			parent = treePanel->nodeAt(a_screenPoint);
			if (!parent) {
				parent = sceneRoot;
			}
		} else if (dock->viewportAt(worldPoint)) {
			parent = sceneRoot;
			atPoint = true;
		}
		if (!parent || (linkOn(parent) == nullptr && contentLocked(parent, "place a BindSnap under"))) {
			return;
		}
		const auto sourceName = bindSnapSourceName(a_fileName);
		const auto documentName = documentPath.empty() ? std::string() : bindSnapSourceName(documentPath);
		if (sourceName == documentName) {
			messageLog->add(MessageLevel::WARNING, "Not placed: " + a_fileName + " is the file being edited, and would hold itself");
			return;
		}
		try {
			auto placed = parent->make(unusedChildId(parent, "placing"));
			auto link = placed->attach<MV::Scene::BindSnapInstance>(managers.services)->placeSource(sourceName);
			if (!link->built()) {
				parent->remove(placed);
				throw std::runtime_error("the file could not be read");
			}
			if (!documentName.empty() && namesFile(placed, documentName, false)) {
				parent->remove(placed);
				messageLog->add(MessageLevel::WARNING, "Not placed: " + a_fileName + " holds the file being edited, which would then hold itself");
				return;
			}
			placed->id(unusedChildId(parent, link->sourceRootId().empty() ? std::string("node") : link->sourceRootId()));
			if (atPoint) {
				placed->position(parent->localFromScreen(a_screenPoint));
			}
			forEachParallax(placed, [](const std::shared_ptr<MV::Scene::Parallax>& a_parallax) { a_parallax->authoring(true); });
			link->markUnchanged();
			history.record("place " + std::filesystem::path(a_fileName).filename().string(),
				[parent, placed]() {
					parent->add(placed, false);
				},
				[placed]() {
					placed->removeFromParent();
				});
			noteBindSnapWritten(sourceName);
			if (treePanel) {
				treePanel->refresh();
			}
			select(placed);
			std::cout << "Workbench placed BindSnap: " << a_fileName << std::endl;
			messageLog->add(MessageLevel::INFO, "Placed " + a_fileName);
		} catch (const std::exception& a_error) {
			reportError("BindSnap drop failed (" + a_fileName + "): " + a_error.what());
		}
	}

	bool App::contentLocked(const std::shared_ptr<MV::Scene::Node>& a_node, const std::string& a_verb) {
		auto holder = MV::Scene::BindSnapInstance::holding(a_node);
		if (!holder) {
			return false;
		}
		messageLog->add(MessageLevel::WARNING, "Cannot " + a_verb + " " + nodeLabel(a_node) + ": it is content of " + holder->source() + ". Unpack " + nodeLabel(holder->owner()) + " first, or change the file.");
		return true;
	}

	App::BindSnapDrop App::bindSnapDropKind(const std::shared_ptr<MV::Scene::Node>& a_node, const std::filesystem::path& a_file) const {
		auto link = linkOn(a_node);
		if (link && bindSnapSourceName(link->source()) == bindSnapSourceName(a_file)) {
			return BindSnapDrop::APPLY;
		}
		std::error_code ignored;
		return std::filesystem::exists(a_file, ignored) ? BindSnapDrop::ASK : BindSnapDrop::SAVE;
	}

	void App::nodeDroppedOnProject(const std::shared_ptr<MV::Scene::Node>& a_node, const MV::Point<int>& a_screenPoint) {
		const auto folder = projectPanel->folderAt(a_screenPoint);
		const auto file = bindSnapFileFor(a_node, folder);
		if (bindSnapDropKind(a_node, file) != BindSnapDrop::ASK) {
			saveAsBindSnap(a_node, folder);
			return;
		}
		std::weak_ptr<MV::Scene::Node> weakNode = a_node;
		auto choose = [this, weakNode, folder](BindSnapClash a_onClash) {
			if (auto node = weakNode.lock()) {
				saveAsBindSnap(node, folder, a_onClash);
			}
		};
		// the new file's name is opened for typing where it appears
		auto chooseNew = [this, weakNode, folder]() {
			if (auto node = weakNode.lock()) {
				auto written = saveAsBindSnap(node, folder, BindSnapClash::NEW_ASSET);
				if (!written.empty() && projectPanel) {
					projectPanel->beginRename(std::filesystem::path(written));
				}
			}
		};
		menuBar->openContext(a_screenPoint, {
			{ "Replace " + file.filename().string(), "", [choose]() { choose(BindSnapClash::REPLACE); } },
			{ "New Asset", "", chooseNew } });
	}

	namespace {
		// Every place a_part is in a_text swapped for a_with; how many there were.
		size_t replaceEach(std::string& a_text, const std::string& a_part, const std::string& a_with) {
			size_t count = 0;
			for (auto at = a_text.find(a_part); at != std::string::npos; at = a_text.find(a_part, at + a_with.size())) {
				a_text.replace(at, a_part.size(), a_with);
				++count;
			}
			return count;
		}

		std::string unusedBeside(const std::filesystem::path& a_entry) {
			std::error_code ignored;
			const auto folder = a_entry.parent_path();
			const auto stem = a_entry.stem().string();
			const auto extension = a_entry.extension().string();
			for (int number = 2; ; ++number) {
				auto name = stem + "_" + std::to_string(number) + extension;
				if (!std::filesystem::exists(folder / name, ignored)) {
					return name;
				}
			}
		}
	}

	bool App::renameAsset(const std::filesystem::path& a_entry, const std::string& a_newName) {
		std::error_code failed;
		const auto target = a_entry.parent_path() / a_newName;
		const bool folder = std::filesystem::is_directory(a_entry, failed);
		if (a_newName.empty() || a_newName.find_first_of("<>:\"/\\|?*") != std::string::npos) {
			messageLog->add(MessageLevel::WARNING, "Not renamed: " + a_newName + " is not a name a file can have");
			return false;
		}
		if (std::filesystem::exists(target, failed)) {
			messageLog->add(MessageLevel::WARNING, "Not renamed: " + target.generic_string() + " is already there");
			return false;
		}
		std::filesystem::rename(a_entry, target, failed);
		if (failed) {
			reportError("rename of " + a_entry.generic_string() + " failed: " + failed.message());
			return false;
		}
		const auto oldName = bindSnapSourceName(a_entry);
		const auto newName = bindSnapSourceName(target);
		// what the open scene's linked nodes name, and the scene's own file
		std::vector<std::shared_ptr<MV::Scene::BindSnapInstance>> links;
		collectLinks(sceneRoot, links);
		for (auto&& link : links) {
			auto source = bindSnapSourceName(link->source());
			if (source == oldName) {
				link->retarget(newName);
			} else if (folder && source.starts_with(oldName + "/")) {
				link->retarget(newName + source.substr(oldName.size()));
			}
		}
		for (auto&& [source, written] : std::map<std::string, std::filesystem::file_time_type>(bindSnapWriteTimes)) {
			if (source == oldName || (folder && source.starts_with(oldName + "/"))) {
				bindSnapWriteTimes[newName + source.substr(oldName.size())] = written;
				bindSnapWriteTimes.erase(source);
			}
		}
		if (bindSnapSourceName(documentPath) == oldName) {
			documentPath = target.generic_string();
			refreshTitle();
		}
		// the paths written in files: as an instance names them and with Assets/ before them, whole for a file and
		// as the start of a path for a folder
		std::vector<std::pair<std::string, std::string>> swaps;
		const std::string ending = folder ? "/" : "\"";
		swaps.emplace_back("\"" + oldName + ending, "\"" + newName + ending);
		if (!a_entry.is_absolute()) {
			swaps.emplace_back("\"Assets/" + oldName + ending, "\"Assets/" + newName + ending);
		}
		std::vector<std::filesystem::path> roots { "Assets" };
		if (a_entry.is_absolute()) {
			roots.push_back(a_entry.parent_path());
		}
		size_t references = 0;
		size_t files = 0;
		std::vector<std::string> scripts;
		for (auto&& scanRoot : roots) {
			for (auto walk = std::filesystem::recursive_directory_iterator(scanRoot, std::filesystem::directory_options::skip_permission_denied, failed); !failed && walk != std::filesystem::recursive_directory_iterator(); walk.increment(failed)) {
				if (!walk->is_regular_file(failed)) {
					continue;
				}
				const auto extension = walk->path().extension().string();
				const bool rewritten = extension == ".scene" || extension == ".bindsnap" || extension == ".json";
				if (!rewritten && extension != ".script" && extension != ".jai") {
					continue;
				}
				const auto fileName = walk->path().generic_string();
				auto text = MV::fileContents(fileName);
				size_t found = 0;
				for (auto&& [from, to] : swaps) {
					found += rewritten ? replaceEach(text, from, to) : (text.find(from) != std::string::npos ? 1 : 0);
				}
				if (found == 0) {
					continue;
				}
				if (!rewritten) {
					scripts.push_back(fileName);
					continue;
				}
				std::ofstream output(walk->path(), std::ios::binary | std::ios::trunc);
				output << text;
				references += found;
				++files;
			}
		}
		messageLog->add(MessageLevel::INFO, "Renamed " + a_entry.generic_string() + " to " + a_newName + (references ? "; " + std::to_string(references) + " references in " + std::to_string(files) + " files follow it" : std::string()));
		for (auto&& script : scripts) {
			messageLog->add(MessageLevel::WARNING, script + " still names " + oldName + ": a script's paths are not rewritten");
		}
		if (projectPanel) {
			projectPanel->refresh();
		}
		linksChanged();
		return true;
	}

	std::filesystem::path App::duplicateAsset(const std::filesystem::path& a_entry) {
		std::error_code failed;
		const auto copy = a_entry.parent_path() / unusedBeside(a_entry);
		std::filesystem::copy(a_entry, copy, std::filesystem::copy_options::recursive, failed);
		if (failed) {
			reportError("duplicate of " + a_entry.generic_string() + " failed: " + failed.message());
			return {};
		}
		messageLog->add(MessageLevel::INFO, "Duplicated " + a_entry.generic_string() + " as " + copy.filename().string());
		if (projectPanel) {
			projectPanel->refresh();
		}
		return copy;
	}

	bool App::deleteAsset(const std::filesystem::path& a_entry) {
		const auto name = bindSnapSourceName(a_entry);
		std::vector<std::shared_ptr<MV::Scene::BindSnapInstance>> links;
		collectLinks(sceneRoot, links);
		const auto linked = std::count_if(links.begin(), links.end(), [&](const std::shared_ptr<MV::Scene::BindSnapInstance>& a_link) {
			auto source = bindSnapSourceName(a_link->source());
			return source == name || source.starts_with(name + "/");
		});
		std::string message = "Delete " + a_entry.generic_string() + "? It goes to the recycle bin.";
		if (linked > 0) {
			message += " " + std::to_string(linked) + " linked node" + (linked == 1 ? "" : "s") + " in the open scene will have no file to read.";
		}
		if (!askToConfirm || !askToConfirm(message)) {
			return false;
		}
		if (!discardFile || !discardFile(a_entry)) {
			reportError("delete of " + a_entry.generic_string() + " failed");
			return false;
		}
		messageLog->add(MessageLevel::INFO, "Deleted " + a_entry.generic_string());
		if (projectPanel) {
			projectPanel->refresh();
		}
		return true;
	}

	std::filesystem::path App::createFolder(const std::filesystem::path& a_in) {
		std::error_code failed;
		auto folder = a_in / "New Folder";
		if (std::filesystem::exists(folder, failed)) {
			folder = a_in / unusedBeside(folder);
		}
		if (!std::filesystem::create_directory(folder, failed) || failed) {
			reportError("a folder could not be made in " + a_in.generic_string());
			return {};
		}
		if (projectPanel) {
			projectPanel->refresh();
			projectPanel->beginRename(folder);
		}
		return folder;
	}

	std::vector<MenuItem> App::projectContextItems(const MV::Point<int>& a_screenPoint) {
		std::vector<MenuItem> result;
		if (auto entry = projectPanel->entryAt(a_screenPoint)) {
			auto target = *entry;
			result.push_back({ "Rename", "", [this, target]() { projectPanel->beginRename(target); } });
			result.push_back({ "Duplicate", "", [this, target]() { duplicateAsset(target); } });
			result.push_back({ "Delete", "", [this, target]() { deleteAsset(target); } });
		}
		auto shown = projectPanel->shownFolder();
		result.push_back({ "New Folder", "", [this, shown]() { createFolder(shown); }, !result.empty() });
		return result;
	}

	void App::noteBindSnapWritten(const std::string& a_sourceName) {
		std::error_code failed;
		auto written = std::filesystem::last_write_time(assetFile(a_sourceName), failed);
		if (!failed) {
			bindSnapWriteTimes[a_sourceName] = written;
		}
	}

	void App::linksChanged() {
		if (selectedNode && !inScene(selectedNode)) {
			select(nullptr);
		}
		if (treePanel) {
			treePanel->refresh();
		}
		if (inspectorPanel) {
			inspectorPanel->inspect(selectedNode);
		}
	}

	void App::refreshLinksTo(const std::string& a_sourceName, const std::shared_ptr<MV::Scene::Node>& a_except) {
		std::vector<std::shared_ptr<MV::Scene::BindSnapInstance>> links;
		collectLinks(sceneRoot, links);
		for (auto&& link : links) {
			// one refreshed earlier in the list may have rebuilt what this one was in
			if (link->ownerIsAlive() && link->owner() != a_except && inScene(link->owner()) && bindSnapSourceName(link->source()) == a_sourceName) {
				link->refresh();
				auto node = link->owner();
				forEachParallax(node, [](const std::shared_ptr<MV::Scene::Parallax>& a_parallax) { a_parallax->authoring(true); });
				link->markUnchanged();
			}
		}
	}

	void App::checkBindSnapFiles() {
		std::vector<std::shared_ptr<MV::Scene::BindSnapInstance>> links;
		collectLinks(sceneRoot, links);
		std::vector<std::string> changed;
		for (auto&& link : links) {
			const auto sourceName = bindSnapSourceName(link->source());
			std::error_code failed;
			auto written = std::filesystem::last_write_time(assetFile(sourceName), failed);
			if (failed) {
				continue;
			}
			auto seen = bindSnapWriteTimes.find(sourceName);
			if (seen == bindSnapWriteTimes.end()) {
				bindSnapWriteTimes[sourceName] = written;
			} else if (seen->second != written) {
				seen->second = written;
				changed.push_back(sourceName);
			}
		}
		for (auto&& sourceName : changed) {
			refreshLinksTo(sourceName, nullptr);
			messageLog->add(MessageLevel::INFO, "Reloaded " + sourceName + ": it changed on disk");
		}
		if (!changed.empty()) {
			linksChanged();
		}
	}

	bool App::unpackBindSnap(const std::shared_ptr<MV::Scene::Node>& a_node) {
		auto link = linkOn(a_node);
		if (!link) {
			return false;
		}
		link->captureOverrides();
		const auto source = link->source();
		const auto overrides = link->overrides();
		link->unpack();
		auto* services = &managers.services;
		history.record("unpack " + nodeLabel(a_node),
			[a_node]() {
				if (auto linked = linkOn(a_node)) {
					linked->unpack();
				}
			},
			[this, services, a_node, source, overrides]() {
				// the content is the node's own here: linked again, it is rebuilt from the file with what it had changed
				a_node->attach<MV::Scene::BindSnapInstance>(*services)->sourceOfPresentContent(source)->overrides(overrides);
				linksChanged();
			});
		linksChanged();
		messageLog->add(MessageLevel::INFO, "Unpacked " + nodeLabel(a_node) + " from " + source);
		return true;
	}

	bool App::revertBindSnap(const std::shared_ptr<MV::Scene::Node>& a_node) {
		auto link = linkOn(a_node);
		if (!link) {
			return false;
		}
		link->captureOverrides();
		const auto overrides = link->overrides();
		link->revert();
		history.record("revert " + nodeLabel(a_node),
			[this, a_node]() {
				if (auto linked = linkOn(a_node)) {
					linked->revert();
				}
				linksChanged();
			},
			[this, a_node, overrides]() {
				if (auto linked = linkOn(a_node)) {
					linked->overrides(overrides);
				}
				linksChanged();
			});
		linksChanged();
		return true;
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
		if (!selectionIsRemovable() || contentLocked(selectedNode, "delete")) {
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

	namespace {
		// A name no node beside it has: node, node_2, node_3.
		std::string unusedChildId(const std::shared_ptr<MV::Scene::Node>& a_parent, const std::string& a_base) {
			auto taken = [&a_parent](const std::string& a_id) {
				return std::any_of(a_parent->begin(), a_parent->end(), [&a_id](const std::shared_ptr<MV::Scene::Node>& a_child) { return a_child->id() == a_id; });
			};
			std::string result = a_base;
			for (int number = 2; taken(result); ++number) {
				result = a_base + "_" + std::to_string(number);
			}
			return result;
		}

		struct AttachableComponent {
			std::string typeName;
			std::function<std::shared_ptr<MV::Scene::Component>(const std::shared_ptr<MV::Scene::Node>&, Managers&, MV::TapDevice&)> attach;
		};

		// Each is made as the scene loader makes it, with the services a loaded one would be given, and sized so
		// that it can be seen and pressed straight away.
		std::vector<AttachableComponent> attachableComponentTypes() {
			return {
				{ "Sprite", [](const std::shared_ptr<MV::Scene::Node>& a_node, Managers&, MV::TapDevice&) -> std::shared_ptr<MV::Scene::Component> {
					return a_node->attach<MV::Scene::Sprite>()->bounds(MV::size(100.0f, 100.0f));
				} },
				{ "Text", [](const std::shared_ptr<MV::Scene::Node>& a_node, Managers& a_managers, MV::TapDevice&) -> std::shared_ptr<MV::Scene::Component> {
					auto text = a_node->attach<MV::Scene::Text>(a_managers.textLibrary, std::string("default"));
					text->bounds({ MV::Point<>(), MV::size(200.0f, 40.0f) });
					text->text("Text");
					return text.self();
				} },
				{ "Clickable", [](const std::shared_ptr<MV::Scene::Node>& a_node, Managers&, MV::TapDevice& a_mouse) -> std::shared_ptr<MV::Scene::Component> {
					return a_node->attach<MV::Scene::Clickable>(a_mouse)->bounds(MV::size(100.0f, 50.0f));
				} },
				{ "Button", [](const std::shared_ptr<MV::Scene::Node>& a_node, Managers&, MV::TapDevice& a_mouse) -> std::shared_ptr<MV::Scene::Component> {
					return a_node->attach<MV::Scene::Button>(a_mouse)->bounds(MV::size(100.0f, 50.0f));
				} },
				{ "FocusGroup", [](const std::shared_ptr<MV::Scene::Node>& a_node, Managers&, MV::TapDevice& a_mouse) -> std::shared_ptr<MV::Scene::Component> {
					return a_node->attach<MV::Scene::FocusGroup>(a_mouse).self();
				} },
				{ "Stencil", [](const std::shared_ptr<MV::Scene::Node>& a_node, Managers&, MV::TapDevice&) -> std::shared_ptr<MV::Scene::Component> {
					return a_node->attach<MV::Scene::Stencil>()->bounds(MV::size(100.0f, 100.0f));
				} },
				{ "Grid", [](const std::shared_ptr<MV::Scene::Node>& a_node, Managers&, MV::TapDevice&) -> std::shared_ptr<MV::Scene::Component> {
					return a_node->attach<MV::Scene::Grid>().self();
				} },
				{ "Parallax", [](const std::shared_ptr<MV::Scene::Node>& a_node, Managers&, MV::TapDevice&) -> std::shared_ptr<MV::Scene::Component> {
					auto parallax = a_node->attach<MV::Scene::Parallax>();
					parallax->authoring(true);
					return parallax.self();
				} } };
		}

		// "class MV::Scene::Button" -> "Button"
		std::string componentTypeName(const std::shared_ptr<MV::Scene::Component>& a_component) {
			std::string name = typeid(*a_component).name();
			auto lastColon = name.rfind("::");
			if (lastColon != std::string::npos) {
				return name.substr(lastColon + 2);
			}
			auto lastSpace = name.rfind(' ');
			return lastSpace == std::string::npos ? name : name.substr(lastSpace + 1);
		}

		std::vector<std::shared_ptr<MV::Scene::Component>> componentsOf(const std::shared_ptr<MV::Scene::Node>& a_node) {
			auto components = a_node->property_value<std::vector<std::shared_ptr<MV::Scene::Component>>>("childComponents");
			return components ? *components : std::vector<std::shared_ptr<MV::Scene::Component>>();
		}

		// attach puts a component last: taking each off and putting it back in turn leaves them in a_order
		void putComponentsInOrder(const std::shared_ptr<MV::Scene::Node>& a_node, const std::vector<std::shared_ptr<MV::Scene::Component>>& a_order) {
			for (auto&& component : a_order) {
				a_node->detach(component);
				a_node->attach(component);
			}
		}
	}

	std::shared_ptr<MV::Scene::Node> App::createChildNode() {
		auto parent = selectedNode && inScene(selectedNode) ? selectedNode : sceneRoot;
		if (contentLocked(parent, "create a child of")) {
			return nullptr;
		}
		auto node = parent->make(unusedChildId(parent, "node"));
		std::weak_ptr<MV::Scene::Node> weakParent = parent;
		history.record("create " + nodeLabel(node),
			[this, parent, node]() {
				parent->add(node, false);
				select(node);
			},
			[this, parent, node]() {
				parent->remove(node, false);
				select(parent == sceneRoot ? nullptr : parent);
			});
		if (treePanel) {
			treePanel->refresh();
		}
		select(node);
		return node;
	}

	std::vector<std::string> App::attachableComponents() const {
		std::vector<std::string> result;
		for (auto&& type : attachableComponentTypes()) {
			result.push_back(type.typeName);
		}
		return result;
	}

	std::vector<MenuItem> App::addComponentItems() {
		std::vector<MenuItem> result;
		for (auto&& typeName : attachableComponents()) {
			result.push_back({ typeName, "", [this, typeName]() { attachComponent(typeName); } });
		}
		return result;
	}

	std::shared_ptr<MV::Scene::Component> App::attachComponent(const std::string& a_typeName) {
		auto node = selectedNode;
		if (!node || !inScene(node) || contentLocked(node, "add a component to")) {
			return nullptr;
		}
		for (auto&& type : attachableComponentTypes()) {
			if (type.typeName != a_typeName) {
				continue;
			}
			try {
				auto component = type.attach(node, managers, mouse);
				history.record("add " + a_typeName + " to " + nodeLabel(node),
					[node, component]() { node->attach(component); },
					[this, node, component]() {
						if (gizmos->editedComponent() == component) {
							gizmos->editComponent(nullptr);
						}
						node->detach(component);
					});
				select(node);
				return component;
			} catch (const std::exception& a_error) {
				reportError("adding " + a_typeName + " failed (" + nodeLabel(node) + "): " + a_error.what());
				return nullptr;
			}
		}
		return nullptr;
	}

	bool App::removeComponent(const std::shared_ptr<MV::Scene::Component>& a_component) {
		auto node = a_component ? a_component->owner() : nullptr;
		if (!node || !inScene(node)) {
			return false;
		}
		if (std::dynamic_pointer_cast<MV::Scene::BindSnapInstance>(a_component)) {
			// taking the link off a node is unpacking it: its content stays, as the node's own
			return unpackBindSnap(node);
		}
		if (contentLocked(node, "remove a component of")) {
			return false;
		}
		if (auto link = linkOn(node); link && !a_component->serializable()) {
			messageLog->add(MessageLevel::WARNING, "Cannot remove a component of " + nodeLabel(node) + ": it is content of " + link->source() + ". Unpack " + nodeLabel(node) + " first, or change the file.");
			return false;
		}
		auto order = componentsOf(node);
		if (std::find(order.begin(), order.end(), a_component) == order.end()) {
			return false;
		}
		std::string typeName = componentTypeName(a_component);
		auto remove = [this, node, a_component]() {
			if (gizmos->editedComponent() == a_component) {
				gizmos->editComponent(nullptr);
			}
			node->detach(a_component);
		};
		remove();
		history.record("remove " + typeName + " from " + nodeLabel(node), remove, [node, a_component, order]() {
			node->attach(a_component);
			putComponentsInOrder(node, order);
		});
		select(selectedNode);
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
		if (!selectionIsRemovable() || contentLocked(selectedNode, "duplicate")) {
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

	void App::controllerCursor(bool a_enabled) {
		controller.cursor(a_enabled);
		settingsChanged();
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

	void App::showConsole() {
		dock->show(consolePanel);
	}

	void App::buildStatusBar(const MV::Size<>& a_worldSize) {
		const float height = theme.px(22.0f);
		const float countsWidth = theme.px(190.0f);
		statusBar->clear();
		statusBar->position(MV::point(0.0f, a_worldSize.height - height));
		auto band = statusBar->attach<MV::Scene::Sprite>()->bounds(MV::size(a_worldSize.width, height))->color(theme.windowBg());
		statusBar->make("edge")->attach<MV::Scene::Sprite>()->bounds(MV::size(a_worldSize.width, theme.px(1.0f)))->color(theme.header());
		statusText = makeLabel(statusBar, managers.services, theme, "latest", MV::size(std::max(theme.px(60.0f), a_worldSize.width - countsWidth - theme.px(24.0f)), height), "");
		statusText->owner()->position(MV::point(theme.px(8.0f), 0.0f));
		statusCountText = makeLabel(statusBar, managers.services, theme, "counts", MV::size(countsWidth, height), "");
		statusCountText->owner()->position(MV::point(a_worldSize.width - countsWidth - theme.px(8.0f), 0.0f));
		statusCountText->justification(MV::TextJustification::RIGHT);
		auto press = statusBar->make("press")->attach<MV::Scene::Clickable>(mouse)->bounds(MV::size(a_worldSize.width, height));
		press->onAccept.connect("console", [this](const std::shared_ptr<MV::Scene::Clickable>&) {
			showConsole();
		});
		hoverTint(press, band, theme.windowBg(), theme.panelBg());
		tooltip(managers.services, press, "Open the Console");
		statusStale = true;
	}

	void App::refreshStatus() {
		statusStale = false;
		if (!statusText || !statusCountText) {
			return;
		}
		auto&& all = messageLog->messages();
		shownStatus = all.empty() ? std::string() : all.back().text + (all.back().repeats > 1 ? " (x" + std::to_string(all.back().repeats) + ")" : "");
		std::string coloured = all.empty() ? std::string() : ConsolePanel::levelMarkup(all.back().level) + fitLabelText(managers.services, theme, shownStatus, statusText->bounds().width());
		statusText->text(coloured);
		auto counted = [](size_t a_count, const char* a_one, const char* a_many) {
			return std::to_string(a_count) + " " + (a_count == 1 ? a_one : a_many);
		};
		const size_t failures = messageLog->count(MessageLevel::FAILURE);
		const size_t warnings = messageLog->count(MessageLevel::WARNING);
		shownCounts.clear();
		std::string colouredCounts;
		if (failures > 0) {
			shownCounts = counted(failures, "error", "errors");
			colouredCounts = ConsolePanel::levelMarkup(MessageLevel::FAILURE) + shownCounts;
		}
		if (warnings > 0) {
			std::string text = counted(warnings, "warning", "warnings");
			colouredCounts += (shownCounts.empty() ? "" : "   ") + ConsolePanel::levelMarkup(MessageLevel::WARNING) + text;
			shownCounts += (shownCounts.empty() ? "" : "   ") + text;
		}
		statusCountText->text(colouredCounts);
	}

	MV::Point<int> App::statusBarScreenPoint() const {
		auto box = statusBar->screenBounds();
		return MV::Point<int>((box.minPoint.x + box.maxPoint.x) / 2, (box.minPoint.y + box.maxPoint.y) / 2);
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
			if (name == "controllerCursor") {
				controller.cursor(trimmed(line.substr(equals + 1)) == "on");
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
		file << "controllerCursor = " << (controllerCursor() ? "on" : "off") << "\n";
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
			{ "History", "", [this]() { sceneToolbar->historyShown(!sceneToolbar->historyShown()); }, true, [this]() { return std::string(sceneToolbar->historyShown() ? "Hide History" : "Show History"); } },
			{ "Create Child Node", "", [this]() { createChildNode(); }, true } });
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
			{
				// the layers going to rest is not a change to a linked node, and its file is read as of now
				bindSnapWriteTimes.clear();
				std::vector<std::shared_ptr<MV::Scene::BindSnapInstance>> links;
				collectLinks(sceneRoot, links);
				for (auto&& link : links) {
					link->markUnchanged();
					noteBindSnapWritten(bindSnapSourceName(link->source()));
				}
			}
			if (treePanel) {
				treePanel->refresh();
			}
			frameSceneInViewport();
			documentPath = a_fileName;
			refreshTitle();
			std::cout << "Workbench loaded: " << a_fileName << std::endl;
			messageLog->add(MessageLevel::INFO, "Loaded " + a_fileName);
			return true;
		} catch (const std::exception& a_error) {
			reportError("load failed (" + a_fileName + "): " + a_error.what());
			return false;
		}
	}

	// a previewed layer is displaced: it goes back to rest for a write
	void App::previewedLayersDisplaced(bool a_displaced) {
		for (auto&& weakParallax : previewedParallax) {
			if (auto parallax = weakParallax.lock()) {
				parallax->authoringPreview(a_displaced);
			}
		}
	}

	std::filesystem::path App::bindSnapFileFor(const std::shared_ptr<MV::Scene::Node>& a_node, const std::string& a_folder) {
		// a linked node goes by its file's name, whatever the node is called
		if (auto link = linkOn(a_node)) {
			return std::filesystem::path(a_folder) / std::filesystem::path(link->source()).filename();
		}
		std::string name = a_node->id();
		for (auto&& character : name) {
			if (static_cast<unsigned char>(character) < 32 || std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos) {
				character = '_';
			}
		}
		return std::filesystem::path(a_folder) / ((name.empty() ? std::string("node") : name) + ".bindsnap");
	}

	std::string App::saveAsBindSnap(const std::shared_ptr<MV::Scene::Node>& a_node, const std::string& a_folder, BindSnapClash a_onClash) {
		if (!a_node || contentLocked(a_node, "save")) {
			return {};
		}
		auto file = bindSnapFileFor(a_node, a_folder);
		std::error_code ignored;
		const auto kind = bindSnapDropKind(a_node, file);
		if (!(kind == BindSnapDrop::ASK && a_onClash == BindSnapClash::NEW_ASSET) && namesFile(a_node, bindSnapSourceName(file), false)) {
			messageLog->add(MessageLevel::WARNING, "Not saved: something in " + nodeLabel(a_node) + " is linked to " + file.generic_string() + ", which would then hold itself");
			return {};
		}
		std::optional<CommandHistory::Group> steps;
		if (!linkOn(a_node) || (kind == BindSnapDrop::ASK && a_onClash == BindSnapClash::NEW_ASSET)) {
			steps.emplace(history, "save " + nodeLabel(a_node) + " as a BindSnap");
		}
		if (kind == BindSnapDrop::ASK && a_onClash == BindSnapClash::NEW_ASSET) {
			// a name no file in the folder has and no node beside this one
			auto parent = a_node->parent();
			const std::string base = file.stem().string();
			std::string name;
			for (int number = 2; ; ++number) {
				name = base + "_" + std::to_string(number);
				const bool besideIt = parent && std::any_of(parent->begin(), parent->end(), [&](const std::shared_ptr<MV::Scene::Node>& a_child) { return a_child != a_node && a_child->id() == name; });
				if (!besideIt && !std::filesystem::exists(std::filesystem::path(a_folder) / (name + ".bindsnap"), ignored)) {
					break;
				}
			}
			const auto oldId = a_node->id();
			a_node->id(name);
			history.record("rename " + oldId,
				[a_node, name]() { a_node->id(name); },
				[a_node, oldId]() { a_node->id(oldId); });
			file = bindSnapFileFor(a_node, a_folder);
		}
		auto shown = file.generic_string();
		// the leading "./" keeps MV's file layer from redirecting a write to the player-preferences folder
		auto writePath = file.is_absolute() || shown.starts_with("./") ? shown : "./" + shown;
		const bool replaced = std::filesystem::exists(file, ignored);
		const auto sourceName = bindSnapSourceName(file);
		std::string result;
		previewedLayersDisplaced(false);
		try {
			captureLinks(a_node);
			auto link = linkOn(a_node);
			if (link) {
				const auto oldSource = link->source();
				link->saveContentAs(writePath);
				if (bindSnapSourceName(oldSource) != sourceName) {
					link->sourceOfPresentContent(sourceName);
					history.record("link " + nodeLabel(a_node),
						[a_node, sourceName]() {
							if (auto linked = linkOn(a_node)) {
								linked->sourceOfPresentContent(sourceName);
							}
						},
						[a_node, oldSource]() {
							if (auto linked = linkOn(a_node)) {
								linked->sourceOfPresentContent(oldSource);
							}
						});
				}
			} else {
				a_node->saveJai(writePath, managers.services, false);
			}
			if (!std::filesystem::exists(file, ignored)) {
				throw std::runtime_error("nothing was written");
			}
			if (!link) {
				auto* services = &managers.services;
				auto linkIt = [services, a_node, sourceName]() {
					a_node->attach<MV::Scene::BindSnapInstance>(*services)->sourceOfPresentContent(sourceName);
				};
				linkIt();
				history.record("link " + nodeLabel(a_node), linkIt, [a_node]() {
					if (auto linked = linkOn(a_node)) {
						linked->unpack();
					}
				});
			}
			result = shown;
			std::cout << "Workbench saved BindSnap: " << shown << std::endl;
			messageLog->add(MessageLevel::INFO, (kind == BindSnapDrop::APPLY ? "Applied to " : replaced ? "Replaced " : "Saved ") + shown);
		} catch (const std::exception& a_error) {
			reportError("BindSnap save failed (" + shown + "): " + a_error.what());
		}
		previewedLayersDisplaced(true);
		steps.reset();
		if (!result.empty()) {
			noteBindSnapWritten(sourceName);
			refreshLinksTo(sourceName, a_node);
		}
		if (projectPanel) {
			projectPanel->refresh();
		}
		linksChanged();
		return result;
	}

	bool App::saveScene(const std::string& a_fileName) {
		bool result = true;
		previewedLayersDisplaced(false);
		try {
			auto assetPath = assetFile(a_fileName);
			// each linked node's changes go into the scene as its overrides
			captureLinks(sceneRoot);
			sceneRoot->saveJai(assetPath, managers.services, false);
			documentPath = a_fileName;
			history.markSaved();
			std::cout << "Workbench saved: " << assetPath << std::endl;
			messageLog->add(MessageLevel::INFO, "Saved " + assetPath);
		} catch (const std::exception& a_error) {
			reportError("save failed (" + a_fileName + "): " + a_error.what());
			result = false;
		}
		previewedLayersDisplaced(true);
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
		bindSnapCheckWait += a_dt;
		if (bindSnapCheckWait >= 0.5) {
			bindSnapCheckWait = 0.0;
			checkBindSnapFiles();
		}
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
			const float statusHeight = theme.px(22.0f);
			dock->layout(DockRect{ MV::point(0.0f, theme.titleBarHeight), MV::Size<>(worldSize.width, worldSize.height - theme.titleBarHeight - statusHeight) });
			layoutTitleBar(worldSize);
			buildStatusBar(worldSize);
		}
		messageLog->drain();
		if (statusStale) {
			refreshStatus();
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

	// What a right press offers depends on what is under it: a dock tab, a text field, a Hierarchy row, or the scene.
	void App::openContextMenu(const MV::Point<int>& a_screenPoint) {
		std::vector<MenuItem> items;
		for (auto&& subject : mouse.hoveredSubjects()) {
			auto clickable = std::static_pointer_cast<MV::Scene::Clickable>(subject);
			if (!clickable->ownerIsAlive()) {
				continue;
			}
			auto node = clickable->owner();
			if (auto* panel = dock->tabPanelOf(node)) {
				items = tabContextItems(panel);
			} else if (clickable->focusable() && clickable->pressOnFocus() && !inScene(node)) {
				// one of the editor's own fields: a text box in the scene being edited is left alone
				items = textContextItems(clickable);
			} else if (treePanel && dock->panelContentContaining(node) == treePanel->content()) {
				if (auto rowNode = treePanel->nodeAt(a_screenPoint)) {
					select(rowNode);
					items = nodeContextItems();
				}
			}
			if (!items.empty()) {
				break;
			}
		}
		if (items.empty() && projectPanel && dock->panelShowingAt(managers.renderer.worldFromScreen(a_screenPoint)) == projectPanel) {
			items = projectContextItems(a_screenPoint);
		}
		if (items.empty() && gizmos->pointerOverScene(a_screenPoint)) {
			pickSceneNode(a_screenPoint);
			items = nodeContextItems();
		}
		menuBar->openContext(a_screenPoint, items);
	}

	std::vector<MenuItem> App::nodeContextItems() {
		std::vector<MenuItem> result {
			{ "Create Child", "", [this]() { createChildNode(); } },
			{ "Frame", "F", [this]() { frameSelectionInViewport(); }, true, {}, [this]() { return static_cast<bool>(selectedNode); } },
			{ "Duplicate", "Ctrl+D", [this]() { duplicateSelected(); }, true, {}, [this]() { return selectionIsRemovable(); } },
			{ "Delete", "Del", [this]() { deleteSelected(); }, false, {}, [this]() { return selectionIsRemovable(); } } };
		if (linkOn(selectedNode)) {
			result.push_back({ "Revert to BindSnap", "", [this]() { revertBindSnap(selectedNode); }, true });
			result.push_back({ "Unpack BindSnap", "", [this]() { unpackBindSnap(selectedNode); } });
		}
		return result;
	}

	std::vector<MenuItem> App::tabContextItems(Panel* a_panel) {
		return {
			{ "Close", "", [this, a_panel]() { dock->closePanel(a_panel); }, false, {}, [this, a_panel]() { return dock->canClose(a_panel); } },
			{ "Float", "", [this, a_panel]() { dock->floatPanel(a_panel); }, false, {}, [this, a_panel]() { return dock->canFloat(a_panel); } } };
	}

	std::vector<MenuItem> App::textContextItems(const std::shared_ptr<MV::Scene::Clickable>& a_field) {
		// the field is typed in first, unless it already is: then what is selected in it stays selected
		bool holdsFocus = mouse.focusedSubject() == a_field;
		if (!focusRouter.active() || !holdsFocus) {
			if (holdsFocus) {
				mouse.focusActivate();
			} else {
				a_field->focus();
			}
		}
		auto text = focusRouter.active();
		if (!text) {
			return {};
		}
		std::weak_ptr<MV::Scene::Text> weakText = text;
		auto onText = [weakText](std::shared_ptr<MV::Scene::Text> (MV::Scene::Text::*a_method)()) {
			return [weakText, a_method]() {
				if (auto locked = weakText.lock()) {
					((*locked).*a_method)();
				}
			};
		};
		auto selected = [weakText]() {
			auto locked = weakText.lock();
			return locked && locked->hasSelection();
		};
		return {
			{ "Cut", "Ctrl+X", onText(&MV::Scene::Text::cutToClipboard), false, {}, selected },
			{ "Copy", "Ctrl+C", onText(&MV::Scene::Text::copyToClipboard) },
			{ "Paste", "Ctrl+V", onText(&MV::Scene::Text::pasteFromClipboard), false, {}, []() { return SDL_HasClipboardText() == SDL_TRUE; } },
			{ "Select All", "Ctrl+A", onText(&MV::Scene::Text::selectAll), true } };
	}

	// Tab and Shift+Tab walk the focusable controls of the panel the focus is in; Enter and Space press a focused
	// control that is not a text field; Escape lets the focus go.
	bool App::handleFocusKey(const SDL_Event& a_event) {
		if (a_event.type != SDL_KEYDOWN || (SDL_GetModState() & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) {
			return false;
		}
		auto key = a_event.key.keysym.sym;
		if (key == SDLK_TAB) {
			auto focusedClickable = std::static_pointer_cast<MV::Scene::Clickable>(mouse.focusedSubject());
			auto from = focusRouter.active() ? focusRouter.active()->owner() : focusedClickable ? focusedClickable->owner() : nullptr;
			auto scope = from ? dock->panelContentContaining(from) : nullptr;
			if (!from) {
				return false;
			}
			focusRouter.leave();
			if (SDL_GetModState() & KMOD_SHIFT) {
				mouse.focusPrevious(scope.get());
			} else {
				mouse.focusNext(scope.get());
			}
			return true;
		}
		if (focusRouter.hasActive()) {
			return false;
		}
		if ((key == SDLK_RETURN || key == SDLK_SPACE) && mouse.hasFocus()) {
			return mouse.focusActivate();
		}
		if (key == SDLK_ESCAPE && mouse.hasFocus()) {
			mouse.focusClear();
			return true;
		}
		return false;
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
			// a press on a field's own menu is not a press away from the field
			bool onContextMenu = event.type == SDL_MOUSEBUTTONDOWN && menuBar->contextOpen() && menuBar->popupContains(MV::Point<int>(event.button.x, event.button.y));
			menuBar->handleInput(event);
			if (handleFocusKey(event)) {
				continue;
			}
			if (!onContextMenu && focusRouter.handleEvent(event)) {
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
		controller.update();
		// the pad walks the editor's own controls, never the buttons of the scene being edited
		controller.cursorBounds(managers.renderer.window().windowSize());
		const bool cursorMode = controller.cursor();
		controller.navigate(mouse, lastDelta, uiRoot.get());
		if (controller.cursor() != cursorMode) {
			settingsChanged();
		}
		if (!controllerCursorArt) {
			controllerCursorArt = std::make_unique<MV::Scene::ControllerCursor>(uiRoot, controller, managers.textures);
			controllerCursorArt->dot(theme.px(10.0f), theme.accent());
		}
		controllerCursorArt->update();
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
