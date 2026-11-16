#ifndef _WORKBENCH_WORKBENCH_H_
#define _WORKBENCH_WORKBENCH_H_

#include "Game/managers.h"
#include "MV/Interface/tapDevice.h"
#include "commands.h"
#include "dock.h"
#include "gizmos.h"
#include "sceneTreePanel.h"
#include "inspector.h"
#include "menu.h"
#include "MV/Render/Scene/parallax.h"

namespace Workbench {

	class App {
	public:
		App(Managers& a_managers, MV::TapDevice& a_mouse, const Theme& a_theme);
		~App();

		void dropPrefab(const std::string& a_fileName, const MV::Point<int>& a_screenPoint);

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
		void saveSceneWithDialog();
		void saveCurrentScene();
		// Left part of the title bar that takes presses (menus and transport); the rest drags the window.
		float titleBarInteractiveWidth() const { return titleInteractiveWidth; }

		// Fits the selected node in the viewport, or the whole scene when nothing is selected (next laid-out frame).
		void frameSelectionInViewport() { selectionFramePending = true; frameAwaitsDraw = true; }
		// Fits the whole scene in the viewport (next laid-out frame).
		void frameSceneInViewport() { sceneFramePending = true; frameAwaitsDraw = true; }
		// The Scene panel's content rectangle in world coordinates; zero-size before the first layout.
		DockRect viewport() const { return dock->viewportRect(); }

		// The transform handles on the selection: Q select, W move, E rotate, R scale.
		void gizmoTool(Tool a_tool) { gizmos->tool(a_tool); }
		Tool gizmoTool() const { return gizmos->tool(); }
		GizmoLayer& gizmoLayer() { return *gizmos; }

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
		void buildTitleBar();
		void refreshTitle();
		void layoutTitleBar(const MV::Size<>& a_worldSize);
		void captureScreenshot(const std::string& a_path);
		void syncViewportClip();
		void pickSceneNode(const MV::Point<int>& a_screenPoint);
		bool inScene(const std::shared_ptr<MV::Scene::Node>& a_node) const;
		static std::string assetFile(const std::string& a_fileName);
		static void forEachParallax(const std::shared_ptr<MV::Scene::Node>& a_node, const std::function<void(const std::shared_ptr<MV::Scene::Parallax>&)>& a_visit);
		void frameNode(const DockRect& a_view, const std::shared_ptr<MV::Scene::Node>& a_node, float a_fill, float a_maximumZoom);
		template <typename Step>
		void guarded(const char* a_stage, Step&& a_step);

		Managers& managers;
		MV::TapDevice& mouse;
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
		std::shared_ptr<MV::Scene::Node> selectedNode;
		std::vector<std::weak_ptr<MV::Scene::Parallax>> previewedParallax;

		std::unique_ptr<DockSpace> dock;
		std::unique_ptr<MenuBar> menuBar;
		std::vector<Panel*> menuPanels;
		std::shared_ptr<MV::Scene::Text> titleText;
		std::string documentPath;
		float titleInteractiveWidth = 0.0f;
		SceneTreePanel* treePanel = nullptr;
		InspectorPanel* inspectorPanel = nullptr;

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

		SDL_Cursor* cursorArrow = nullptr;
		SDL_Cursor* cursorResizeH = nullptr;
		SDL_Cursor* cursorResizeV = nullptr;
		DockSpace::CursorHint activeCursor = DockSpace::CursorHint::None;
	};

}

#endif
