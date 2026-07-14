#ifndef _WORKBENCH_WORKBENCH_H_
#define _WORKBENCH_WORKBENCH_H_

#include "Game/managers.h"
#include "MV/Interface/tapDevice.h"
#include "commands.h"
#include "dock.h"
#include "gizmos.h"
#include "sceneTreePanel.h"
#include "inspector.h"

namespace Workbench {

	class App {
	public:
		App(Managers& a_managers, MV::TapDevice& a_mouse, const Theme& a_theme);
		~App();

		void dropPrefab(const std::string& a_fileName, const MV::Point<int>& a_screenPoint);

		int run();
		int runFrames(int a_frameCount, const std::string& a_screenshotPath = "");

		std::shared_ptr<MV::Scene::Node> scene() const { return sceneRoot; }
		void select(const std::shared_ptr<MV::Scene::Node>& a_node);
		void loadScene(const std::string& a_fileName);
		void saveScene(const std::string& a_fileName);

		// Pans the viewport so the selected node sits at its center (next laid-out frame).
		void frameSelectionInViewport() { selectionFramePending = true; }

	private:
		bool update(double a_dt);
		void handleInput();
		void render();
		void updateCameraKeys(double a_dt);
		void buildTitleBar();
		void layoutTitleBar(const MV::Size<>& a_worldSize);
		void captureScreenshot(const std::string& a_path);
		void syncViewportClip();
		void pickSceneNode(const MV::Point<int>& a_screenPoint);

		Managers& managers;
		MV::TapDevice& mouse;
		Theme theme;
		FocusRouter focusRouter;
		CommandHistory history;

		std::shared_ptr<MV::Scene::Node> visor;
		std::shared_ptr<MV::Scene::Node> viewportClip;
		std::shared_ptr<MV::Scene::Node> sceneRoot;
		std::shared_ptr<MV::Scene::Node> uiRoot;
		MV::Scene::SafeComponent<MV::Scene::Stencil> viewportStencil;
		DockRect lastViewportRect;

		std::shared_ptr<MV::Scene::Node> gizmoOverlay;
		std::unique_ptr<GizmoLayer> gizmos;
		std::shared_ptr<MV::Scene::Node> selectedNode;

		std::unique_ptr<DockSpace> dock;
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

		bool panning = false;
		bool panMoved = false;
		MV::Point<int> panStart;
		MV::Point<int> panLast;
		MV::TapDevice::SignalType panDownReceiver;
		MV::TapDevice::SignalType panMoveReceiver;
		MV::TapDevice::SignalType panUpReceiver;

		SDL_Cursor* cursorArrow = nullptr;
		SDL_Cursor* cursorResizeH = nullptr;
		SDL_Cursor* cursorResizeV = nullptr;
		DockSpace::CursorHint activeCursor = DockSpace::CursorHint::None;
	};

}

#endif
