#include "clientActions.h"
#include "Game/game.h"
#include "Game/player.h"
#include "Utility/cerealUtility.h"

void LoginResponse::execute(Game& a_game) {
	std::cout << "Login: [" << message << "] Success: [" << (success ? "true" : "false") << "]" << std::endl;
	a_game.authenticate(*this);
}

std::shared_ptr<Player> LoginResponse::loadedPlayer() {
	return player.empty() ? 
		std::shared_ptr<Player>() : 
		MV::fromJson<std::shared_ptr<Player>>(player);
}

void IllegalResponse::execute(Game& /*a_game*/) {
	std::cout << "Illegal: [" << message << "]" << std::endl;
}

void MatchedResponse::execute(Game& a_game) {
	a_game.enterGameServer(gameServer, secret);
}
