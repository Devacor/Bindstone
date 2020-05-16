#ifndef _NETWORKACTION_MV_H_
#define _NETWORKACTION_MV_H_

#include "MV/Utility/package.h"
#include "MV/Network/package.h"
#include "Game/managers.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>

//#include "pqxx/pqxx"

class Game;
class GameServer;
class LobbyUserConnectionState;
class LobbyGameConnectionState;
class GameUserConnectionState;

class NetworkAction : public std::enable_shared_from_this<NetworkAction> {
public:
	virtual ~NetworkAction() = default;

	virtual void execute(Game&) { MV::require<MV::ResourceException>(false, "Unimplemented NetworkAction::execute(Game);"); }

#ifdef BINDSTONE_SERVER
	virtual void execute(GameServer&) { MV::require<MV::ResourceException>(false, "Unimplemented NetworkAction::execute(GameServer);"); }
	virtual void execute(GameUserConnectionState*, GameServer&) { MV::require<MV::ResourceException>(false, "Unimplemented NetworkAction::execute(GameUserConnectionState, GameServer);"); }
	virtual void execute(LobbyUserConnectionState*) { MV::require<MV::ResourceException>(false, "Unimplemented NetworkAction::execute(LobbyUserConnectionState);"); }
	virtual void execute(LobbyGameConnectionState*) { MV::require<MV::ResourceException>(false, "Unimplemented NetworkAction::execute(LobbyGameConnectionState);"); }
#endif

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(0);
	}

	std::string toNetworkString() {
		return MV::toBinaryStringCast<NetworkAction>(shared_from_this());
	}
};

template <typename T, typename ... Args>
std::string makeNetworkString(Args && ... args) {
	return MV::toBinaryStringCast<NetworkAction>(std::make_shared<T>(std::forward<Args>(args)...));
}

#endif
