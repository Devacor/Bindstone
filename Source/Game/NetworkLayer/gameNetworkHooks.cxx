#include "Game/NetworkLayer/package.h"

MV::Script::Registrar<CreatePlayer> _hookCreatePlayer([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<CreatePlayer>(), "CreatePlayer");
	a_script.add(chaiscript::base_class<NetworkAction, CreatePlayer>());
	a_script.add(chaiscript::constructor<CreatePlayer(const std::string &a_email, const std::string &a_identifier, const std::string &a_password)>(), "CreatePlayer");
});

MV::Script::Registrar<LoginRequest> _hookLoginRequest([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<LoginRequest>(), "LoginRequest");
	a_script.add(chaiscript::base_class<NetworkAction, LoginRequest>());
	a_script.add(chaiscript::constructor<LoginRequest(const std::string &a_identifier, const std::string &a_password)>(), "LoginRequest");
	a_script.add(chaiscript::constructor<LoginRequest(const std::string &a_identifier, const std::string &a_password, const std::string &a_saveHash)>(), "LoginRequest");
});

MV::Script::Registrar<FindMatchRequest> _hookFindMatchRequest([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<FindMatchRequest>(), "FindMatchRequest");
	a_script.add(chaiscript::base_class<NetworkAction, FindMatchRequest>());
	a_script.add(chaiscript::constructor<FindMatchRequest(const std::string &a_type)>(), "FindMatchRequest");
	a_script.add(chaiscript::fun(&FindMatchRequest::type), "type");
});

MV::Script::Registrar<MessageAction> _hookMessageAction([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<MessageAction>(), "MessageResponse");
	a_script.add(chaiscript::base_class<NetworkAction, MessageAction>());

	a_script.add(chaiscript::fun(&MessageAction::message), "message");
});

MV::Script::Registrar<LoginResponse> _hookLoginResponse([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<LoginResponse>(), "LoginResponse");
	a_script.add(chaiscript::base_class<NetworkAction, LoginResponse>());

	a_script.add(chaiscript::fun(&LoginResponse::message), "message");
	a_script.add(chaiscript::fun(&LoginResponse::success), "success");
});

MV::Script::Registrar<IllegalResponse> _hookIllegalResponse([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<IllegalResponse>(), "IllegalResponse");
	a_script.add(chaiscript::base_class<NetworkAction, IllegalResponse>());

	a_script.add(chaiscript::fun(&IllegalResponse::message), "message");
});

MV::Script::Registrar<NetworkAction> _hookNetworkAction([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::user_type<NetworkAction>(), "NetworkAction");
	a_script.add(chaiscript::fun(&NetworkAction::toNetworkString), "toNetworkString");
});
