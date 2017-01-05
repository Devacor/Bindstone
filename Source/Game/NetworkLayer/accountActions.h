#ifndef _ACCOUNTACTIONS_MV_H_
#define _ACCOUNTACTIONS_MV_H_

#include "Game/NetworkLayer/serverActions.h"
#include "Utility/chaiscriptUtility.h"

class CreatePlayer : public ServerAction {
public:
	CreatePlayer() {}

	CreatePlayer(const std::string &a_email, const std::string &a_handle, const std::string &a_password) :
		email(a_email),
		handle(a_handle),
		password(a_password) {
	}

	virtual void execute(LobbyConnectionState* a_connection) override;

	virtual bool done() const override { return doneFlag; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(handle), CEREAL_NVP(email), CEREAL_NVP(password), cereal::make_nvp("ServerAction", cereal::base_class<ServerAction>(this)));
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<CreatePlayer>(), "CreatePlayer");
		a_script.add(chaiscript::base_class<ServerAction, CreatePlayer>());
		a_script.add(chaiscript::constructor<CreatePlayer(const std::string &a_email, const std::string &a_identifier, const std::string &a_password)>(), "CreatePlayer");
	}

private:
	void sendValidationEmail(LobbyConnectionState *a_connection, const std::string &a_passSalt);

	bool validateHandle(const std::string &a_handle) {
		return a_handle.size() > 3 && MV::simpleFilter(a_handle) == a_handle;
	}

	std::string makeSaveString();
	std::string makeServerSaveString();

	std::string createPlayerQueryString(pqxx::work &transaction, const std::string &a_salt);
	pqxx::result selectUser(pqxx::work* a_transaction);
	pqxx::result createPlayer(pqxx::work* transaction, LobbyConnectionState *a_connection);

	std::string handle;
	std::string email;
	std::string password;
	bool doneFlag = false;

	const int DEFAULT_HARD_CURRENCY = 150;
	const int DEFAULT_SOFT_CURRENCY = 500;
};

CEREAL_REGISTER_TYPE(CreatePlayer);


class LoginRequest : public ServerAction {
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

	virtual void execute(LobbyConnectionState* a_connection) override;

	virtual bool done() const override { return doneFlag; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(identifier), CEREAL_NVP(password), CEREAL_NVP(saveHash), cereal::make_nvp("ServerAction", cereal::base_class<ServerAction>(this)));
		doneFlag = false;
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<LoginRequest>(), "LoginRequest");
		a_script.add(chaiscript::base_class<ServerAction, LoginRequest>());
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

class FindMatchRequest : public ServerAction {
public:
	enum MatchType {NORMAL, RANKED};

	FindMatchRequest() {}
	FindMatchRequest(MatchType a_type) : type(a_type) {}
	
	virtual void execute(LobbyConnectionState* a_connection) override;

	virtual bool done() const override { return true; }

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			cereal::make_nvp("ServerAction", cereal::base_class<ServerAction>(this)),
			cereal::make_nvp("type", type));
	}

	static void hook(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<FindMatchRequest>(), "FindMatchRequest");
		a_script.add(chaiscript::base_class<ServerAction, FindMatchRequest>());
		a_script.add(chaiscript::constructor<FindMatchRequest(MatchType a_type)>(), "FindMatchRequest");
		a_script.add(chaiscript::fun(&FindMatchRequest::type), "type");

		chaiscript::ModulePtr m = chaiscript::ModulePtr(new chaiscript::Module());
		chaiscript::utility::add_class<MatchType>(*m, "MatchType", { 
			{ NORMAL, "NORMAL" },
			{ RANKED, "RANKED" }
		});
		a_script.add(m);
	}

private:
	MatchType type;
};

CEREAL_REGISTER_TYPE(FindMatchRequest);

#endif
