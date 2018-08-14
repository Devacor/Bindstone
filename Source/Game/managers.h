#ifndef __MV_MANAGERS_H__
#define __MV_MANAGERS_H__

#include "Utility/package.h"
#include "Render/package.h"
#include "Render/Scene/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
#include "Network/package.h"
#include "Interface/package.h"
#include "Audio/package.h"
#include "Utility/services.h"

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