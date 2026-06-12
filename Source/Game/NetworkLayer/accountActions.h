#ifndef _ACCOUNTACTIONS_MV_H_
#define _ACCOUNTACTIONS_MV_H_

#include "Game/NetworkLayer/networkAction.h"

#ifdef BINDSTONE_SERVER
#include "pqxx/pqxx"
#endif

class CreatePlayer : public NetworkAction {
public:
	CreatePlayer() {}

	CreatePlayer(const std::string &a_email, const std::string &a_handle, const std::string &a_password) :
		email(a_email),
		handle(a_handle),
		password(a_password) {
	}


#ifdef BINDSTONE_SERVER
private:
	std::string createPlayerQueryString(pqxx::work& transaction, const std::string& a_salt);
	pqxx::result selectUser(pqxx::work* a_transaction);
	pqxx::result createPlayer(pqxx::work* transaction, LobbyUserConnectionState* a_connection);
public:
	virtual void execute(LobbyUserConnectionState* a_connection) override;
	static bool ensureAccount(pqxx::connection &a_db, const std::string &a_email, const std::string &a_handle, const std::string &a_password);
#endif

	template<class Archive>
	void serialize(Archive& archive) {
		archive(JAI_NVP(handle), JAI_NVP(email), JAI_NVP(password));
		NetworkAction::serialize(archive);
	}

	static std::string makeSaveString();
	static std::string makeServerSaveString();

	std::string handle;
	std::string email;
	std::string password;

	static const int DEFAULT_HARD_CURRENCY = 150;
	static const int DEFAULT_SOFT_CURRENCY = 500;

private:
	void sendValidationEmail(LobbyUserConnectionState *a_connection, const std::string &a_passSalt);

	bool validateHandle(const std::string &a_handle) {
		return a_handle.size() > 3 && MV::simpleFilter(a_handle) == a_handle;
	}
};

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

#ifdef BINDSTONE_SERVER
private:
	pqxx::result selectUser(pqxx::work* a_transaction);
public:
	virtual void execute(LobbyUserConnectionState* a_connection) override;
#endif

	template<class Archive>
	void serialize(Archive& archive) {
		archive(JAI_NVP(identifier), JAI_NVP(password), JAI_NVP(saveHash));
		NetworkAction::serialize(archive);
	}

	std::string identifier;
	std::string password;
	std::string saveHash;
};

class FindMatchRequest : public NetworkAction {
public:
	FindMatchRequest() {}
	FindMatchRequest(const std::string &a_type) : type(a_type) {}

#ifdef BINDSTONE_SERVER
	virtual void execute(LobbyUserConnectionState* a_connection) override;
#endif

	template<class Archive>
	void serialize(Archive& archive) {
		archive(JAI_NVP(type));
		NetworkAction::serialize(archive);
	}

	std::string type;
};

class ExpectedPlayersNoted : public NetworkAction {
public:
	ExpectedPlayersNoted() {}

#ifdef BINDSTONE_SERVER
	virtual void execute(LobbyGameConnectionState* a_connection) override;
#endif

	template<class Archive>
	void serialize(Archive& archive) {
		NetworkAction::serialize(archive);
	}
};

#endif
