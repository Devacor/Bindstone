#include "clickerGame.h"
#include "Editor/editorFactories.h"

void sdl_quit_2(void) {
	SDL_Quit();
	TTF_Quit();
}

ClickerGame::ClickerGame(MV::ThreadPool* a_pool, MV::Draw2D* a_renderer) :
	pool(a_pool),
	renderer(a_renderer),
	textLibrary(*a_renderer),
	done(false) {

	initializeWindow();

}

void ClickerGame::initializeWindow() {
	MV::initializeFilesystem();
	srand(static_cast<unsigned int>(time(0)));
	//RENDERER SETUP:::::::::::::::::::::::::::::::::
	MV::Size<> worldSize(375, 667);
	MV::Size<int> windowSize(375, 667);

	renderer->window().windowedMode().allowUserResize(false).resizeWorldWithWindow(true);

	if (!renderer->initialize(windowSize, worldSize)) {
		exit(0);
	}
	atexit(sdl_quit_2);

	MV::AudioPlayer::instance()->initAudio();
	mouse.update();

	std::ifstream stream("clicker.scene");

	cereal::JSONInputArchive archive(stream);

	archive.add(
		cereal::make_nvp("mouse", &mouse),
		cereal::make_nvp("renderer", renderer),
		cereal::make_nvp("textLibrary", &textLibrary),
		cereal::make_nvp("pool", pool)
		);

	archive(cereal::make_nvp("scene", worldScene));

	textures.assemblePacks("Assets/Atlases", renderer);
	textures.files("Assets/Map");

	MV::FontDefinition::make(textLibrary, "default", "Assets/Fonts/Verdana.ttf", 14);
	MV::FontDefinition::make(textLibrary, "small", "Assets/Fonts/Verdana.ttf", 9);
	MV::FontDefinition::make(textLibrary, "big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);

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
		goldTextComponent->text(MV::to_string(newValue));
	});
}

bool ClickerGame::update(double dt) {
	lastUpdateDelta = dt;
	pool->run();
	if (done) {
		done = false;
		return false;
	}
	return true;
}

void ClickerGame::handleInput() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (!renderer->handleEvent(event)) {
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
	renderer->clearScreen();
	worldScene->drawUpdate(static_cast<float>(lastUpdateDelta));
	//testShape->draw();
	renderer->updateScreen();
}