#ifndef _BINDSTONE_CERTIFICATES_H_
#define _BINDSTONE_CERTIFICATES_H_

#include "MV/Network/tls.h"

#include <string>

// Made by Tools/LocalServer/make_certs.py; Source/MV/Network/AGENTS.md explains each file.
namespace GameCertificates {
	inline const std::string FOLDER = "Certificates/Tls/";
	inline const std::string CA = FOLDER + "ca.pem";
	// The common name prefix of a game server's client certificate, which the lobby requires on 22326.
	inline const std::string GAME_SERVER_CLIENT_PREFIX = "gameserver:";

	inline MV::TlsConfig player() {
		return {CA};
	}

	inline MV::TlsConfig lobby(bool a_requireGameServerCertificates) {
		return {CA, FOLDER + "lobby.pem", FOLDER + "lobby.key", a_requireGameServerCertificates};
	}

	inline MV::TlsConfig gameServer(const std::string &a_name) {
		return {CA, FOLDER + "gameservers/" + a_name + "/server.pem", FOLDER + "gameservers/" + a_name + "/server.key"};
	}

	inline MV::TlsConfig gameServerToLobby(const std::string &a_name) {
		return {CA, FOLDER + "gameservers/" + a_name + "/client.pem", FOLDER + "gameservers/" + a_name + "/client.key"};
	}
}

#endif
