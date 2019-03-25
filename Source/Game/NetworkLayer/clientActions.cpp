#include "clientActions.h"
#include "Game/game.h"
#include "Game/player.h"
#include "MV/Utility/cerealUtility.h"

CEREAL_REGISTER_TYPE(MatchedResponse);
CEREAL_REGISTER_TYPE(ServerDetails);
CEREAL_REGISTER_TYPE(IllegalResponse);
CEREAL_REGISTER_TYPE(LoginResponse);
CEREAL_REGISTER_TYPE(MessageAction);
CEREAL_REGISTER_DYNAMIC_INIT(mv_clientactions);

void LoginResponse::execute(Game& a_game) {
	std::cout << "Login: [" << message << "] Success: [" << (success ? "true" : "false") << "]" << std::endl;
	a_game.authenticate(*this);
}

std::shared_ptr<LocalPlayer> LoginResponse::loadedPlayer() {
	if (!playerObject && !player.empty()) {
		playerObject = MV::fromJson<std::shared_ptr<LocalPlayer>>(player);
	}
	return playerObject;
}

void IllegalResponse::execute(Game& /*a_game*/) {
	std::cout << "Illegal: [" << message << "]" << std::endl;
}

void MatchedResponse::execute(Game& a_game) {
	a_game.enterGameServer(gameServer + ":" + std::to_string(port), secret);
}
