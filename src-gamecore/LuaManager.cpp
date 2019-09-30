#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "gamecore-pch.h"
#include "fs.h"

#include "LuaManager.h"

#include "Utility.h"

int LuaPanic(lua_State* State)
{
	const char* msg = NULL;
	if (lua_isstring(State, 1)) {
		msg = lua_tostring(State, 1);
		luaL_traceback(State, State, msg, 2);
		return 1;
	}
	else {
		return 0;
	}
}

int Break(lua_State *S)
{
    Utility::DebugBreak();
    return 0;
}


int LuaReadOnlyError(lua_State *S)
{
	luaL_error(S, "tried to write to read-only table");
	return 0;
}

LuaManager::LuaManager()
{
	WeOwnThisState = true;
    State = luaL_newstate();
    if (State)
    {
        // luaL_openlibs(State);
        RegisterStruct("Luaman", (void*)this);
        Register(Break, "DEBUGBREAK");
        luaL_openlibs(State);
        lua_atpanic(State, &LuaPanic);
    }
    // If we couldn't open lua, can we throw an exception?

	func_input = func_args = func_results = 0;
	func_err = false;
}

LuaManager::LuaManager(lua_State *L)
{
	WeOwnThisState = false;
	State = L;
}

LuaManager::~LuaManager()
{
    if (State && WeOwnThisState)
        lua_close(State);
}

void LuaManager::GetGlobal(std::string VarName)
{
    lua_getglobal(State, VarName.c_str());
}

void reportError(lua_State *State)
{
	const char* reason = lua_tostring(State, -1);


    // Log::LogPrintf("LuaManager: Lua error: %s\n", reason);
#ifndef TESTS
    Utility::DebugBreak();
#endif
    lua_pop(State, 1);
}

std::string LuaManager::GetLastError()
{
    return last_error;
}

bool LuaManager::RunScript(std::filesystem::path file)
{
    int errload = 0, errcall = 0;

    if (!std::filesystem::exists(file))
    {
        last_error = Utility::Format("File %s does not exist", file.string().c_str());
        return false;    
    }

	if ((errload = luaL_loadfile(State, file.c_str()))) {
		std::string s = lua_tostring(State, -1);
        last_error = s;
        Pop();
		return false;
	}
	
	lua_pushcfunction(State, LuaPanic);
	lua_insert(State, -2);

	if ((errcall = lua_pcall(State, 0, 0, -2)))
    {
		std::string s = lua_tostring(State, -1);
        last_error = s;
        Pop(); // remove error
		Pop(); // remove pushed panic function
		return false;
    }

	Pop(); // remove panic func.
    return true;
}

bool LuaManager::RunString(std::string string)
{
    int errload = 0, errcall = 0;

    if ((errload = luaL_loadstring(State, string.c_str())) || (errcall = lua_pcall(State, 0, LUA_MULTRET, 0)))
    {
        std::string reason = lua_tostring(State, -1);
        last_error = reason;
        Pop();
        return false;
    }
    return true;
}

bool LuaManager::Require(std::filesystem::path Filename)
{
	lua_pushcfunction(State, LuaPanic);
    lua_getglobal(State, "require");
    lua_pushstring(State, Conversion::ToLocaleStr(Filename.wstring()).c_str());
    if (lua_pcall(State, 1, 1, -3))
    {
        const char* reason = lua_tostring(State, -1);
        if (reason && last_error != reason)
        {
			last_error = reason;
        }

		lua_remove(State, -2); // Remove the traceback.
        // No popping here - if succesful or not we want to leave that return value to lua.
        return false;
    }

	lua_remove(State, -2); // Remove the traceback.
    return true;
}

bool LuaManager::IsValid()
{
    return State != nullptr;
}

bool LuaManager::Register(lua_CFunction Function, std::string FunctionName)
{
    if (!Function || FunctionName.empty())
        return false;
    lua_register(State, FunctionName.c_str(), Function);
    return true;
}

int LuaManager::GetGlobalI(std::string VariableName, int Default)
{
    int rval = Default;

    GetGlobal(VariableName);

    if (lua_isnumber(State, -1))
    {
        rval = lua_tonumber(State, -1);
    }

    Pop();
    return rval;
}

std::string LuaManager::GetGlobalS(std::string VariableName, std::string Default)
{
    std::string rval = Default;

    GetGlobal(VariableName);

    if (!lua_isnil(State, -1) && lua_isstring(State, -1))
    {
        const char* s = lua_tostring(State, -1);
        rval = s ? s : "";
    }

    Pop();
    return rval;
}

double LuaManager::GetGlobalD(std::string VariableName, double Default)
{
    double rval = Default;

    GetGlobal(VariableName);
    if (lua_isnumber(State, -1))
    {
        rval = lua_tonumber(State, -1);
    }

    Pop();
    return rval;
}

void LuaManager::SetGlobal(const std::string &VariableName, const std::string &Value)
{
    lua_pushstring(State, Value.c_str());
    lua_setglobal(State, VariableName.c_str());
}

void LuaManager::SetGlobal(const std::string &VariableName, const double &Value)
{
    lua_pushnumber(State, Value);
    lua_setglobal(State, VariableName.c_str());
}

bool LuaManager::RegisterStruct(std::string Key, void* data, std::string MetatableName)
{
    if (!data) return false;
    if (Key.length() < 1) return false;

    lua_pushstring(State, Key.c_str());
    lua_pushlightuserdata(State, data);

    if (MetatableName.length())
    {
        luaL_getmetatable(State, MetatableName.c_str());
        lua_setmetatable(State, -2);
    }

    lua_settable(State, LUA_REGISTRYINDEX);
    return true;
}

void* LuaManager::GetStruct(std::string Key)
{
    void* ptr = nullptr;
    lua_pushstring(State, Key.c_str());
    lua_gettable(State, LUA_REGISTRYINDEX);
    ptr = lua_touserdata(State, -1); // returns null if does not exist

    Pop();
    return ptr;
}

void LuaManager::NewArray()
{
    lua_newtable(State);
}

bool LuaManager::UseArray(std::string VariableName)
{
    GetGlobal(VariableName);
    if (lua_istable(State, -1))
        return true;

    Pop();
    return false;
}

void LuaManager::SetFieldI(int index, int Value)
{
    lua_pushnumber(State, Value);
    lua_rawseti(State, -2, index);
}

void LuaManager::SetFieldI(std::string name, int Value)
{
	lua_pushinteger(State, Value);
	lua_setfield(State, -2, name.c_str());
}

void LuaManager::SetFieldS(int index, std::string Value)
{
    lua_pushstring(State, Value.c_str());
    lua_rawseti(State, -2, index);
}

void LuaManager::SetFieldS(std::string name, std::string Value)
{
    lua_pushstring(State, Value.c_str());
    lua_setfield(State, -2, name.c_str());
}

void LuaManager::SetFieldD(int index, double Value)
{
    lua_pushnumber(State, Value);
    lua_rawseti(State, -2, index);
}

void LuaManager::SetFieldD(std::string name, double Value)
{
	lua_pushnumber(State, Value);
	lua_setfield(State, -2, name.c_str());
}

int LuaManager::GetFieldI(std::string Key, int Default)
{
    int R = Default;
    lua_pushstring(State, Key.c_str());
    lua_gettable(State, -2);

    if (lua_isnumber(State, -1))
    {
        R = lua_tonumber(State, -1);
    }// else Error

    Pop();
    return R;
}

double LuaManager::GetFieldD(std::string Key, double Default)
{
    double R = Default;

    if (lua_istable(State, -1))
    {
        lua_pushstring(State, Key.c_str());
        lua_gettable(State, -2);

        if (lua_isnumber(State, -1))
        {
            R = lua_tonumber(State, -1);
        }// else Error

        Pop();
        return R;
    }
    else
        return R;
}

std::string LuaManager::GetFieldS(std::string Key, std::string Default)
{
    std::string R = Default;

    lua_pushstring(State, Key.c_str());
    lua_gettable(State, -2);

    if (lua_isstring(State, -1))
    {
        R = lua_tostring(State, -1);
    }// else Error

    Pop();
    return R;
}

void LuaManager::Pop()
{
    lua_pop(State, 1);
}

void LuaManager::FinalizeArray(std::string ArrayName)
{
    lua_setglobal(State, ArrayName.c_str());
}

void LuaManager::FinalizeEnum(std::string EnumName)
{
	// create read-only metatable
	NewArray();
	lua_pushcfunction(State, LuaReadOnlyError);
	lua_setfield(State, -2, "__newindex");
	lua_setmetatable(State, -2);
	
	lua_setglobal(State, EnumName.c_str());
}

void LuaManager::AppendPath(std::string Path)
{
    GetGlobal("package");
    SetFieldS("path", GetFieldS("path") + ";" + Path);
    Pop();
}

void LuaManager::PushArgument(int Value)
{
    if (func_input)
        lua_pushnumber(State, Value);
}

void LuaManager::PushArgument(double Value)
{
    if (func_input)
        lua_pushnumber(State, Value);
}

void LuaManager::PushArgument(std::string Value)
{
    if (func_input)
        lua_pushstring(State, Value.c_str());
}

void LuaManager::PushArgument(bool Value)
{
	if (func_input)
		lua_pushboolean(State, Value);
}

int LuaManager::GetStackTop()
{
    return lua_gettop(State);
}

// http://lua-users.org/lists/lua-l/2006-03/msg00335.html
void LuaManager::DumpStack()
{
	auto L = State;
	int i = lua_gettop(L);
	/*Log::LogPrintf(" ----------------  Stack Dump ----------------\n");
	while (i) {
		int t = lua_type(L, i);
		switch (t) {
		case LUA_TSTRING:
			Log::LogPrintf("%d:`%s'\n", i, lua_tostring(L, i));
			break;
		case LUA_TBOOLEAN:
			Log::LogPrintf("%d: %s\n", i, lua_toboolean(L, i) ? "true" : "false");
			break;
		case LUA_TNUMBER:
			Log::LogPrintf("%d: %g\n", i, lua_tonumber(L, i));
			break;
		default: Log::LogPrintf("%d: %s\n", i, lua_typename(L, t)); break;
		}
		i--;
	}
	Log::LogPrintf("--------------- Stack Dump Finished ---------------\n");*/
}

bool LuaManager::CallFunction(const char* Name, int Arguments, int Results)
{
	bool IsFunc;

	func_args = Arguments;
	func_results = Results;

	bool isTable = lua_istable(State, -1);
	if (isTable) {
		lua_pushstring(State, Name);
		lua_gettable(State, -2);
	}
	else {
		lua_getglobal(State, Name);
	}
    IsFunc = lua_isfunction(State, -1);

    if (IsFunc)
        func_input = true;
	else {
		Pop();

		// this is generalizable, but i'm too lazy. -az
		if (isTable) {
			lua_getglobal(State, Name);

			IsFunc = lua_isfunction(State, -1);
			if (IsFunc)
			{
				func_input = true;
			}
			else
				Pop();
		}
	}

    return IsFunc;
}

bool LuaManager::RunFunction()
{
    if (!func_input)
        return false;
    func_input = false;

	int base = lua_gettop(State) - func_args;
	lua_pushcfunction(State, LuaPanic);
	lua_insert(State, base);

    int errc = lua_pcall(State, func_args, func_results, base);

    if (errc)
    {
        std::string reason = lua_tostring(State, -1);

		if (last_error != reason) {
			last_error = reason;
#ifndef WIN32
			printf("lua call error: %s\n", reason.c_str());
#else
			Log::LogPrintf("lua call error: %s\n", reason.c_str());
#endif
		}
		lua_remove(State, base); // remove traceback function
        Pop(); // Remove the error from the stack.
        func_err = true;
        return false;
    }

    func_err = false;
	lua_remove(State, base);
	// Remove traceback function.
    return true;
}

int LuaManager::GetFunctionResult(int StackPos)
{
    return GetFunctionResultD(StackPos);
}

std::string LuaManager::GetFunctionResultS(int StackPos)
{
	std::string Value;

	if (func_err) return Value;

	if (lua_isstring(State, -StackPos))
	{
		Value = lua_tostring(State, -StackPos);
	}

	Pop();
	return Value;
}

float LuaManager::GetFunctionResultF(int StackPos)
{
	return GetFunctionResultD(StackPos);
}

double LuaManager::GetFunctionResultD(int StackPos)
{
	double Value = -1;

	if (func_err) return 0;

	if (lua_isnumber(State, -StackPos))
	{
		Value = lua_tonumber(State, -StackPos);
	}

	Pop();
	return Value;
}

void LuaManager::NewMetatable(std::string MtName)
{
    luaL_newmetatable(State, MtName.c_str());
}

void LuaManager::RegisterLibrary(std::string Libname, const luaL_Reg *Reg)
{
    luaL_newlib(State, Reg);
    lua_setglobal(State, Libname.c_str());
}

lua_State* LuaManager::GetState()
{
    return State;
}

void LuaManager::StartIteration()
{
    lua_pushnil(State);
}

bool LuaManager::IterateNext()
{
    return lua_next(State, -2) != 0;
}

int LuaManager::NextInt()
{
    return lua_tonumber(State, -1);
}

double LuaManager::NextDouble()
{
    return lua_tonumber(State, -1);
}

std::string LuaManager::NextGString()
{
    return lua_tostring(State, -1);
}