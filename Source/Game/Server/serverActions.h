#ifndef _SERVERACTIONS_MV_H_
#define _SERVERACTIONS_MV_H_

#include "Utility/package.h"
#include "Network/package.h"
#include "Game/managers.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>

#include "chaiscript/chaiscript.hpp"

#include "pqxx/pqxx"

class LobbyConnectionState;

class ServerAction : public std::enable_shared_from_this<ServerAction> {
public:
	virtual ~ServerAction() = default;

	virtual void execute(LobbyConnectionState&) {
	}

	virtual bool done() const { return true; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(0);
	}
};

#endif
