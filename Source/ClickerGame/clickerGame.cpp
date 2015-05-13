#include "clickerGame.h"
#include "Editor/editorFactories.h"

void sdl_quit_2(void) {
	SDL_Quit();
	TTF_Quit();
}

ClickerGame::ClickerGame() :
	textLibrary(renderer),
	done(false) {

	initializeWindow();

}

void ClickerGame::initializeWindow() {
	MV::initializeFilesystem();
	srand(static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Size<> worldSize(375, 667);
	MV::Size<int> windowSize(375, 667);

	renderer.window().windowedMode().allowUserResize(false).resizeWorldWithWindow(true);

	if (!renderer.initialize(windowSize, worldSize)) {
		exit(0);
	}
	renderer.loadShader(MV::DEFAULT_ID, "Assets/Shaders/default.vert", "Assets/Shaders/default.frag");
	renderer.loadShader(MV::PREMULTIPLY_ID, "Assets/Shaders/default.vert", "Assets/Shaders/premultiply.frag");
	renderer.loadShader(MV::COLOR_PICKER_ID, "Assets/Shaders/default.vert", "Assets/Shaders/colorPicker.frag");
	atexit(sdl_quit_2);

	AudioPlayer::instance()->initAudio();
	mouse.update();

	std::ifstream stream("clicker.scene");

	cereal::JSONInputArchive archive(stream);

	archive.add(
		cereal::make_nvp("mouse", &mouse),
		cereal::make_nvp("renderer", &renderer),
		cereal::make_nvp("textLibrary", &textLibrary),
		cereal::make_nvp("pool", &pool)
		);

	archive(cereal::make_nvp("scene", worldScene));

	textLibrary.loadFont("default", "Assets/Fonts/Verdana.ttf", 14);
	textLibrary.loadFont("small", "Assets/Fonts/Verdana.ttf", 9);
	textLibrary.loadFont("big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);

	textures.assemblePacks("Assets/Atlases", &renderer);
	textures.files("Assets/Map");


	InitializeWorldScene();
}
void ClickerGame::InitializeWorldScene() {
	auto clickDamageEffect = worldScene->get("DamageOn")->component<MV::Scene::Emitter>();
	clickDamageEffect->disable();

	auto enemyButton = worldScene->get("Enemy")->attach<MV::Scene::Clickable>(mouse)->clickDetectionType(MV::Scene::Clickable::BoundsType::NODE);
	enemyButton->onAccept.connect("ClickEnemy", [&, clickDamageEffect](std::shared_ptr<MV::Scene::Clickable>) {
		player.click();
		clickDamageEffect->enable();
		auto damageDisable = worldScene->task().get("DamageDisable", false);
		if (damageDisable) {
			damageDisable->cancel();
		}
		double timeToDisable = 1.0;
		worldScene->task().also("DamageDisable", [=](const MV::Task&, double a_dt) mutable {
			timeToDisable -= a_dt;
			if (timeToDisable <= 0.0) {
				clickDamageEffect->disable();
				return true;
			}
			return false;
		});
	});

	auto currencyBarNode = worldScene->get("CurrencyBar");

	auto stubbedGoldComponent = currencyBarNode->component<MV::Scene::Sprite>();
	auto goldTextSize = stubbedGoldComponent->bounds().size();
	currencyBarNode->detach(stubbedGoldComponent);

	auto goldTextComponent = makeLabel(currencyBarNode, textLibrary, "CurrencyLabel", goldTextSize, UTF_CHAR_STR("0"));

	player.onGoldChange.connect("UpdateGoldValue", [goldTextComponent](uint64_t newValue) {
		goldTextComponent->text(std::to_wstring(newValue));
	});
}

bool ClickerGame::update(double dt) {
	lastUpdateDelta = dt;
	if (done) {
		done = false;
		return false;
	}
	return true;
}

void ClickerGame::handleInput() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (!renderer.handleEvent(event)) {
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
					//testBox->translateScrollPosition(MV::Point<>(0, -2));
					break;
				case SDLK_LEFT:
					//testBox->translateScrollPosition(MV::Point<>(-2, 0));
					break;
				case SDLK_DOWN:
					//testBox->translateScrollPosition(MV::Point<>(0, 2));
					//renderer.window().windowedMode().bordered();
					break;
				case SDLK_SPACE:
					//renderer.window().allowUserResize();
					break;
				case SDLK_RIGHT:
					//testBox->translateScrollPosition(MV::Point<>(2, 0));
					break;
				}
				break;
			}
		}
	}
	mouse.update();
}

void ClickerGame::render() {
	renderer.clearScreen();
	worldScene->drawUpdate(static_cast<float>(lastUpdateDelta));
	//testShape->draw();
	renderer.updateScreen();
}
