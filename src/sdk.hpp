#pragma once

// Minimal Source engine SDK definitions needed to register a console command
// and force a client cvar. Reverse-engineering work and struct layouts are
// derived from SourceAutoRecord (https://github.com/p2sr/SourceAutoRecord, MIT).
// Targets Portal 2 (build 9568), 32-bit.

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// --- Platform ---------------------------------------------------------------

#ifdef _WIN32
#	include <windows.h>
#	include <psapi.h>
#	define __thiscallconv __thiscall
#	define DLL_EXPORT extern "C" __declspec(dllexport)
// Module names must match how the loader lists them: filename WITH extension.
#	define MODULE_VSTDLIB "vstdlib.dll"
#	define MODULE_TIER0 "tier0.dll"
#else
#	include <dlfcn.h>
#	include <link.h>
#	define __thiscallconv __attribute__((__cdecl__))
#	define DLL_EXPORT extern "C" __attribute__((visibility("default")))
#	define MODULE_VSTDLIB "libvstdlib.so"
#	define MODULE_TIER0 "libtier0.so"
#	ifndef MAX_PATH
#		define MAX_PATH 4096
#	endif
#endif

// tier0 print symbols (mangled on Windows)
#ifdef _WIN32
#	define CONCOLORMSG_SYMBOL "?ConColorMsg@@YAXABVColor@@PBDZZ"
#else
#	define CONCOLORMSG_SYMBOL "_Z11ConColorMsgRK5ColorPKcz"
#endif
#define MSG_SYMBOL "Msg"

// --- Vtable offsets (Portal 2 9568) -----------------------------------------

namespace Offsets {
#ifdef _WIN32
	constexpr int CreateInterfaceInternal = 5;
	constexpr int s_pInterfaceRegs = 6;
	constexpr int RegisterConCommand = 9;
	constexpr int UnregisterConCommand = 10;
	constexpr int FindCommandBase = 13;
	constexpr int InternalSetIntValue = 14;
	constexpr int Dtor = 9;
	constexpr int Create = 27;
#else
	constexpr int CreateInterfaceInternal = 5;
	constexpr int s_pInterfaceRegs = 11;
	constexpr int RegisterConCommand = 9;
	constexpr int UnregisterConCommand = 10;
	constexpr int FindCommandBase = 13;
	constexpr int InternalSetIntValue = 21;
	constexpr int Dtor = 0;
	constexpr int Create = 25;
#endif
}  // namespace Offsets

// --- Color ------------------------------------------------------------------

struct Color {
	Color()
		: Color(0, 0, 0) {}
	Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
		: r(r), g(g), b(b), a(a) {}
	uint8_t r, g, b, a;
};

// --- CUtlVector (layout-compatible with the game's ConVar callback list) ----

template <class T>
struct CUtlMemory {
	T *m_pMemory;
	int m_nAllocationCount;
	int m_nGrowSize;
};

template <class T>
struct CUtlVector {
	CUtlMemory<T> m_Memory;
	int m_Size;
	T *m_pElements;

	void Append(const T &val) {
		if (this->m_Size == this->m_Memory.m_nAllocationCount) {
			int grow = this->m_Memory.m_nGrowSize ? this->m_Memory.m_nGrowSize : 1;
			this->m_Memory.m_nAllocationCount += grow;
			this->m_Memory.m_pMemory = static_cast<T *>(realloc(this->m_Memory.m_pMemory, sizeof(T) * this->m_Memory.m_nAllocationCount));
			this->m_pElements = this->m_Memory.m_pMemory;
		}
		this->m_Memory.m_pMemory[this->m_Size] = val;
		this->m_Size++;
	}
};

// --- ConCommand / ConVar ----------------------------------------------------

#define FCVAR_NONE 0
#define FCVAR_ARCHIVE (1 << 7)  // saved to config.cfg, restored on launch

struct CCommand;
struct ConCommandBase;

typedef void (*FnChangeCallback_t)(void *var, const char *pOldValue, float flOldValue);
using _CommandCallback = void (*)(const CCommand &args);
using _CommandCompletionCallback = int (*)(const char *partial, char commands[64][64]);
using _InternalSetIntValue = void(__thiscallconv *)(void *thisptr, int value);
using _RegisterConCommand = void(__thiscallconv *)(void *thisptr, ConCommandBase *pCommandBase);
using _UnregisterConCommand = void(__thiscallconv *)(void *thisptr, ConCommandBase *pCommandBase);
using _FindCommandBase = void *(__thiscallconv *)(void *thisptr, const char *name);
using _Create = int(__thiscallconv *)(void *thisptr, const char *name, const char *def, int flags, const char *help, bool bMin, float fMin, bool bMax, float fMax, FnChangeCallback_t cb);
#ifdef _WIN32
using _Dtor = int(__thiscallconv *)(void *thisptr, char a2);
#else
using _Dtor = int(__thiscallconv *)(void *thisptr);
#endif

struct ConCommandBase {
	ConCommandBase(const char *name, int flags, const char *helpstr)
		: m_pNext(nullptr)
		, m_bRegistered(false)
		, m_pszName(name)
		, m_pszHelpString(helpstr)
		, m_nFlags(flags) {}

	// Dummy virtuals so the object has a vtable slot we later overwrite with
	// the game's real ConCommand vtable. A real virtual destructor breaks the
	// layout, so we use placeholders.
	virtual void _dtor() {}
#ifndef _WIN32
	virtual void _dtor1() {}
#endif
	virtual bool IsCommand() const { return false; }

	ConCommandBase *m_pNext;
	bool m_bRegistered;
	const char *m_pszName;
	const char *m_pszHelpString;
	int m_nFlags;
};

struct CCommand {
	enum { COMMAND_MAX_ARGC = 64, COMMAND_MAX_LENGTH = 512 };
	int m_nArgc;
	int m_nArgv0Size;
	char m_pArgSBuffer[COMMAND_MAX_LENGTH];
	char m_pArgvBuffer[COMMAND_MAX_LENGTH];
	const char *m_ppArgv[COMMAND_MAX_ARGC];

	int ArgC() const { return this->m_nArgc; }
	const char *Arg(int i) const { return this->m_ppArgv[i]; }
	const char *operator[](int i) const { return Arg(i); }
};

struct ConCommand : ConCommandBase {
	union {
		void *m_fnCommandCallbackV1;
		_CommandCallback m_fnCommandCallback;
		void *m_pCommandCallback;
	};
	union {
		_CommandCompletionCallback m_fnCompletionCallback;
		void *m_pCommandCompletionCallback;
	};
	bool m_bHasCompletionCallback : 1;
	bool m_bUsingNewCommandCallback : 1;
	bool m_bUsingCommandCallbackInterface : 1;

	ConCommand(const char *pName, _CommandCallback callback, const char *pHelpString, int flags = 0)
		: ConCommandBase(pName, flags, pHelpString)
		, m_fnCommandCallback(callback)
		, m_fnCompletionCallback(nullptr)
		, m_bHasCompletionCallback(false)
		, m_bUsingNewCommandCallback(true)
		, m_bUsingCommandCallbackInterface(false) {}
};

struct ConVar : ConCommandBase {
	void *ConVar_VTable;                                // 24
	ConVar *m_pParent;                                  // 28
	const char *m_pszDefaultValue;                      // 32
	char *m_pszString;                                  // 36
	int m_StringLength;                                 // 40
	float m_fValue;                                     // 44
	int m_nValue;                                       // 48
	bool m_bHasMin;                                     // 52
	float m_fMinVal;                                    // 56
	bool m_bHasMax;                                     // 60
	float m_fMaxVal;                                    // 64
	CUtlVector<FnChangeCallback_t> m_fnChangeCallback;  // 68

	ConVar(const char *name, const char *value, int flags, const char *helpstr)
		: ConCommandBase(name, flags, helpstr)
		, ConVar_VTable(nullptr)
		, m_pParent(nullptr)
		, m_pszDefaultValue(value)
		, m_pszString(nullptr)
		, m_StringLength(0)
		, m_fValue(0.0f)
		, m_nValue(0)
		, m_bHasMin(false)
		, m_fMinVal(0)
		, m_bHasMax(false)
		, m_fMaxVal(0)
		, m_fnChangeCallback() {}
};

// --- Plugin interface -------------------------------------------------------

#define INTERFACEVERSION_ISERVERPLUGINCALLBACKS "ISERVERPLUGINCALLBACKS002"

typedef void *(*CreateInterfaceFn)(const char *pName, int *pReturnCode);
typedef void *(*InstantiateInterfaceFn)();

struct InterfaceReg {
	InstantiateInterfaceFn m_CreateFn;
	const char *m_pName;
	InterfaceReg *m_pNext;
	static InterfaceReg *s_pInterfaceRegs;

	InterfaceReg(InstantiateInterfaceFn fn, const char *pName)
		: m_CreateFn(fn), m_pName(pName) {
		m_pNext = s_pInterfaceRegs;
		s_pInterfaceRegs = this;
	}
};

class IServerPluginCallbacks {
public:
	virtual bool Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory) = 0;
	virtual void Unload() = 0;
	virtual void Pause() = 0;
	virtual void UnPause() = 0;
	virtual const char *GetPluginDescription() = 0;
	virtual void LevelInit(char const *pMapName) = 0;
	virtual void ServerActivate(void *pEdictList, int edictCount, int clientMax) = 0;
	virtual void GameFrame(bool simulating) = 0;
	virtual void LevelShutdown() = 0;
	virtual void ClientFullyConnect(void *pEdict) = 0;
	virtual void ClientActive(void *pEntity) = 0;
	virtual void ClientDisconnect(void *pEntity) = 0;
	virtual void ClientPutInServer(void *pEntity, char const *playername) = 0;
	virtual void SetCommandClient(int index) = 0;
	virtual void ClientSettingsChanged(void *pEdict) = 0;
	virtual int ClientConnect(bool *bAllowConnect, void *pEntity, const char *pszName, const char *pszAddress, char *reject, int maxrejectlen) = 0;
	virtual int ClientCommand(void *pEntity, const void *&args) = 0;
	virtual int NetworkIDValidated(const char *pszUserName, const char *pszNetworkID) = 0;
	virtual void OnQueryCvarValueFinished(int iCookie, void *pPlayerEntity, int eStatus, const char *pCvarName, const char *pCvarValue) = 0;
	virtual void OnEdictAllocated(void *edict) = 0;
	virtual void OnEdictFreed(const void *edict) = 0;
};

#define EXPOSE_SINGLE_INTERFACE_GLOBALVAR(className, interfaceName, versionName, globalVarName)                          \
	static void *__Create##className##interfaceName##_interface() { return static_cast<interfaceName *>(&globalVarName); } \
	static InterfaceReg __g_Create##className##interfaceName##_reg(__Create##className##interfaceName##_interface, versionName);

// --- Memory helpers ---------------------------------------------------------

namespace Memory {
	struct ModuleInfo {
		char name[MAX_PATH];
		uintptr_t base;
		uintptr_t size;
		char path[MAX_PATH];
	};

	bool TryGetModule(const char *moduleName, ModuleInfo *info);
	void *GetModuleHandleByName(const char *moduleName);
	void CloseModuleHandle(void *moduleHandle);

	template <typename T = void *>
	inline T GetSymbolAddress(void *moduleHandle, const char *symbolName) {
#ifdef _WIN32
		return (T)GetProcAddress((HMODULE)moduleHandle, symbolName);
#else
		return (T)dlsym(moduleHandle, symbolName);
#endif
	}
	template <typename T = void *>
	inline T VMT(void *ptr, int index) {
		return reinterpret_cast<T>((*((void ***)ptr))[index]);
	}
	template <typename T = uintptr_t>
	inline T Read(uintptr_t source) {
		auto rel = *reinterpret_cast<int *>(source);
		return (T)(source + rel + sizeof(rel));
	}
	template <typename T = void *>
	inline T Deref(uintptr_t source) {
		return *reinterpret_cast<T *>(source);
	}
	template <typename T = void *>
	inline T DerefDeref(uintptr_t source) {
		return Memory::Deref<T>(Memory::Deref<uintptr_t>(source));
	}
}  // namespace Memory
