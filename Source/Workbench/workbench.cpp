#include "workbench.h"
#include "projectPanel.h"
#include "scriptPanel.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace Workbench {

	namespace {
		class ViewportPanel : public Panel {
		public:
			ViewportPanel() : Panel("Scene", true) {}
		};
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
		sceneRoot = viewportClip->make("root");
		// Sibling after sceneRoot: draws above the scene, stencil-clipped, never serialized, and
		// stays unscaled so handles keep pixel size at any zoom.
		gizmoOverlay = viewportClip->make("gizmoOverlay");
		gizmos = std::make_unique<GizmoLayer>(gizmoOverlay, mouse, history, theme);
		gizmos->onHandlePressed = [this]() { panning = false; };
		gizmos->onEdited = [this]() {
			if (inspectorPanel) {
				inspectorPanel->inspect(selectedNode);
			}
		};

		cursorArrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
		cursorResizeH = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
		cursorResizeV = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);

		panDownReceiver = mouse.onLeftMouseDown.connect("workbenchPan", [this](MV::TapDevice& a_mouse) {
			auto worldPoint = managers.renderer.worldFromScreen(a_mouse.position());
			if (dock->viewportAt(worldPoint)) {
				panning = true;
				panMoved = false;
				panStart = a_mouse.position();
				panLast = a_mouse.position();
			}
		});
		panMoveReceiver = mouse.onMove.connect("workbenchPanMove", [this](MV::TapDevice& a_mouse) {
			if (!panning) {
				return;
			}
			auto current = a_mouse.position();
			if (std::abs(current.x - panStart.x) + std::abs(current.y - panStart.y) > 4) {
				panMoved = true;
			}
			sceneRoot->translate(MV::point(static_cast<float>(current.x - panLast.x), static_cast<float>(current.y - panLast.y)));
			panLast = current;
		});
		panUpReceiver = mouse.onLeftMouseUp.connect("workbenchPanEnd", [this](MV::TapDevice& a_mouse) {
			if (panning && !panMoved) {
				pickSceneNode(a_mouse.position());
			}
			panning = false;
		});

		history.onAfterChange = [this]() {
			if (treePanel) {
				treePanel->refresh();
			}
			if (inspectorPanel) {
				inspectorPanel->inspect(treePanel ? treePanel->selected() : nullptr);
			}
		};

		dock = std::make_unique<DockSpace>(uiRoot, managers.services, theme, focusRouter);
		auto tree = std::make_shared<SceneTreePanel>(history,
			[this]() { return sceneRoot; },
			[this](const std::shared_ptr<MV::Scene::Node>& a_node) { select(a_node); });
		auto inspector = std::make_shared<InspectorPanel>(history);
		auto viewport = std::make_shared<ViewportPanel>();
		auto file = std::make_shared<ScriptPanel>("File", "Assets/Workbench/filePanel.jai");
		file->onLoadScene = [this](const std::string& a_fileName) { loadScene(a_fileName); };
		file->onSaveScene = [this](const std::string& a_fileName) { saveScene(a_fileName); };
		auto project = std::make_shared<ProjectPanel>(
			[this](const std::string& a_fileName) {
				loadScene(a_fileName);
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
		Panel* viewportPanel = dock->registerPanel(std::move(viewport));
		Panel* filePanel = dock->registerPanel(std::move(file));
		Panel* projectPanel = dock->registerPanel(std::move(project));

		dock->root(DockSpace::split(false, 0.18f,
			DockSpace::leaf({ treePanel, filePanel }),
			DockSpace::split(false, 0.72f,
				DockSpace::split(true, 0.70f,
					DockSpace::leaf({ viewportPanel }),
					DockSpace::leaf({ projectPanel })),
				DockSpace::leaf({ inspectorPanel }))));

		buildTitleBar();
	}

	App::~App() {
		SDL_SetCursor(SDL_GetDefaultCursor());
		for (auto&& cursor : { cursorArrow, cursorResizeH, cursorResizeV }) {
			if (cursor) {
				SDL_FreeCursor(cursor);
			}
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
			if (treePanel) {
				treePanel->selectNode(hit);
			} else {
				select(hit);
			}
		}
	}

	void App::dropPrefab(const std::string& a_fileName, const MV::Point<int>& a_screenPoint) {
		auto worldPoint = managers.renderer.worldFromScreen(a_screenPoint);
		if (!dock->viewportAt(worldPoint)) {
			return;
		}
		try {
			auto prefab = MV::Scene::Node::loadJai(a_fileName, managers.services, true);
			auto dropPosition = sceneRoot->localFromScreen(a_screenPoint);
			auto scene = sceneRoot;
			sceneRoot->add(prefab);
			prefab->position(dropPosition);
			history.record("place " + a_fileName,
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
			std::cerr << "Workbench BindSnap drop failed (" << a_fileName << "): " << a_error.what() << std::endl;
		}
	}

	void App::buildTitleBar() {
		auto& library = *managers.services.get<MV::TextLibrary>();
		auto bar = uiRoot->make("titleBar");
		auto sizer = bar->attach<MV::Scene::Sprite>();
		sizer->id("titleSizer");
		sizer->bounds(MV::size(1600.0f, theme.titleBarHeight));
		sizer->hide();
		auto titleBg = bar->attach<MV::Scene::Sprite>()->id("titleBg")->bounds(MV::size(1600.0f, theme.titleBarHeight))->color(theme.header());
		titleBg->anchors().anchor(MV::BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(1.0f, 1.0f))).parent(sizer.self());
		auto title = bar->attach<MV::Scene::Text>(library, theme.titleFont);
		title->bounds({ MV::point(10.0f, 0.0f), MV::size(400.0f, theme.titleBarHeight) });
		title->justification(MV::TextJustification::LEFT)->wrapping(MV::TextWrapMethod::NONE, 400.0f)->minimumLineHeight(theme.titleBarHeight)->text("Bindstone Workbench");

		// Scene-animation transport: pause/step/speed of the viewport's drawUpdate clock
		// (particle, spine, and shader-time preview).
		auto transport = bar->make("transport");
		transport->position(MV::point(theme.titleBarHeight * 8.0f, theme.pad * 0.5f));
		MV::Size<> transportSize(theme.titleButtonWidth * 1.4f, theme.titleBarHeight - theme.pad);
		auto pauseButton = makeButton(transport, managers.services, theme, "pause", transportSize, "||", nullptr);
		std::weak_ptr<MV::Scene::Button> weakPause = pauseButton;
		pauseButton->onAccept.connect("toggle", [this, weakPause](const std::shared_ptr<MV::Scene::Clickable>&) {
			scenePaused = !scenePaused;
			if (auto locked = weakPause.lock()) {
				locked->text(scenePaused ? ">" : "||");
			}
		});
		auto stepButton = makeButton(transport, managers.services, theme, "step", transportSize, ">|", [this]() {
			if (scenePaused) {
				sceneStepQueued = true;
			}
		});
		stepButton->owner()->position(MV::point(transportSize.width + 2.0f, 0.0f));
		auto speedButton = makeCycleButton(transport, managers.services, theme, "speed", transportSize,
			{ "1x", "2x", "4x", ".25x", ".5x" }, 0, [this](int a_index) {
				static constexpr float speeds[] = { 1.0f, 2.0f, 4.0f, 0.25f, 0.5f };
				sceneSpeed = speeds[a_index];
			});
		speedButton->owner()->position(MV::point((transportSize.width + 2.0f) * 2.0f, 0.0f));

		auto buttons = bar->make("buttons");
		MV::Size<> buttonSize(theme.titleButtonWidth, theme.titleBarHeight);
		auto minimizeButton = makeButton(buttons, managers.services, theme, "minimize", buttonSize, "_", [this]() {
			managers.renderer.window().minimize();
		});
		auto maximizeButton = makeButton(buttons, managers.services, theme, "maximize", buttonSize, "[]", [this]() {
			auto& window = managers.renderer.window();
			if (window.maximized()) {
				window.restore();
			} else {
				window.maximize();
			}
		});
		auto closeButton = makeButton(buttons, managers.services, theme, "close", buttonSize, "x", [this]() {
			done = true;
		});
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

	void App::select(const std::shared_ptr<MV::Scene::Node>& a_node) {
		selectedNode = a_node;
		gizmos->select(a_node);
		if (inspectorPanel) {
			inspectorPanel->inspect(a_node);
		}
	}

	void App::loadScene(const std::string& a_fileName) {
		try {
			auto newRoot = MV::Scene::Node::load(a_fileName, managers.services, false);
			history.clear();
			viewportClip->remove(sceneRoot, false);
			sceneRoot = newRoot;
			sceneRoot->id("root");
			viewportClip->add(sceneRoot);
			// Re-added roots get last+1 sortDepth; re-add the overlay so it stays above the scene.
			viewportClip->remove(gizmoOverlay, false);
			viewportClip->add(gizmoOverlay);
			sceneRoot->postLoadStep();
			if (treePanel) {
				treePanel->refresh();
			}
			select(nullptr);
			std::cout << "Workbench loaded: " << a_fileName << std::endl;
		} catch (const std::exception& a_error) {
			std::cerr << "Workbench load failed (" << a_fileName << "): " << a_error.what() << std::endl;
		}
	}

	void App::saveScene(const std::string& a_fileName) {
		try {
			sceneRoot->saveJai(a_fileName, managers.services, false);
			std::cout << "Workbench saved: " << a_fileName << std::endl;
		} catch (const std::exception& a_error) {
			std::cerr << "Workbench save failed (" << a_fileName << "): " << a_error.what() << std::endl;
		}
	}

	bool App::update(double a_dt) {
		lastDelta = a_dt;
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
		if (selectionFramePending && selectedNode) {
			auto view = dock->viewportRect();
			if (view.size.width > 0.0f && view.size.height > 0.0f) {
				auto nodeWorld = selectedNode->worldPosition();
				sceneRoot->translate(MV::point(
					view.position.x + view.size.width * 0.5f - nodeWorld.x,
					view.position.y + view.size.height * 0.5f - nodeWorld.y));
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

		managers.pool.run();
		return !done;
	}

	void App::syncViewportClip() {
		auto view = dock->viewportRect();
		if (view.size.width <= 0.0f || view.size.height <= 0.0f) {
			return;
		}
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
		if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) { delta.x += speed; }
		if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) { delta.x -= speed; }
		if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) { delta.y += speed; }
		if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) { delta.y -= speed; }
		if (delta.x != 0.0f || delta.y != 0.0f) {
			sceneRoot->translate(delta);
		}
	}

	void App::handleInput() {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (managers.renderer.handleEvent(event)) {
				continue;
			}
			if (event.type == SDL_QUIT) {
				done = true;
				continue;
			}
			if (focusRouter.handleEvent(event)) {
				continue;
			}
			if (event.type == SDL_KEYDOWN && (SDL_GetModState() & KMOD_CTRL)) {
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
			if (event.type == SDL_MOUSEWHEEL) {
				auto worldPoint = managers.renderer.worldFromScreen(mouse.position());
				if (dock->viewportAt(worldPoint)) {
					float zoom = 1.0f + 0.1f * static_cast<float>(event.wheel.y);
					auto current = sceneRoot->scale();
					float newScale = std::clamp(current.x * zoom, 0.05f, 20.0f);
					auto position = sceneRoot->position();
					// Keep the point under the cursor stationary while zooming.
					MV::Point<> local((worldPoint.x - position.x) / current.x, (worldPoint.y - position.y) / current.y);
					sceneRoot->position(MV::point(worldPoint.x - local.x * newScale, worldPoint.y - local.y * newScale, position.z));
					sceneRoot->scale(MV::Scale(newScale, newScale, current.z));
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
		uiRoot->drawUpdate(lastDelta);
		managers.renderer.updateScreen();
	}

	int App::run() {
		managers.timer.start();
		managers.timer.delta("workbenchTick");
		while (update(managers.timer.delta("workbenchTick"))) {
			handleInput();
			render();
			std::this_thread::yield();
		}
		return 0;
	}

	int App::runFrames(int a_frameCount, const std::string& a_screenshotPath) {
		managers.timer.start();
		managers.timer.delta("workbenchTick");
		for (int frame = 0; frame < a_frameCount && update(managers.timer.delta("workbenchTick")); ++frame) {
			handleInput();
			if (!a_screenshotPath.empty() && frame == a_frameCount - 1) {
				managers.renderer.clearScreen();
				visor->drawUpdate(lastDelta);
				uiRoot->drawUpdate(lastDelta);
				captureScreenshot(a_screenshotPath);
				managers.renderer.updateScreen();
			} else {
				render();
			}
		}
		std::cout << "Workbench smoke completed (" << a_frameCount << " frames)" << std::endl;
		return 0;
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
