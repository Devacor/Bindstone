#ifndef _ACCOUNTACTIONS_MV_H_
#define _ACCOUNTACTIONS_MV_H_

#include "Game/NetworkLayer/networkAction.h"
#include "pqxx/pqxx"

#include "Utility/chaiscriptUtility.h"

class CreatePlayer : public NetworkAction {
public:
	CreatePlayer() {}

	CreatePlayer(const std::string &a_email, const std::string &a_handle, const std::string &a_password) :
		email(a_email),
		handle(a_handle),
		password(a_password) {
	}

	virtual void execute(LobbyUserConnectionState* a_connection) override;

	virtual bool done() const override { return doneFlag; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(handle), CEREAL_NVP(email), CEREAL_NVP(password), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<CreatePlayer>(), "CreatePlayer");
		a_script.add(chaiscript::base_class<NetworkAction, CreatePlayer>());
		a_script.add(chaiscript::constructor<CreatePlayer(const std::string &a_email, const std::string &a_identifier, const std::string &a_password)>(), "CreatePlayer");
	}

private:
	void sendValidationEmail(LobbyUserConnectionState *a_connection, const std::string &a_passSalt);

	bool validateHandle(const std::string &a_handle) {
		return a_handle.size() > 3 && MV::simpleFilter(a_handle) == a_handle;
	}

	std::string makeSaveString();
	std::string makeServerSaveString();

	std::string createPlayerQueryString(pqxx::work &transaction, const std::string &a_salt);
	pqxx::result selectUser(pqxx::work* a_transaction);
	pqxx::result createPlayer(pqxx::work* transaction, LobbyUserConnectionState *a_connection);

	std::string handle;
	std::string email;
	std::string password;
	bool doneFlag = false;

	const int DEFAULT_HARD_CURRENCY = 150;
	const int DEFAULT_SOFT_CURRENCY = 500;
};

CEREAL_REGISTER_TYPE(CreatePlayer);


class LoginRequest : public NetworkAction {
public:
	LoginRequest() {}
	LoginRequest(const std::string &a_identifier, const std::string &a_password) :
		identifier(a_identifier),
		password(a_password) {
	}
	LoginRequest(const std::string &a_identifier, const std::string &a_password, const std::string &a_saveHash) :
		identifier(a_identifier),
		password(a_password),
		saveHash(a_saveHash) {
	}

	virtual void execute(LobbyUserConnectionState* a_connection) override;

	virtual bool done() const override { return doneFlag; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(identifier), CEREAL_NVP(password), CEREAL_NVP(saveHash), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
		doneFlag = false;
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<LoginRequest>(), "LoginRequest");
		a_script.add(chaiscript::base_class<NetworkAction, LoginRequest>());
		a_script.add(chaiscript::constructor<LoginRequest(const std::string &a_identifier, const std::string &a_password)>(), "LoginRequest");
		a_script.add(chaiscript::constructor<LoginRequest(const std::string &a_identifier, const std::string &a_password, const std::string &a_saveHash)>(), "LoginRequest");
	}

private:
	pqxx::result selectUser(pqxx::work* a_transaction);

	std::string identifier;
	std::string password;
	std::string saveHash;
		
	bool doneFlag = false;
};

CEREAL_REGISTER_TYPE(LoginRequest);

class FindMatchRequest : public NetworkAction {
public:
	FindMatchRequest() {}
	FindMatchRequest(const std::string &a_type) : type(a_type) {}
	
	virtual void execute(LobbyUserConnectionState* a_connection) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			cereal::make_nvp("type", type),
			cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<FindMatchRequest>(), "FindMatchRequest");
		a_script.add(chaiscript::base_class<NetworkAction, FindMatchRequest>());
		a_script.add(chaiscript::constructor<FindMatchRequest(const std::string &a_type)>(), "FindMatchRequest");
		a_script.add(chaiscript::fun(&FindMatchRequest::type), "type");
	}

private:
	std::string type;
};

CEREAL_REGISTER_TYPE(FindMatchRequest);

class ExpectedPlayersNoted : public NetworkAction {
public:
	ExpectedPlayersNoted() {}

	virtual void execute(LobbyGameConnectionState* a_connection) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}
};

CEREAL_REGISTER_TYPE(ExpectedPlayersNoted);

#endif
