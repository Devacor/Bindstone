#ifndef _MV_EDITOR_DEFINES_H_
#define _MV_EDITOR_DEFINES_H_

#include "Render/package.h"
#include "Utility/package.h"
#include "Interface/package.h"

enum InterfaceColors : uint32_t {
	POSITION_HANDLE_CENTER = 0xFFFFFFFF,
	POSITION_HANDLE = 0x44FFFF00,
	ROTATION_HANDLE = 0x8800FFFF,
	BUTTON_TOP_IDLE = 0x707070,
	BUTTON_BOTTOM_IDLE = 0x636363,
	BUTTON_TOP_ACTIVE = 0x3d3d3d,
	BUTTON_BOTTOM_ACTIVE = 0x323232,
	SCENE_BUTTON_TOP_IDLE = 0x494949,
	SCENE_BUTTON_BOTTOM_IDLE = 0x3e3e3e,
	SCENE_BUTTON_TOP_ACTIVE = 0x595959,
	SCENE_BUTTON_BOTTOM_ACTIVE = 0x4e4e4e,
	TEXTBOX_TOP = 0x303030,
	TEXTBOX_BOTTOM = 0x414141,
	LABEL_TOP = 0x414141,
	LABEL_BOTTOM = 0x717171,
	BOX_BACKGROUND = 0x4f4f4f,
	BOX_HEADER = 0x2d2d2d,
	CREATED_DEFAULT = 0xdfdfdf,
	SIZE_HANDLES = 0x44ffb400,
	SLIDER_BACKGROUND = 0x2c2e36,
	SIDER_HANDLE = 0x516191,
	TOGGLE_BACKGROUND = 0x9fa4b7,
	TOGGLE_CENTER = 0x3c404f
};
class Editor;
struct SharedResources {
	SharedResources(Editor* a_editor, MV::ThreadPool *a_pool, MV::SharedTextures *a_textures, MV::TextLibrary *a_textLibrary, MV::MouseState *a_mouse):
		pool(a_pool),
		textures(a_textures),
		textLibrary(a_textLibrary),
		mouse(a_mouse),
		editor(a_editor){
	}
	MV::TextLibrary *textLibrary;
	MV::MouseState *mouse;
	MV::ThreadPool *pool;
	MV::SharedTextures *textures;
	Editor* editor;
};

#endif
