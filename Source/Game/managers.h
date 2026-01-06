#ifndef __MV_MANAGERS_H__
#define __MV_MANAGERS_H__

#include "MV/Utility/package.h"
#include "MV/Render/package.h"
#include "MV/Render/Scene/package.h"
#include "MV/Audio/package.h"
#include "MV/Network/package.h"
#include "MV/Interface/package.h"
#include "MV/Audio/package.h"
#include "MV/Utility/services.hpp"
#include <jaiscript/signals/signal.hpp>

struct DefaultLogin {
	std::string id;
	std::string password;
};

struct StandardMessages {
	jai::signal_emitter<void()> lobbyConnected;
	jai::signal_emitter<void(const std::string &)> lobbyDisconnect;
	jai::signal_emitter<void(bool, const std::string &)> lobbyAuthenticated;
};

struct Managers {
	Managers(const DefaultLogin &a_login) : defaultLogin(a_login), textLibrary(renderer)/*, audio(*MV::AudioPlayer::instance())*/ {
		services.connect(&timer);
		services.connect(&pool);
		services.connect(&renderer);
		services.connect(&messages);
		services.connect(&textLibrary);
		services.connect(&textures);
		services.connect(&defaultLogin);
		//services.connect(&audio);
	}

	MV::Stopwatch timer;
	MV::ThreadPool pool;
	MV::Draw2D renderer;
	StandardMessages messages;
	MV::TextLibrary textLibrary;
	MV::SharedTextures textures;
	DefaultLogin defaultLogin;
	//MV::AudioPlayer& audio;

	MV::Services services;
};

#endif