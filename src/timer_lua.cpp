/******************************************************************************
	Project: 	MicroMacro
	Author: 	SolarStrike Software
	URL:		www.solarstrike.net
	License:	Modified BSD (see license.txt)
******************************************************************************/

#include "timer_lua.h"
#include "timer.h"
#include "luatypes.h"
#include "int64_lua.h"
#include "error.h"
#include "macro.h"

extern "C"
{
	#include <lua.h>
	#include <lauxlib.h>
	#include <lualib.h>
}


int Timer_lua::regmod(lua_State *L)
{
	static const luaL_Reg _funcs[] = {
		{"getNow", Timer_lua::getNow},
		{"deltaTime", Timer_lua::deltaTime},
		{"diff", Timer_lua::diff},
		{NULL, NULL}
	};

	luaL_newlib(L, _funcs);
	lua_setglobal(L, TIMER_MODULE_NAME);

	return MicroMacro::ERR_OK;
}

/*	timer.getNow()
	Returns:	table (int64)

	Returns the current high-precision time as an int64 table.
*/
int Timer_lua::getNow(lua_State *L)
{
	if( lua_gettop(L) != 0 )
		wrongArgs(L);

	TimeType now = ::getNow();
	lua_pushint64(L, now);

	return 1;
}

/*	timer.deltaTime()
	Returns:	number delta

	Returns the deltaTime for the current logic cycle
*/
int Timer_lua::deltaTime(lua_State *L)
{
	if( lua_gettop(L) != 0 )
		wrongArgs(L);

	double dt = Macro::instance()->getEngine()->getDeltaTime();
	lua_pushnumber(L, dt);
	return 1;
}

/*	timer.diff(int64 t2, int64 t1)
	Returns:	number delta

	Compares two high-precision time values (from timer.getNow())
	and returns the amount of time that has elapsed between them
	in seconds.
*/
int Timer_lua::diff(lua_State *L)
{
	if( lua_gettop(L) != 2 )
		wrongArgs(L);
	checkType(L, LT_TABLE, 1);
	checkType(L, LT_TABLE, 2);

	if( !lua_isint64(L, 1) )
		luaL_typerror(L, 1, LuaType::metatable_int64);
	if( !lua_isint64(L, 2) )
		luaL_typerror(L, 2, LuaType::metatable_int64);

	TimeType t2, t1;
	t2 = lua_toint64(L, 1);
	t1 = lua_toint64(L, 2);

	double dt = ::deltaTime(t2, t1);
	lua_pushnumber(L, dt);
	return 1;
}
