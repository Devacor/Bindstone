#ifndef _NETWORKACTION_MV_H_
#define _NETWORKACTION_MV_H_

#include "Utility/package.h"
#include "Network/package.h"
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
struct Player;

class NetworkAction : public std::enable_shared_from_this<NetworkAction> {
public:
	virtual ~NetworkAction() = default;

	virtual void execute(Game&) { MV::require<MV::ResourceException>(false, "Unimplemented NetworkAction::execute(Game);"); }
	virtual void execute(GameServer&) { MV::require<MV::ResourceException>(false, "Unimplemented NetworkAction::execute(GameServer);"); }

	virtual void execute(GameUserConnectionState*, GameServer&) { MV::require<MV::ResourceException>(false, "Unimplemented NetworkAction::execute(GameUserConnectionState, GameServer);"); }
	virtual void execute(LobbyUserConnectionState*) { MV::require<MV::ResourceException>(false, "Unimplemented NetworkAction::execute(LobbyUserConnectionState);"); }
	virtual void execute(LobbyGameConnectionState*) { MV::require<MV::ResourceException>(false, "Unimplemented NetworkAction::execute(LobbyGameConnectionState);"); }

	virtual bool done() const { return true; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(0);
	}

	std::string toNetworkString() {
		return MV::toBinaryStringCast<NetworkAction>(shared_from_this());
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<NetworkAction>(), "NetworkAction");
		a_script.add(chaiscript::fun(&NetworkAction::toNetworkString), "toNetworkString");
	}
};

template <typename T, typename ... Args>
std::string makeNetworkString(Args && ... args) {
	return MV::toBinaryStringCast<NetworkAction>(std::make_shared<T>(std::forward<Args>(args)...));
}

#endif
