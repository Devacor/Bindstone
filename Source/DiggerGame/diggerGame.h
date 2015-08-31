#ifndef __DIGGER_GAME_H__
#define __DIGGER_GAME_H__
#include <SDL.h>
#include "Utility/package.h"
#include "Render/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
#include "Network/package.h"
#include "Interface/package.h"
#include "Physics/package.h"
#include <string>
#include <ctime>
#include <stdint.h>
#include <memory>

class DiggerPlayer {
private:
	MV::Signal<void(uint64_t)> onGoldChangeSignal;

public:
	MV::SignalRegister<void(uint64_t)> onGoldChange;
	DiggerPlayer():
		onGoldChange(onGoldChangeSignal){
	}

	void gold(uint64_t a_newGold) {
		goldHeld = a_newGold;
		onGoldChangeSignal(goldHeld);
	}

	uint64_t gold() const {
		return goldHeld;
	}
private:
	uint64_t goldHeld = 0;
};

class GroundTileSet {
public:
	GroundTileSet(MV::SharedTextures &a_sharedTextures) {

		texture = a_sharedTextures.file("Assets/Pixel/Levels/ground.png", false, true);
		std::vector<MV::Point<int>> topGrassPositions{ { 0, 0 },{ 1, 0 },{ 0, 2 },{ 1, 2 },{ 2, 2 },{ 3, 2 } };
		for (auto&& grassCoordinate : topGrassPositions) {
			grassTop.push_back(texture->makeHandle({ grassCoordinate * MV::Point<int>(32, 32), MV::Size<int>{32, 32} }));
		}
		std::vector<MV::Point<int>> standardPositions{ {0, 1}, {1, 1}, {0, 3}, {1, 3} };
		for (auto&& standardCoordinate : standardPositions) {
			standard.push_back(texture->makeHandle({ standardCoordinate * MV::Point<int>(32, 32), MV::Size<int>{32, 32} }));
		}
		skyHandle = texture->makeHandle({ MV::Point<int>{3, 0} * MV::Point<int>(32, 32), MV::Size<int>{32, 32} });

		std::vector<MV::Point<int>> breakablePositions{ { 2, 3 },{ 3, 3 } };
		for (auto&& breakableCoordinate : breakablePositions) {
			breakable.push_back(texture->makeHandle({ breakableCoordinate * MV::Point<int>(32, 32), MV::Size<int>{32, 32} }));
		}
	}

	std::shared_ptr<MV::TextureHandle> randomBreakable() const {
		return breakable[MV::randomInteger(0, breakable.size() - 1)];
	}

	std::shared_ptr<MV::TextureHandle> randomStandard() const {
		return standard[MV::randomInteger(0, standard.size() - 1)];
	}
	std::shared_ptr<MV::TextureHandle> randomGrass() const {
		return grassTop[MV::randomInteger(0, grassTop.size() - 1)];
	}

	std::shared_ptr<MV::TextureHandle> sky() const {
		return skyHandle;
	}
private:
	std::shared_ptr<MV::FileTextureDefinition> texture;

	std::shared_ptr<MV::TextureHandle> skyHandle;
	std::vector<std::shared_ptr<MV::TextureHandle>> standard;
	std::vector<std::shared_ptr<MV::TextureHandle>> breakable;
	std::vector<std::shared_ptr<MV::TextureHandle>> grassTop;
};

class DiggerTileSet {
public:
	DiggerTileSet(const std::string &a_levelName, MV::SharedTextures &a_sharedTextures) :
		sharedTextures(a_sharedTextures){

		texture = sharedTextures.file("Assets/Pixel/Levels/" + a_levelName + ".png", false, true);
		for (int i = 0; i < 6; ++i) {
			standard.push_back(texture->makeHandle({ {i * 32, 0}, MV::Size<int>{32, 32} }));
		}
	}

	std::shared_ptr<MV::TextureHandle> randomStandard() const {
		return standard[MV::randomInteger(0, standard.size() - 1)];
	}
private:
	std::vector<std::shared_ptr<MV::TextureHandle>> standard;
	std::vector<std::shared_ptr<MV::TextureHandle>> corners;

	std::shared_ptr<MV::FileTextureDefinition> texture;

	MV::SharedTextures &sharedTextures;
};

class DiggerWorld {
public:
	MV::Scene::SafeComponent<MV::Scene::Collider> thing;

	DiggerWorld(const std::shared_ptr<MV::Scene::Node> &a_node, MV::SharedTextures &a_sharedTextures) :
		background(a_node->make("Background")->attach<MV::Scene::Grid>()),
		environment(a_node->make("Environment")->attach<MV::Scene::Grid>()),
		foreground(a_node->make("Foreground")->attach<MV::Scene::Grid>()),
		sharedTextures(a_sharedTextures){

		loadTextures();

		auto physicsWorld = a_node->make("world")->attach<MV::Scene::Environment>();
		physicsWorld->owner()->make("ground")->position({ 100.0f, 300.0f })->attach<MV::Scene::Sprite>()->size({ 100.0f, 100.0f }, true)->color({1.0f, 1.0f, 1.0f, .5f})->owner()->
			attach<MV::Scene::Collider>(physicsWorld, MV::Scene::CollisionBodyAttributes().makeStatic())->attach({100.0f, 100.0f});
		thing = physicsWorld->owner()->make("thing")->position({ 100.0f, 0.0f })->attach<MV::Scene::Sprite>()->size({ 50.0f, 50.0f }, true)->color({ 0.0f, 0.0f, 1.0f, .5f })->owner()->
			attach<MV::Scene::Collider>(physicsWorld, MV::Scene::CollisionBodyAttributes().makeDynamic());
		thing->attach({ 50.0f, 50.0f });

		const int worldWidth = 20;
		background->columns(worldWidth)->hide();
		environment->columns(worldWidth)->hide();
		foreground->columns(worldWidth)->hide();

		environment->owner()->attach<MV::Scene::Environment>();
		auto player = environment->owner()->make("Player")->position({ 0.0f, 0.0f });
		player->attach<MV::Scene::Sprite>()->bounds({ {0, 0}, tileSize })->texture(ground->randomBreakable());
		player->attach<MV::Scene::Collider>()->attach(tileSize, (toPoint(tileSize) / 2.0f) * -1.0f);

		for (int i = 0; i < worldWidth * 3; ++i) {
			pushTile(false, nullptr, ground->sky());
		}
		std::vector<int> breakableCoordinates{worldWidth / 2 - 1, worldWidth / 2, worldWidth / 2 + 1};

		for (int i = 0; i < worldWidth; ++i) {
			pushTile(false, nullptr, ground->sky(), ground->randomGrass());
		}

		for (int i = 0; i < worldWidth; ++i) {
			pushTile(true, std::find(breakableCoordinates.begin(), breakableCoordinates.end(), i) != breakableCoordinates.end() ? ground->randomBreakable() : ground->randomStandard(), nullptr);
		}

		for (auto&& tileSet : tileSets) {
			for (int i = 0; i < worldWidth * 10; ++i) {
				pushTile(true, tileSet.randomStandard(), nullptr);
			}
		}
	}

	void pushTile(bool a_collidable, const std::shared_ptr<MV::TextureHandle> &a_environment, const std::shared_ptr<MV::TextureHandle> &a_background, const std::shared_ptr<MV::TextureHandle> &a_foreground = nullptr) {
		std::shared_ptr<MV::Scene::Node> worldTile;
		if (a_environment) {
			worldTile = environment->owner()->make()->attach<MV::Scene::Sprite>()->bounds({ { 0, 0 }, tileSize })->texture(a_environment)->owner();
		} else {
			worldTile = environment->owner()->make()->attach<MV::Scene::Sprite>()->bounds({ { 0, 0 }, tileSize })->color({ 0, 0, 0, 0 })->owner();
		}
		if (a_background) {
			background->owner()->make()->attach<MV::Scene::Sprite>()->bounds({ { 0, 0 }, tileSize })->texture(a_background);
		} else {
			background->owner()->make()->attach<MV::Scene::Sprite>()->bounds({ { 0, 0 }, tileSize })->color({ 0, 0, 0, 0 });
		}
		if (a_foreground) {
			foreground->owner()->make()->attach<MV::Scene::Sprite>()->bounds({ { 0, 0 }, tileSize })->texture(a_foreground);
		} else {
			foreground->owner()->make()->attach<MV::Scene::Sprite>()->bounds({ { 0, 0 }, tileSize })->color({ 0, 0, 0, 0 });
		}

		if (a_collidable) {
			worldTile->attach<MV::Scene::Collider>(MV::Scene::CollisionBodyAttributes().makeStatic())->attach(tileSize, (toPoint(tileSize) / 2.0f) * -1.0f);
		}
	}


private:
	void loadTextures() {
		tileSets.emplace_back("dirt", sharedTextures);
		ground = std::make_shared<GroundTileSet>(sharedTextures);
	}

	std::shared_ptr<GroundTileSet> ground;
	std::vector<DiggerTileSet> tileSets;

	MV::SharedTextures &sharedTextures;

	MV::Scene::SafeComponent<MV::Scene::Grid> background;
	MV::Scene::SafeComponent<MV::Scene::Grid> environment;
	MV::Scene::SafeComponent<MV::Scene::Grid> foreground;

	const MV::Size<> tileSize { 32, 32 };
};

class DiggerGame {
public:
	DiggerGame(MV::ThreadPool* pool, MV::Draw2D* renderer);

	//return true if we're still good to go
	bool update(double dt);
	void handleInput();
	void render();

	MV::ThreadPool* getPool() {
		return pool;
	}

	MV::Draw2D* getRenderer() {
		return renderer;
	}

	MV::TextLibrary* getTextLibrary() {
		return &textLibrary;
	}

private:
	DiggerGame(const DiggerGame &) = delete;
	DiggerGame& operator=(const DiggerGame &) = delete;

	void InitializeWorldScene();

	void initializeWindow();

	MV::ThreadPool* pool;
	MV::Draw2D* renderer;
	MV::TextLibrary textLibrary;

	MV::SharedTextures textures;

	std::shared_ptr<MV::Scene::Node> worldScene;

	bool done;
	MV::MouseState mouse;

	double lastUpdateDelta;

	DiggerPlayer player;
	std::shared_ptr<DiggerWorld> world;
};

void sdl_quit_3(void);

#endif