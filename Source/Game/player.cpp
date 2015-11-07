#include "player.h"
#include "state.h"

Team::Team(const std::shared_ptr<Player> &a_player, const Constants& a_constants) :
	player(a_player),
	health(a_constants.startHealth) {
}
