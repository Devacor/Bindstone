/* PUC Lua host for the kernel suite: stock libs plus dasProfile's profile_native (addOne, a QPC
   clock), exposed as a global the way dasProfile's own Lua host does.   lua_kernel_host file.lua */
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int l_addOne(lua_State *L) {
	lua_pushinteger(L, luaL_checkinteger(L, 1) + 1);
	return 1;
}

static int l_clock(lua_State *L) {
	LARGE_INTEGER freq, now;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&now);
	lua_pushnumber(L, (double)now.QuadPart / (double)freq.QuadPart);
	return 1;
}

static const luaL_Reg profile_native_funcs[] = {
	{"addOne", l_addOne},
	{"clock", l_clock},
	{NULL, NULL}
};

static int luaopen_profile_native(lua_State *L) {
	luaL_newlib(L, profile_native_funcs);
	return 1;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: lua_kernel_host file.lua\n");
		return 2;
	}
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	luaL_requiref(L, "profile_native", luaopen_profile_native, 1);
	lua_pop(L, 1);
	if (luaL_dofile(L, argv[1]) != LUA_OK) {
		fprintf(stderr, "%s\n", lua_tostring(L, -1));
		lua_close(L);
		return 1;
	}
	lua_close(L);
	return 0;
}
