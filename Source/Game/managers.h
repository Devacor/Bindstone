#ifndef __MV_MANAGERS_H__
#define __MV_MANAGERS_H__

#include "Utility/package.h"
#include "Render/package.h"
#include "Render/Scene/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
#include "Network/package.h"
#include "Interface/package.h"

struct Managers {
	Managers():textLibrary(renderer){}

	MV::Stopwatch timer;
	MV::ThreadPool pool;
	MV::Draw2D renderer;
	MV::MouseState mouse;
	MV::TextLibrary textLibrary;
	MV::SharedTextures textures;
};

#endif