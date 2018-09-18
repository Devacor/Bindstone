#ifndef __MV_MANAGERS_H__
#define __MV_MANAGERS_H__

#include "MV/Utility/package.h"
#include "MV/Render/package.h"
#include "MV/Render/Scene/package.h"
#include "MV/Audio/package.h"
#include "MV/Network/package.h"
#include "MV/Interface/package.h"
#include "MV/Audio/package.h"
#include "MV/Utility/services.h"

struct Managers {
	Managers() : textLibrary(renderer), audio(*MV::AudioPlayer::instance()) {
		services.connect(&timer);
		services.connect(&pool);
		services.connect(&renderer);
		services.connect(&textLibrary);
		services.connect(&textures);
		services.connect(&audio);
	}

	MV::Stopwatch timer;
	MV::ThreadPool pool;
	MV::Draw2D renderer;
	MV::TextLibrary textLibrary;
	MV::SharedTextures textures;
	MV::AudioPlayer& audio;

	MV::Services services;
};

void bindstoneScriptHook(chaiscript::ChaiScript &a_script, MV::TapDevice &a_tapDevice, MV::ThreadPool &a_pool);

#endif