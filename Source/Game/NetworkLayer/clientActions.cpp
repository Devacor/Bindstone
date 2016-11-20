#include "clientActions.h"
#include "Game/game.h"

void LoginResponse::execute(Game& a_game) {
	std::cout << "Login: [" << message << "] Success: [" << (success ? "true" : "false") << "]" << std::endl;
	a_game.onLoginResponse(*this);
}
