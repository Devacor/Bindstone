#include "state.h"
#include "player.h"

bool LocalData::isLocal(const std::shared_ptr<Player> &a_other) const {
	return localPlayer && localPlayer->email == a_other->email;
}
