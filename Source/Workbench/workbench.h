#ifndef _WORKBENCH_WORKBENCH_H_
#define _WORKBENCH_WORKBENCH_H_

#include "Game/managers.h"
#include "MV/Interface/tapDevice.h"
#include "MV/Interface/controllerDevice.h"
#include "MV/Render/Scene/controllerCursor.h"
#include "commands.h"
#include "dock.h"
#include "gizmos.h"
#include "sceneTreePanel.h"
#include "inspector.h"
#include "menu.h"
#include "sceneGrid.h"
#include "sceneToolbar.h"
#include "settingsPanel.h"
#include "tooltips.h"
#include "consolePanel.h"
#include "messageLog.h"
#include "MV/Render/Scene/parallax.h"

namespace Workbench {

	class App {
	public:
		App(Managers& a_managers, MV::TapDevice& a_mouse, const Theme& a_theme);
		~App();

		void dropPrefab(const std::string& a_fileName, const MV::Point<int>& a_screenPoint);

		// Removes the selected node, or adds a copy of it beside it; false when the selection is nothing or the scene's root.
		bool deleteSelected();
		bool duplicateSelected();
		CommandHistory& commands() { return history; }
		// The scene has edits its file does not.
		bool modified() const { return history.modified(); }

		enum class UnsavedChoice { SAVE, DISCARD, CANCEL };
		// Asked before a modified scene is replaced or the editor closes; a native message box unless replaced.
		std::function<UnsavedChoice(const std::string& a_documentName)> askAboutUnsavedChanges;
		// What the menus, shortcuts and panels call: each asks about unsaved changes first.
		void requestNewScene();
		void requestOpenScene();
		bool requestLoadScene(const std::string& a_fileName);
		void requestExit();
		bool exiting() const { return done; }

		// The most undo steps kept. Fewer than the history holds asks first, since the steps past it are dropped.
		size_t undoSteps() const { return history.limit(); }
		bool requestUndoSteps(size_t a_steps);
		// The scene grid's cell. Its lines sit a tenth of the cell apart, and a snapped move lands on those.
		MV::Size<> gridSize() const { return sceneGridSize; }
		void gridSize(const MV::Size<>& a_size);
		// The degrees a snapped rotation lands on.
		float rotationSnap() const { return gizmos->snapSteps.rotate; }
		void rotationSnap(float a_degrees);
		// Asked before something that cannot be taken back; a native Okay / Cancel box unless replaced.
		std::function<bool(const std::string& a_message)> askToConfirm;
		// Reads the editor's settings from this file and writes them back to it when one changes.
		void useSettingsFile(const std::string& a_path);
		// The dock's arrangement as one line of text, and back; false when the text is not a layout. The arrangement
		// in use is kept in the settings file. Reset returns to the one the editor starts with.
		std::string dockLayout() const { return dock->describeLayout(); }
		bool dockLayout(const std::string& a_description);
		void resetDockLayout();
		bool saveDockLayout(const std::string& a_fileName);
		bool loadDockLayout(const std::string& a_fileName);
		DockSpace& docking() { return *dock; }
		Tooltips& tips() { return *tooltips; }
		// The text field being typed into, if any.
		std::shared_ptr<MV::Scene::Text> editingText() const { return focusRouter.active(); }
		// What the editor and the engine have reported. The Console panel lists it; the bar along the bottom of the
		// window shows the latest line and how many errors and warnings there have been, and a press on it opens the Console.
		MessageLog& messages() { return *messageLog; }
		ConsolePanel& console() { return *consolePanel; }
		void showConsole();
		const std::string& statusLine() const { return shownStatus; }
		const std::string& statusCounts() const { return shownCounts; }
		MV::Point<int> statusBarScreenPoint() const;
		// The scene's own clock (emitters, Spine, shader time) is stopped; the editor keeps running.
		bool paused() const { return scenePaused; }
		// The title bar's pause, step and speed buttons, left to right.
		MV::Point<int> transportScreenPoint(int a_index) const;
		void showSettings();
		void showInspector();
		SettingsPanel& settings() { return *settingsPanel; }

		int run();
		int runFrames(int a_frameCount, const std::string& a_screenshotPath = "");

		std::shared_ptr<MV::Scene::Node> scene() const { return sceneRoot; }
		std::shared_ptr<MV::Scene::Node> selected() const { return selectedNode; }
		void select(const std::shared_ptr<MV::Scene::Node>& a_node);
		bool loadScene(const std::string& a_fileName);
		bool saveScene(const std::string& a_fileName);
		void newScene();
		// File menu: the platform's pickers, and Save to the file the scene came from (Save As when it has none).
		void openSceneWithDialog();
		bool saveSceneWithDialog();
		bool saveCurrentScene();
		// Left part of the title bar that takes presses (menus and transport); the rest drags the window.
		float titleBarInteractiveWidth() const { return titleInteractiveWidth; }

		// Fits the selected node in the viewport, or the whole scene when nothing is selected (next laid-out frame).
		void frameSelectionInViewport() { selectionFramePending = true; frameAwaitsDraw = true; }
		// Fits the whole scene in the viewport (next laid-out frame).
		void frameSceneInViewport() { sceneFramePending = true; frameAwaitsDraw = true; }
		// The Scene panel's content rectangle in world coordinates; zero-size before the first layout.
		DockRect viewport() const { return dock->viewportRect(); }

		// The transform handles on the selection: Q select, W move, E rotate, R scale.
		void gizmoTool(Tool a_tool);
		Tool gizmoTool() const { return gizmos->tool(); }
		GizmoLayer& gizmoLayer() { return *gizmos; }
		SceneToolbar& sceneTools() { return *sceneToolbar; }
		SceneGrid& grid() { return *sceneGrid; }
		MenuBar& menus() { return *menuBar; }
		InspectorPanel& inspector() { return *inspectorPanel; }
		SceneTreePanel& hierarchy() { return *treePanel; }
		// The game controller: it moves the keyboard focus, presses what holds it, and its east button lets go.
		MV::ControllerDevice& pad() { return controller; }
		// Whether the controller's left stick moves a pointer of its own, drawn as a dot, instead of the focus.
		bool controllerCursor() const { return controller.cursor(); }
		void controllerCursor(bool a_enabled);
		bool controllerCursorVisible() const { return controllerCursorArt && controllerCursorArt->visible(); }
		const std::string& document() const { return documentPath; }
		// The title bar's text: the document's name, starred while it is modified.
		const std::string& title() const { return shownTitle; }

		// Displaces the Parallax layers on the selected node and below it as a run would, until the selection changes.
		// A save writes the layers at rest either way.
		void parallaxPreview(bool a_preview);
		bool parallaxPreviewing() const { return !previewedParallax.empty(); }

		// Every failure the editor survives is counted; the smoke modes exit nonzero on any.
		void reportError(const std::string& a_message);
		int errors() const { return errorCount; }

	private:
		bool update(double a_dt);
		void handleInput();
		void render();
		void updateCameraKeys(double a_dt);
		bool handleShortcutKey(SDL_Keycode a_key);
		bool handleFocusKey(const SDL_Event& a_event);
		void openContextMenu(const MV::Point<int>& a_screenPoint);
		std::vector<MenuItem> nodeContextItems();
		std::vector<MenuItem> tabContextItems(Panel* a_panel);
		std::vector<MenuItem> textContextItems(const std::shared_ptr<MV::Scene::Clickable>& a_field);
		void buildTitleBar();
		void buildStatusBar(const MV::Size<>& a_worldSize);
		void refreshStatus();
		void refreshTitle();
		void layoutTitleBar(const MV::Size<>& a_worldSize);
		void captureScreenshot(const std::string& a_path);
		void syncViewportClip();
		void pickSceneNode(const MV::Point<int>& a_screenPoint);
		bool inScene(const std::shared_ptr<MV::Scene::Node>& a_node) const;
		bool selectionIsRemovable() const;
		void clearReferencesInto(const std::shared_ptr<MV::Scene::Node>& a_leaving);
		bool unsavedChangesResolved();
		std::string documentName() const;
		static std::string assetFile(const std::string& a_fileName);
		static void forEachParallax(const std::shared_ptr<MV::Scene::Node>& a_node, const std::function<void(const std::shared_ptr<MV::Scene::Parallax>&)>& a_visit);
		void frameNode(const DockRect& a_view, const std::shared_ptr<MV::Scene::Node>& a_node, float a_fill, float a_maximumZoom);
		template <typename Step>
		void guarded(const char* a_stage, Step&& a_step);

		Managers& managers;
		MV::TapDevice& mouse;
		MV::ControllerDevice controller;
		MV::ControllerDevice::ButtonSignalType controllerReceiver;
		Theme theme;
		FocusRouter focusRouter;
		CommandHistory history;

		std::shared_ptr<MV::Scene::Node> visor;
		std::shared_ptr<MV::Scene::Node> viewportClip;
		// The editor's camera: pan and zoom live here, so the document root keeps the transform its file gave it.
		std::shared_ptr<MV::Scene::Node> viewCamera;
		std::shared_ptr<MV::Scene::Node> sceneRoot;
		std::shared_ptr<MV::Scene::Node> uiRoot;
		MV::Scene::SafeComponent<MV::Scene::Stencil> viewportStencil;
		DockRect lastViewportRect;

		std::shared_ptr<MV::Scene::Node> gizmoOverlay;
		std::unique_ptr<GizmoLayer> gizmos;
		std::unique_ptr<SceneGrid> sceneGrid;
		std::unique_ptr<SceneToolbar> sceneToolbar;
		std::shared_ptr<MV::Scene::Node> selectedNode;
		std::vector<std::weak_ptr<MV::Scene::Parallax>> previewedParallax;

		std::unique_ptr<DockSpace> dock;
		std::unique_ptr<MenuBar> menuBar;
		std::unique_ptr<Tooltips> tooltips;
		std::unique_ptr<MV::Scene::ControllerCursor> controllerCursorArt;
		std::vector<std::weak_ptr<MV::Scene::Node>> transportButtons;
		std::vector<Panel*> menuPanels;
		std::shared_ptr<MV::Scene::Text> titleText;
		std::string documentPath;
		std::string shownTitle;
		float titleInteractiveWidth = 0.0f;
		SceneTreePanel* treePanel = nullptr;
		InspectorPanel* inspectorPanel = nullptr;
		SettingsPanel* settingsPanel = nullptr;
		ConsolePanel* consolePanel = nullptr;
		std::shared_ptr<MessageLog> messageLog;
		std::shared_ptr<MV::Scene::Node> statusBar;
		std::shared_ptr<MV::Scene::Text> statusText;
		std::shared_ptr<MV::Scene::Text> statusCountText;
		std::string shownStatus;
		std::string shownCounts;
		bool statusStale = true;
		std::string settingsPath;
		std::string defaultDockLayout;
		MV::Size<> sceneGridSize { 100.0f, 100.0f };
		void applyGridSize();
		void settingsChanged();
		void writeSettingsFile();

		MV::Size<> lastWorldSize;
		double lastDelta = 0.0;
		bool done = false;

		// Scene animation clock (the transport's ||/>|/speed): emitters, spine, and shader
		// time preview on this clock; editor UI stays on real time.
		bool scenePaused = false;
		float sceneSpeed = 1.0f;
		bool sceneStepQueued = false;
		double lastSceneDelta = 0.0;

		bool selectionFramePending = false;
		bool sceneFramePending = false;
		// A frame request waits for one draw: Parallax layers settle to rest in the update a draw runs.
		bool frameAwaitsDraw = false;

		int errorCount = 0;
		std::string lastErrorMessage;

		// A left drag on the empty viewport pans and a left click picks; a middle or right drag only pans.
		enum class PanButton { NONE, LEFT, MIDDLE, RIGHT };
		void beginPan(PanButton a_button, const MV::Point<int>& a_screenPoint);
		void endPan(PanButton a_button, const MV::Point<int>& a_screenPoint);

		PanButton panButton = PanButton::NONE;
		bool panMoved = false;
		MV::Point<int> panStart;
		MV::Point<int> panLast;
		std::vector<MV::TapDevice::SignalType> panReceivers;
		MV::Point<int> contextPress;

		SDL_Cursor* cursorArrow = nullptr;
		SDL_Cursor* cursorResizeH = nullptr;
		SDL_Cursor* cursorResizeV = nullptr;
		DockSpace::CursorHint activeCursor = DockSpace::CursorHint::None;
	};

}

#endif
