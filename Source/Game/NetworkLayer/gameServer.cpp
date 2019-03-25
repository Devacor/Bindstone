#include "gameServer.h"
#include "networkAction.h"
#include "clientActions.h"

#include <SDL.h>

#include <conio.h>

void sdl_quit_gameserver(void) {
	SDL_Quit();
	TTF_Quit();
}

GameUserConnectionState::GameUserConnectionState(const std::shared_ptr<MV::Connection> &a_connection, GameServer& a_server) :
	MV::ConnectionStateBase(a_connection),
	ourServer(a_server) {
}

void GameUserConnectionState::connectImplementation() {
	connection()->send(makeNetworkString<ServerDetails>());
}

void GameUserConnectionState::disconnectImplementation() {
	ourServer.userDisconnected(ourSecret);
}

void GameUserConnectionState::message(const std::string &a_message) {
	auto action = MV::fromBinaryString<std::shared_ptr<NetworkAction>>(a_message);
	action->execute(this, ourServer);
}

GameServer::GameServer(Managers &a_managers, unsigned short a_port) :
	manager(a_managers),
	gameData(a_managers),
	ourUserServer(std::make_shared<MV::Server>(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), a_port),
		[this](const std::shared_ptr<MV::Connection> &a_connection) {
			return std::make_unique<GameUserConnectionState>(a_connection, *this);
		})) {

	MV::Size<> worldSize(960, 640);
	MV::Size<int> windowSize(960, 640);

	gameData.managers().renderer.//makeHeadless().
		window().windowedMode().allowUserResize(false).resizeWorldWithWindow(true);

	if (!gameData.managers().renderer.initialize(windowSize, worldSize)) {
		exit(0);
	}
	atexit(sdl_quit_gameserver);
	MV::AudioPlayer::instance()->initAudio();
	nullMouse.update();

	rootScene = MV::Scene::Node::make(gameData.managers().renderer);

	MV::FontDefinition::make(gameData.managers().textLibrary, "default", "Assets/Fonts/Verdana.ttf", 14);
	MV::FontDefinition::make(gameData.managers().textLibrary, "small", "Assets/Fonts/Verdana.ttf", 9);
	MV::FontDefinition::make(gameData.managers().textLibrary, "big", "Assets/Fonts/Verdana.ttf", 18, MV::FontStyle::BOLD | MV::FontStyle::UNDERLINE);
	if (!gameData.managers().renderer.headless()) {
		gameData.managers().renderer.loadShader("vortex", "Assets/Shaders/default.vert", "Assets/Shaders/vortex.frag");
		gameData.managers().renderer.loadShader("lillypad", "Assets/Shaders/lillypad.vert", "Assets/Shaders/default.frag");
		gameData.managers().renderer.loadShader("wave", "Assets/Shaders/wave.vert", "Assets/Shaders/wave.frag");
		gameData.managers().renderer.loadShader("waterfall", "Assets/Shaders/default.vert", "Assets/Shaders/waterfall.frag");
		gameData.managers().renderer.loadShader("pool", "Assets/Shaders/default.vert", "Assets/Shaders/pool.frag");
		gameData.managers().renderer.loadShader("shimmer", "Assets/Shaders/default.vert", "Assets/Shaders/shimmer.frag");

		gameData.managers().textures.assemblePacks("Assets/Atlases", &gameData.managers().renderer);
		gameData.managers().textures.files("Assets/Map");
		gameData.managers().textures.files("Assets/Images");
	}
	//(const std::shared_ptr<Player> &a_leftPlayer, const std::shared_ptr<Player> &a_rightPlayer, const std::shared_ptr<MV::Scene::Node> &a_scene, MV::TapDevice& a_mouse, LocalData& a_data)

	initializeClientToLobbyServer();
}

void GameServer::update(double dt) {
	rootTask.update(dt);
	if (ourLobbyClient) {
		ourLobbyClient->update();
	}else{
		if (rootTask.get("reconnect", false) == nullptr) {
			rootTask.now("reconnect").recent()->
				then(std::make_shared<MV::BlockForSeconds>(.5f)).
				then("reconnecting", [&](MV::Task&, double) {
					initializeClientToLobbyServer();
					return false;
				});
		}
	}
	ourUserServer->update(dt);
	threadPool.run();

	if (ourInstance) {
		ourInstance->update(dt);
		if (!MV::RUNNING_IN_HEADLESS) {
			ourInstance->scene()->draw();
		}
	}

	gameData.managers().renderer.updateScreen();

	handleInput();
}

void GameServer::handleInput() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		auto windowResized = gameData.managers().renderer.handleEvent(event);
		if (!windowResized && (!ourInstance || (ourInstance && !ourInstance->handleEvent(event)))) {
		}
	}
	nullMouse.update();

	if (_kbhit()) {
		switch (_getch()) {
		case 'c':
			std::cout << "Connections: " << ourUserServer->connections().size() << std::endl;
			break;
		}
	}
}

void GameServer::assign(const AssignedPlayer &a_left, const AssignedPlayer &a_right, const std::string &a_queueId) {
	left = a_left;
	right = a_right;
	queueId = a_queueId;
	ourInstance = ServerGameInstance::make(left->player, right->player, *this);
}
