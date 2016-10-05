#ifndef __DIGGER_GAME_H__
#define __DIGGER_GAME_H__
#include <SDL.h>
#include "Utility/package.h"
#include "Render/package.h"
#include "Audio/package.h"
#include "Network/package.h"
#include "Interface/package.h"
#include "Physics/package.h"
#include <string>
#include <ctime>
#include <stdint.h>
#include <memory>
#include "Game/managers.h"

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

	DiggerWorld(const std::shared_ptr<MV::Scene::Node> &a_node, MV::SharedTextures &a_sharedTextures, MV::MouseState &a_mouse) :
		background(a_node->make("Background")->attach<MV::Scene::Grid>()),
		environment(a_node->make("Environment")->attach<MV::Scene::Grid>()),
		foreground(a_node->make("Foreground")->attach<MV::Scene::Grid>()),
		physicsWorld(a_node->make("Physics")->attach<MV::Scene::Environment>()),
		mouse(a_mouse),
		sharedTextures(a_sharedTextures){

		environment->layoutPolicy(MV::Scene::Grid::AutoLayoutPolicy::None);
		foreground->layoutPolicy(MV::Scene::Grid::AutoLayoutPolicy::None);
		background->layoutPolicy(MV::Scene::Grid::AutoLayoutPolicy::None);

		loadTextures();

		const int worldWidth = 20;
		background->columns(worldWidth)->hide();
		environment->columns(worldWidth)->hide();
		foreground->columns(worldWidth)->hide();

		for (int i = 0; i < worldWidth * 3; ++i) {
			pushTile(false, nullptr, ground->sky());
		}

		std::vector<int> breakableCoordinates{ worldWidth / 2 - 1, worldWidth / 2, worldWidth / 2 + 1 };
		for (int i = 0; i < worldWidth; ++i) {
			pushTile(false, nullptr, ground->sky(), std::find(breakableCoordinates.begin(), breakableCoordinates.end(), i) != breakableCoordinates.end() ? nullptr : ground->randomGrass());
		}

		for (int i = 0; i < worldWidth; ++i) {
			pushTile(true, std::find(breakableCoordinates.begin(), breakableCoordinates.end(), i) != breakableCoordinates.end() ? ground->randomBreakable() : ground->randomStandard(), nullptr);
		}

		for (auto&& tileSet : tileSets) {
			for (int i = 0; i < worldWidth * 10; ++i) {
				pushTile(true, tileSet.randomStandard(), nullptr);
			}
		}
		environment->layoutCells();
		background->layoutCells();
		foreground->layoutCells();

		thing = physicsWorld->owner()->make("thing")->position({ 100.0f, 5.0f })->attach<MV::Scene::Sprite>()->size({ 20.0f, 20.0f }, true)->color({ 0.0f, 0.0f, 1.0f, .9f })->owner()->
			attach<MV::Scene::Collider>(physicsWorld, MV::Scene::CollisionBodyAttributes().makeDynamic().angularDamping(55.0f));
		physicsWorld->owner()->make("thing1")->position({ 100.0f, -50.0f })->attach<MV::Scene::Sprite>()->size({ 10.0f, 10.0f }, true)->color({ 0.0f, 1.0f, 1.0f, .5f })->owner()->
			attach<MV::Scene::Collider>(physicsWorld, MV::Scene::CollisionBodyAttributes().makeDynamic())->attach({ 10.0f, 10.0f })->id("thing1");
		physicsWorld->owner()->make("thing2")->position({ 150.0f, 30.0f })->attach<MV::Scene::Sprite>()->size({ 10.0f, 10.0f }, true)->color({ 0.0f, 1.0f, 1.0f, .5f })->owner()->
			attach<MV::Scene::Collider>(physicsWorld, MV::Scene::CollisionBodyAttributes().makeDynamic())->attach({ 10.0f, 10.0f })->id("thing2");
		physicsWorld->owner()->make("thing3")->position({ 140.0f, 10.0f })->attach<MV::Scene::Sprite>()->size({ 10.0f, 10.0f }, true)->color({ 0.0f, 1.0f, 1.0f, .5f })->owner()->
			attach<MV::Scene::Collider>(physicsWorld, MV::Scene::CollisionBodyAttributes().makeDynamic())->attach({ 10.0f, 10.0f })->id("thing3");
		
		thing->attach(20.0f, MV::Point<>(), MV::Scene::CollisionPartAttributes().id("foot").friction(15.0f).restitution(0));

		auto topBody = physicsWorld->owner()->make("thingBody")->position({ 100.0f, -12.0f })->attach<MV::Scene::Sprite>()->size({ 25.0f, 38.0f }, true)->color({ 0.0f, 1.0f, 0.0f, .6f })->owner()->
			attach<MV::Scene::Collider>(physicsWorld, MV::Scene::CollisionBodyAttributes().makeDynamic().disableRotation());
		topBody->attach({ { -12.2f, -19.0f }, { 12.2f, -19.0f}, {12.6f, 18.0f}, {4.0f, 20.0f}, {-4.0f, 20.0f}, {-12.6f, 18.0f} }, MV::Point<>(), MV::Scene::CollisionPartAttributes().friction(0.0f).restitution(0));

		motor = thing->rotationJoint(topBody.self(), MV::Scene::RotationJointAttributes().torque(10000.0f));

		//thing->ignorePhysicsAngle();
	}

	void pushTile(bool a_collidable, const std::shared_ptr<MV::TextureHandle> &a_environment, const std::shared_ptr<MV::TextureHandle> &a_background, const std::shared_ptr<MV::TextureHandle> &a_foreground = nullptr) {
		static int tileCounter = 0;
		std::shared_ptr<MV::Scene::Node> worldTile;
		if (a_environment) {
			worldTile = environment->owner()->make()->attach<MV::Scene::Clickable>(mouse)->bounds({ { 0, 0 }, tileSize })->show()->texture(a_environment)->owner();
		} else {
			worldTile = environment->owner()->make()->attach<MV::Scene::Clickable>(mouse)->bounds({ { 0, 0 }, tileSize })->show()->color({ 0, 0, 0, 0 })->owner();
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
			worldTile->attach<MV::Scene::Collider>(physicsWorld, MV::Scene::CollisionBodyAttributes().makeStatic())->id("T:"+std::to_string(tileCounter))->attach(tileSize, (toPoint(tileSize) / 2.0f))->id("T:"+std::to_string(tileCounter));
			worldTile->component<MV::Scene::Clickable>()->onAccept.connect("clicky", [](std::shared_ptr<MV::Scene::Clickable> a_self) {
				a_self->color({ 0, 0, 0, 0 })->clearTexture();
				a_self->owner()->detach<MV::Scene::Collider>(true, false);
			});
			tileCounter++;
		}
	}


private:
	void loadTextures() {
		tileSets.emplace_back("dirt", sharedTextures);
		ground = std::make_shared<GroundTileSet>(sharedTextures);
	}

	std::shared_ptr<GroundTileSet> ground;
	std::vector<DiggerTileSet> tileSets;

	std::shared_ptr<MV::Scene::RotationJointAttributes> motor;

	MV::SharedTextures &sharedTextures;

	MV::Scene::SafeComponent<MV::Scene::Grid> background;
	MV::Scene::SafeComponent<MV::Scene::Grid> environment;
	MV::Scene::SafeComponent<MV::Scene::Grid> foreground;

	MV::Scene::SafeComponent<MV::Scene::Environment> physicsWorld;

	const MV::Size<> tileSize { 32, 32 };

	MV::MouseState &mouse;
};

class DiggerGame {
public:
	DiggerGame(Managers &a_managers);

	//return true if we're still good to go
	bool update(double dt);
	void handleInput();
	void render();

	Managers& getManager() {
		return managers;
	}

private:
	DiggerGame(const DiggerGame &) = delete;
	DiggerGame& operator=(const DiggerGame &) = delete;

	void InitializeWorldScene();

	void initializeWindow();

	void handleScroll(int a_amount);

	Managers& managers;

	std::shared_ptr<MV::Scene::Node> worldScene;

	bool done;
	MV::MouseState mouse;

	double lastUpdateDelta;

	int grounded = 0;
	MV::Stopwatch jumpTimer;

	chaiscript::ChaiScript scriptEngine;

	DiggerPlayer player;
	std::shared_ptr<DiggerWorld> world;
};

void sdl_quit_3(void);

#endif