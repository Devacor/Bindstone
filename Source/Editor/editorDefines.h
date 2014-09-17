#ifndef _MV_EDITOR_DEFINES_H_
#define _MV_EDITOR_DEFINES_H_

#include "Render/package.h"
#include "Utility/package.h"
#include "Interface/package.h"

enum InterfaceColors {
	BUTTON_TOP_IDLE = 0x707070,
	BUTTON_BOTTOM_IDLE = 0x636363,
	BUTTON_TOP_ACTIVE = 0x3d3d3d,
	BUTTON_BOTTOM_ACTIVE = 0x323232,
	TEXTBOX_TOP = 0x303030,
	TEXTBOX_BOTTOM = 0x414141,
	LABEL_TOP = 0x414141,
	LABEL_BOTTOM = 0x717171,
	BOX_BACKGROUND = 0x4f4f4f,
	BOX_HEADER = 0x2d2d2d,
	CREATED_DEFAULT = 0xdfdfdf,
	SIZE_HANDLES = 0xffb400,
	SLIDER_BACKGROUND = 0x2c2e36,
	SIDER_HANDLE = 0x516191
};

struct SharedResources {
	SharedResources(MV::ThreadPool *a_pool, MV::SharedTextures *a_textures, MV::TextLibrary *a_textLibrary, MV::MouseState *a_mouse):
		pool(a_pool),
		textures(a_textures),
		textLibrary(a_textLibrary),
		mouse(a_mouse){
	}
	MV::TextLibrary *textLibrary;
	MV::MouseState *mouse;
	MV::ThreadPool *pool;
	MV::SharedTextures *textures;
};

#endif
