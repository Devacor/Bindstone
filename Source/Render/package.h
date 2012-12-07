#ifndef __RENDER_PACKAGE__
#define __RENDER_PACKAGE__
#ifdef __APPLE__
	#include "drawShapes.h"
	#include "textures.h"
	#include "text.h"
	#include "tiles.h"
#else
	#include "Render/drawShapes.h"
	#include "Render/textures.h"
	#include "Render/text.h"
	#include "Render/tiles.h"
#endif
#endif