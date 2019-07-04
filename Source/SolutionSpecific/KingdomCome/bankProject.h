#ifndef __DIGGER_GAME_H__
#define __DIGGER_GAME_H__
#include <SDL.h>
#include "MV/Utility/package.h"
#include "MV/Render/package.h"
#include "MV/Audio/package.h"
#include "MV/Network/package.h"
#include "MV/Interface/package.h"
#include "MV/Physics/package.h"
#include <string>
#include <ctime>
#include <stdint.h>
#include <memory>
#include "Game/managers.h"

class ExampleTileSet {
public:
	ExampleTileSet(MV::SharedTextures &a_sharedTextures) {

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

class ExampleWorld {
public:
	MV::Scene::SafeComponent<MV::Scene::Sprite> playerVisual;

	ExampleWorld(const std::shared_ptr<MV::Scene::Node> &a_node, MV::SharedTextures &a_sharedTextures, MV::TapDevice &a_mouse);

	void controlPlayer();
private:
	void loadTextures() {
		tileset = std::make_shared<ExampleTileSet>(sharedTextures);
	}

	std::shared_ptr<ExampleTileSet> tileset;

	MV::SharedTextures &sharedTextures;

	MV::Scene::SafeComponent<MV::Scene::Node> background;
	MV::Scene::SafeComponent<MV::Scene::Node> environment;
	MV::Scene::SafeComponent<MV::Scene::Node> foreground;

	MV::TapDevice &mouse;
};

class ExampleGame {
public:
	ExampleGame(Managers &a_managers);

	//return true if we're still good to go
	bool update(double dt);
	void handleInput();
	void render();

	Managers& getManager() {
		return managers;
	}

private:
	ExampleGame(const ExampleGame&) = delete;
	ExampleGame& operator=(const ExampleGame&) = delete;

	void InitializeWorldScene();

	void initializeWindow();

	void handleScroll(int a_amount);

	Managers& managers;

	std::shared_ptr<MV::Scene::Node> worldScene;

	bool done;
	MV::TapDevice mouse;

	double lastUpdateDelta;
	
	chaiscript::ChaiScript scriptEngine;

	std::shared_ptr<ExampleWorld> world;
};

void sdl_quit_3(void);

#endif