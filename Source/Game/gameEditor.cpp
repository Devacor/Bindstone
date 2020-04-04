#include "gameEditor.h"
#include "Editor/editorDefines.h"
#include "Editor/editorFactories.h"


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

GameEditor::GameEditor(std::string a_username, std::string a_password) :
	game(managers, a_username, a_password),
	autoStartGame(!a_username.empty() && !a_password.empty()),
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
	// 		auto populateArchive = [&](cereal::JSONInputArchive& archive) {
	// 			archive.add(cereal::make_nvp("renderer", &managers.renderer));
	// 			archive.add(cereal::make_nvp("pool", &managers.pool));
	// 		};
	// 		spineTestNode->loadChild("simple.scene", populateArchive);
	// 		spineTestNode->loadChild("tree_particle.scene", populateArchive);
	screenScaler = limbo->attach<MV::Scene::Sprite>();
	screenScaler->hide();

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


	if (autoStartGame) {
		gameButton->press();
	}
	
// 	auto sendButton = makeButton(grid, game.managers().textLibrary, mouse, "Send", { 100.0f, 20.0f }, UTF_CHAR_STR("Send"));
// 	sendButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
// 		if (client) {
// 			client->send(MV::toBinaryStringCast<ServerAction>(std::make_shared<CreatePlayer>("maxmike@gmail.com", "M2tM", "SuperTinker123")));
// 		}
// 	});

	grid->component<MV::Scene::Grid>()->anchors().anchor({ MV::point(0.5f, 0.5f), MV::point(0.5f, 0.5f) }).parent(screenScaler.self(), true);
}
