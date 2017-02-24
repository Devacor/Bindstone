#ifndef _GAMESERVERACTIONS_MV_H_
#define _GAMESERVERACTIONS_MV_H_

#include "Game/NetworkLayer/serverActions.h"
#include "Utility/chaiscriptUtility.h"

class GameServerFinishedRequest : public ServerGameAction {
public:
	GameServerFinishedRequest() {}
	
	virtual void execute(LobbyGameConnectionState* a_connection) override;

	virtual bool done() const override { return true; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			cereal::make_nvp("ServerAction", cereal::base_class<ServerUserAction>(this)),
			cereal::make_nvp("type", type));
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<GameServerFinishedRequest>(), "GameServerFinishedRequest");
		a_script.add(chaiscript::base_class<ServerUserAction, GameServerFinishedRequest>());

		chaiscript::ModulePtr m = chaiscript::ModulePtr(new chaiscript::Module());
		a_script.add(m);
	}

private:
};

CEREAL_REGISTER_TYPE(GameServerFinishedRequest);

#endif
