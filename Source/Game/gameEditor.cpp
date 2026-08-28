#include "gameEditor.h"
#include "Editor/editorDefines.h"
#include "Editor/editorFactories.h"
#include <cstring>
#include <iostream>
#include <vector>


void GameEditor::resumeTitleMusic() {
	//auto playlistGame = std::make_shared<MV::AudioPlayList>();
	//playlistGame->addSoundBack("title");
	//playlistGame->loopSounds(false);

	//game.managers().audio.setMusicPlayList(playlistGame);

	//playlistGame->beginPlaying();
}

void GameEditor::handleInput(){
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (!game.managers().renderer.handleEvent(event)) {
			switch (event.type) {
			case SDL_QUIT:
				done = true;
				break;
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
					done = true;
					break;
				case SDLK_UP:

					break;
				case SDLK_LEFT:
					break;
				case SDLK_DOWN:

					break;
				case SDLK_SPACE:

					break;
				case SDLK_RIGHT:
					break;
				}
				break;
			case SDL_WINDOWEVENT:
				break;
			case SDL_MOUSEWHEEL:
				break;
			}
			mouse.updateTouch(event, managers.renderer.window().drawableSize());
		} else {
			auto scale = game.managers().renderer.window().uiScale();
			screenScaler->bounds({ MV::point(0.0f, 0.0f), game.managers().renderer.world().size() / scale });
			limbo->scale(scale);
		}
	}
	mouse.update();
}

GameEditor::GameEditor(std::string a_username, std::string a_password, bool a_autoStart) :
	managers({ a_username, a_password, a_autoStart }),
	game(managers),
	autoStartGame(a_autoStart && !a_username.empty() && !a_password.empty()),
	editor(managers),
	limbo(MV::Scene::Node::make(managers.renderer))
{
	//managers.audio.loadMusic("Audio/TitleTheme.ogg", "title");
	//managers.audio.loadMusic("Audio/FieldIntro.ogg", "gameintro");
	//managers.audio.loadMusic("Audio/FieldLoopNeutral.ogg", "gameloop");

	resumeTitleMusic();

	// 		auto stencilNode = limbo->make("StencilTestNode");
	// 		auto spineTestNode = stencilNode->make("SpineTest")->position({ 400.0f, 600.0f })->attach<MV::Scene::Spine>(MV::Scene::Spine::FileBundle("Spine/Tree/life.json", "Spine/Tree/life.atlas", 0.5f))->shader(MV::DEFAULT_ID)->animate("idle")->bindNode("effects", "tree_particle")->bindNode("effects", "simple")->owner();
	// 
	// 		stencilNode->attach<MV::Scene::Stencil>()->bounds({ MV::Point<>(), MV::Size<>(450.0f, 550.0f) });

	// 		spineTestNode->make("PaletteTest")->position({ -50.0f, -100.0f })->
	// 			attach<MV::Scene::Palette>(mouse)->bounds(MV::size(256.0f, 256.0f));
	screenScaler = limbo->attach<MV::Scene::Sprite>();
	screenScaler->hide()->id("ScreenScaler");

	auto scale = game.managers().renderer.window().uiScale();
	screenScaler->bounds({ MV::point(0.0f, 0.0f), game.managers().renderer.world().size() / scale });
	limbo->scale(scale);

	auto child = limbo->make("child");
	auto screenBounds = screenScaler->worldBounds();

	// 	auto alignedSprite = limbo->make("repositionNode")->attach<MV::Scene::Sprite>();
	// 	alignedSprite->texture(textureSheet->makeHandle());
	// 	alignedSprite->anchors().anchor({ MV::point(0.5f, 0.5f), MV::point(0.5f, 0.5f) }).usePosition(true).parent(screenScaler.self());

	// 	mouse.onLeftMouseDownEnd.connect("TEST", [&](MV::TapDevice& a_mouse) {
	// 		std::cout << "PRE: " << screenScaler->bounds() << std::endl;
	// 		screenScaler->bounds({ screenScaler->owner()->localFromScreen(a_mouse.position()), screenScaler->bounds().maxPoint });
	// 		std::cout << "POST: " << screenScaler->bounds() << std::endl;
	// 	});
	// 	mouse.onRightMouseDownEnd.connect("TEST", [&](MV::TapDevice& a_mouse) {
	// 		std::cout << "PRE: " << screenScaler->bounds() << std::endl;
	// 		screenScaler->bounds({ screenScaler->bounds().minPoint, screenScaler->owner()->localFromScreen(a_mouse.position()) });
	// 		std::cout << "POST: " << screenScaler->bounds() << std::endl;
	// 	});

	MV::Point worldCenter{ screenScaler->worldBounds().width() / 2.0f, screenScaler->worldBounds().height() / 2.0f };
	MV::Point localCenter{ screenScaler->bounds().width() / 2.0f, screenScaler->bounds().height() / 2.0f };
	MV::Point halfSize{ screenScaler->worldBounds().width() / 2.0f - 10.0f, screenScaler->worldBounds().height() / 2.0f - 10.0f };

	auto grid = limbo->make("Grid")->position({ screenScaler->bounds().width() / 2.0f, screenScaler->bounds().height() / 2.0f })->
		attach<MV::Scene::Grid>()->columns(1)->padding({ 2.0f, 2.0f })->margin({ 4.0f, 4.0f })->color({ BOX_BACKGROUND })->owner();

	auto editorButton = makeButton(grid, game.managers().textLibrary, mouse, "Editor", { 100.0f, 20.0f }, U8_STR("Editor"));
	editorButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
		runEditor();
		resumeTitleMusic();
		});
	auto gameButton = makeButton(grid, game.managers().textLibrary, mouse, "Game", { 100.0f, 20.0f }, U8_STR("Game"));
	gameButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
		runGame();
		resumeTitleMusic();
		});
	auto quitButton = makeButton(grid, game.managers().textLibrary, mouse, "Quit", { 100.0f, 20.0f }, U8_STR("Quit"));
	quitButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
		done = true;
		});

	grid->position(grid->position() - MV::toPoint(grid->bounds().size()) / 2.0f);
	
	auto scrollerWindow = limbo->make("ScrollerWindow")->position({ 100.0f, 100.0f })->
		attach<MV::Scene::Sprite>()->color(MV::Color(128, 128, 128, 128))->bounds({ MV::Size{400.0f, 400.0f} })->owner();
	auto childWindow = scrollerWindow->make("ChildView")->position({ 0.0f, 20.0f })->
		attach<MV::Scene::Sprite>()->color(MV::Color(128, 128, 128, 128))->bounds({ MV::Size{400.0f, 380.0f} })->owner();

	auto moveState = std::make_shared<bool>(true);
	auto toggleBut = makeButton(limbo, game.managers().textLibrary, mouse, "Quit", MV::Size(200.0f, 20.0f), "Toggle");
	toggleBut->owner()->position({ 15.0f, 15.0f });
	toggleBut->onAccept.connect("Toggle", [=](const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
		*moveState = !*moveState;
		});

	scrollerWindow->rotation({ 0.0f, 0.0f, 45.0f });
	scrollerWindow->attach<MV::Scene::Clickable>(mouse)->color(MV::Color(32, 23, 32, 128))->show()->bounds({ MV::Size{400.0f, 20.0f} })->
		onDrag.connect("Drag", [moveState,childWindow,weakScrollerWindow=std::weak_ptr<MV::Scene::Node>(scrollerWindow)](const std::shared_ptr<MV::Scene::Clickable> &a_handle, const MV::Point<int>& startPosition, const MV::Point<int>& deltaPosition) {
			if (auto scrollerWindow = weakScrollerWindow.lock()) {
				if (*moveState) {
					scrollerWindow->translate(scrollerWindow->parent()->localFromScreen(deltaPosition) - scrollerWindow->parent()->localFromScreen(MV::Point<int>()));
				} else {
					childWindow->translate(childWindow->parent()->localFromScreen(deltaPosition) - childWindow->parent()->localFromScreen(MV::Point<int>()));
				}
			}
		});

// 	auto sendButton = makeButton(grid, game.managers().textLibrary, mouse, "Send", { 100.0f, 20.0f }, UTF_CHAR_STR("Send"));
// 	sendButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
// 		if (client) {
// 			client->send(MV::toBinaryStringCast<ServerAction>(std::make_shared<CreatePlayer>("maxmike@gmail.com", "M2tM", "SuperTinker123")));
// 		}
// 	});

	grid->component<MV::Scene::Grid>()->anchors().anchor({ MV::point(0.5f, 0.5f), MV::point(0.5f, 0.5f) }).parent(screenScaler.self(), true);
}

int GameEditor::runGameFrames(int a_frameLimit, const std::string& a_capturePath) {
	game.returnFromBackground();
	managers.timer.start();
	managers.timer.delta("tick");
	int frame = 0;
	for (; (a_frameLimit < 0 || frame < a_frameLimit) && game.update(managers.timer.delta("tick")); ++frame) {
		game.handleInput();
		if (!a_capturePath.empty() && frame == a_frameLimit - 1) {
			managers.renderer.clearScreen();
			if (game.instance()) {
				game.instance()->scene()->draw();
			}
			game.root()->draw();
			captureScreenshot(a_capturePath);
			managers.renderer.updateScreen();
		} else {
			game.render();
		}
		std::this_thread::yield();
	}
	return frame;
}

void GameEditor::dumpSlicedSprites() {
	for (auto&& entry : game.gui().root()->componentsInChildren<MV::Scene::Sprite>(false, true)) {
		auto sprite = std::get<0>(entry).self();
		if (!sprite || sprite->pointSize() != 16 || !sprite->texture()) { continue; }
		auto handle = sprite->texture();
		auto percent = handle->rawPercent();
		auto slice = handle->slice();
		auto logical = handle->logicalSize();
		std::cout << "[slice] " << sprite->owner()->id() << "." << sprite->id() << " handle=" << handle->name()
			<< " percent=(" << percent.minPoint.x << "," << percent.minPoint.y << ")-(" << percent.maxPoint.x << "," << percent.maxPoint.y << ")"
			<< " slice=(" << slice.minPoint.x << "," << slice.minPoint.y << ")-(" << slice.maxPoint.x << "," << slice.maxPoint.y << ")"
			<< " logical=" << logical.width << "x" << logical.height << " texture=" << handle->texture()->size().width << "x" << handle->texture()->size().height
			<< " visible=" << sprite->owner()->visible() << std::endl;
		for (size_t i = 0; i < 16; ++i) {
			auto& p = sprite->point(i);
			std::cout << "    p" << i << "=(" << p.x << "," << p.y << ") uv=(" << p.textureX << "," << p.textureY << ")" << std::endl;
		}
	}
}

int GameEditor::runFrames(int a_frameCount, const std::string& a_capturePath) {
	int frames = runGameFrames(a_frameCount, a_capturePath);
	dumpSlicedSprites();
	std::cout << "Game frames completed (" << frames << "/" << a_frameCount << ")" << std::endl;
	return frames == a_frameCount ? 0 : 1;
}

void GameEditor::captureScreenshot(const std::string& a_path) {
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
	std::cout << "Game screenshot: " << a_path << std::endl;
}
