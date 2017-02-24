#ifndef _SERVERACTIONS_MV_H_
#define _SERVERACTIONS_MV_H_

#include "Utility/package.h"
#include "Network/package.h"
#include "Game/managers.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>

#include "Utility/cerealUtility.h"
#include "chaiscript/chaiscript.hpp"

#include "pqxx/pqxx"

class LobbyUserConnectionState;
class LobbyGameConnectionState;

class ServerUserAction : public std::enable_shared_from_this<ServerUserAction> {
public:
	virtual ~ServerUserAction() = default;

	virtual void execute(LobbyUserConnectionState*) {
	}

	virtual bool done() const { return true; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(0);
	}

	std::string toNetworkString() {
		return MV::toBinaryString(shared_from_this());
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<ServerUserAction>(), "ServerUserAction");
		a_script.add(chaiscript::fun(&ServerUserAction::toNetworkString), "toNetworkString");

		a_script.add(chaiscript::fun(&ServerUserAction::execute), "execute");
		a_script.add(chaiscript::fun(&ServerUserAction::done), "done");
	}
};

class ServerGameAction : public std::enable_shared_from_this<ServerGameAction> {
public:
	virtual ~ServerGameAction() = default;

	virtual void execute(LobbyGameConnectionState*) {
	}

	virtual bool done() const { return true; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(0);
	}

	std::string toNetworkString() {
		return MV::toBinaryString(shared_from_this());
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<ServerGameAction>(), "ServerGameAction");
		a_script.add(chaiscript::fun(&ServerGameAction::toNetworkString), "toNetworkString");

		a_script.add(chaiscript::fun(&ServerGameAction::execute), "execute");
		a_script.add(chaiscript::fun(&ServerGameAction::done), "done");
	}
};



#endif
