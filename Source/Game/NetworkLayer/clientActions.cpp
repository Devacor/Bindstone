#include "clientActions.h"
#include "Game/game.h"
#include "Game/player.h"
#include "MV/Serialization/serialize.h"


void LoginResponse::execute(Game& a_game) {
	std::cout << "Login: [" << message << "] Success: [" << (success ? "true" : "false") << "]" << std::endl;
	a_game.authenticate(*this);
}

std::shared_ptr<LocalPlayer> LoginResponse::loadedPlayer() {
	if (!playerObject && !player.empty()) {
		playerObject = MV::fromJson<std::shared_ptr<LocalPlayer>>(player, MV::Services::instance());
	}
	return playerObject;
}

void IllegalResponse::execute(Game& /*a_game*/) {
	std::cout << "Illegal: [" << message << "]" << std::endl;
}

void MatchedResponse::execute(Game& a_game) {
	a_game.enterGameServer(gameServer + ":" + std::to_string(port), secret);
}
