// fovmod - a minimal Portal 2 plugin implementing a single cvar:
//
//     fov <value>   - force the player FOV to <value> (45-140), 0 to disable
//                     persists across sessions (saved to fovmod.cfg)
//
// This is a stripped-down fork of SourceAutoRecord (https://github.com/p2sr/
// SourceAutoRecord, MIT). All the engine plumbing (struct layouts, vtable
// offsets, the cvar-forcing technique) is derived from that project; everything
// unrelated to FOV forcing has been removed.

#include "sdk.hpp"

#include <cstdarg>
#include <fstream>

// --- Exported interface registration (engine entry point) -------------------

InterfaceReg *InterfaceReg::s_pInterfaceRegs = nullptr;

DLL_EXPORT void *CreateInterface(const char *pName, int *pReturnCode) {
	for (InterfaceReg *cur = InterfaceReg::s_pInterfaceRegs; cur; cur = cur->m_pNext) {
		if (!std::strcmp(cur->m_pName, pName)) {
			if (pReturnCode) *pReturnCode = 0;
			return cur->m_CreateFn();
		}
	}
	if (pReturnCode) *pReturnCode = 1;
	return nullptr;
}

// --- Console output ---------------------------------------------------------

using _Msg = void (*)(const char *fmt, ...);
using _ColorMsg = void (*)(const Color &clr, const char *fmt, ...);

static _Msg g_Msg = nullptr;
static _ColorMsg g_ColorMsg = nullptr;
static const Color FOV_COLOR(247, 214, 68);

static void InitConsole() {
	if (void *tier0 = Memory::GetModuleHandleByName(MODULE_TIER0)) {
		g_Msg = Memory::GetSymbolAddress<_Msg>(tier0, MSG_SYMBOL);
		g_ColorMsg = Memory::GetSymbolAddress<_ColorMsg>(tier0, CONCOLORMSG_SYMBOL);
		Memory::CloseModuleHandle(tier0);
	}
}

static void Print(const char *fmt, ...) {
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (g_ColorMsg) g_ColorMsg(FOV_COLOR, "%s", buf);
	else if (g_Msg) g_Msg("%s", buf);
}

// --- Cvar interface ---------------------------------------------------------

static void *GetCvarInterface() {
	void *handle = Memory::GetModuleHandleByName(MODULE_VSTDLIB);
	if (!handle) return nullptr;

	auto CreateInterfaceFromVStdlib = Memory::GetSymbolAddress<CreateInterfaceFn>(handle, "CreateInterface");
	Memory::CloseModuleHandle(handle);
	if (!CreateInterfaceFromVStdlib) return nullptr;

	int ret = 0;
	void *fn = CreateInterfaceFromVStdlib("VEngineCvar007", &ret);
	if (ret) {
		// The easy path failed; walk the interface registration list directly.
		uintptr_t internal = Memory::Read((uintptr_t)CreateInterfaceFromVStdlib + Offsets::CreateInterfaceInternal);
		InterfaceReg *regs = internal ? Memory::DerefDeref<InterfaceReg *>(internal + Offsets::s_pInterfaceRegs) : nullptr;
		for (InterfaceReg *cur = regs; cur; cur = cur->m_pNext) {
			if (!std::strncmp(cur->m_pName, "VEngineCvar007", std::strlen("VEngineCvar007"))) {
				fn = cur->m_CreateFn();
				break;
			}
		}
	}
	return fn;
}

// --- FOV forcing ------------------------------------------------------------

static void *g_pCVar = nullptr;
static ConVar *g_cl_fov = nullptr;
static int g_forcedFov = 0;  // 0 = not forcing

static bool g_fovHooked = false;
static CUtlVector<FnChangeCallback_t> g_origFovCallbacks;

static bool g_cvarRegistered = false;
static void *g_convarVtable = nullptr;   // ConVar primary vtable (stolen from the game)
static void *g_convarVtable2 = nullptr;  // ConVar secondary vtable (IConVar)

static void ForceFov() {
	if (g_forcedFov == 0 || !g_cl_fov) return;
	if (g_cl_fov->m_nValue != g_forcedFov) {
		Memory::VMT<_InternalSetIntValue>(g_cl_fov, Offsets::InternalSetIntValue)(g_cl_fov, g_forcedFov);
	}
}

// Re-applied by the engine whenever something tries to change cl_fov, which is
// what keeps the forced value sticky across map loads, configs, etc.
static void cl_fov_callback(void *, const char *, float) {
	ForceFov();
}

// Persistence is handled with a small state file next to the plugin rather than
// FCVAR_ARCHIVE/config.cfg: Portal 2 loads addon plugins *after* it execs
// config.cfg, so an archived value would be restored before this cvar exists and
// would be silently dropped. We read this file on load and write it on change.
static const char *kStateFile = "fovmod.cfg";

static void SaveFov(int fov) {
	std::ofstream f(kStateFile, std::ios::trunc);
	if (f) f << fov << "\n";
}
static int LoadSavedFov() {
	std::ifstream f(kStateFile);
	int fov = 0;
	return (f >> fov) ? fov : 0;
}

static ConVar fov_cvar("fov", "0", FCVAR_NONE,
	"fov <value> - forces player FOV to <value> (45-140), 0 to disable. Saved across sessions.\n");

// Fires whenever fov changes (console, or our own restore-on-load).
static void fov_callback(void *, const char *, float) {
	int fov = fov_cvar.m_nValue;

	if (fov == 0) {
		g_forcedFov = 0;
		SaveFov(0);
		return;
	}

	if (fov < 45 || fov > 140) {
		Print("fov: value must be 45-140 (or 0 to disable)\n");
		g_forcedFov = 0;
		// Reset to 0; this re-fires the callback with 0, which disables and saves.
		Memory::VMT<_InternalSetIntValue>(&fov_cvar, Offsets::InternalSetIntValue)(&fov_cvar, 0);
		return;
	}

	g_forcedFov = fov;
	ForceFov();
	SaveFov(fov);
}

// --- Plugin -----------------------------------------------------------------

class FovPlugin : public IServerPluginCallbacks {
public:
	virtual bool Load(CreateInterfaceFn, CreateInterfaceFn) override {
		InitConsole();

		g_pCVar = GetCvarInterface();
		if (!g_pCVar) {
			Print("fovmod: failed to get cvar interface!\n");
			return false;
		}

		auto FindCommandBase = Memory::VMT<_FindCommandBase>(g_pCVar, Offsets::FindCommandBase);

		// Register the fov ConVar. ConVars need the game's real
		// vtables, so steal them from an existing ConVar (sv_lan).
		auto sv_lan = reinterpret_cast<ConVar *>(FindCommandBase(g_pCVar, "sv_lan"));
		if (!sv_lan) {
			Print("fovmod: failed to find a cvar to source a vtable from!\n");
			return false;
		}
		g_convarVtable = *(void **)sv_lan;
		g_convarVtable2 = sv_lan->ConVar_VTable;

		void *createVt =
#ifdef _WIN32
			g_convarVtable2;
#else
			g_convarVtable;
#endif
		auto Create = Memory::VMT<_Create>(&createVt, Offsets::Create);

		*(void **)&fov_cvar = g_convarVtable;
		fov_cvar.ConVar_VTable = g_convarVtable2;
		Create(&fov_cvar, fov_cvar.m_pszName, fov_cvar.m_pszDefaultValue,
			fov_cvar.m_nFlags, fov_cvar.m_pszHelpString,
			fov_cvar.m_bHasMin, fov_cvar.m_fMinVal,
			fov_cvar.m_bHasMax, fov_cvar.m_fMaxVal, fov_callback);
		g_cvarRegistered = true;

		// Hook cl_fov's change callback so our forced value is re-applied.
		g_cl_fov = reinterpret_cast<ConVar *>(FindCommandBase(g_pCVar, "cl_fov"));
		if (g_cl_fov) {
			g_origFovCallbacks = g_cl_fov->m_fnChangeCallback;
			g_cl_fov->m_fnChangeCallback.Append(cl_fov_callback);
			g_fovHooked = true;
		} else {
			Print("fovmod: warning - cl_fov not found, forcing will not work.\n");
		}

		// Restore the persisted FOV now that the cvar exists. Setting it fires
		// fov_callback, which arms forcing.
		int saved = LoadSavedFov();
		if (saved >= 45 && saved <= 140) {
			Memory::VMT<_InternalSetIntValue>(&fov_cvar, Offsets::InternalSetIntValue)(&fov_cvar, saved);
		}

		Print("fovmod loaded. Use: fov <value>\n");
		return true;
	}

	virtual void Unload() override {
		g_forcedFov = 0;
		if (g_fovHooked && g_cl_fov) {
			g_cl_fov->m_fnChangeCallback = g_origFovCallbacks;
			g_fovHooked = false;
		}
		if (g_pCVar && g_cvarRegistered) {
			auto UnregisterConCommand = Memory::VMT<_UnregisterConCommand>(g_pCVar, Offsets::UnregisterConCommand);
			UnregisterConCommand(g_pCVar, &fov_cvar);

			void *dtorVt =
#ifdef _WIN32
				g_convarVtable2;
#else
				g_convarVtable;
#endif
			auto Dtor = Memory::VMT<_Dtor>(&dtorVt, Offsets::Dtor);
#ifdef _WIN32
			Dtor(&fov_cvar, 0);
#else
			Dtor(&fov_cvar);
#endif
			g_cvarRegistered = false;
		}
	}

	virtual const char *GetPluginDescription() override { return "fovmod"; }

	// Unused callbacks
	virtual void Pause() override {}
	virtual void UnPause() override {}
	virtual void LevelInit(char const *) override {}
	virtual void ServerActivate(void *, int, int) override {}
	virtual void GameFrame(bool) override {}
	virtual void LevelShutdown() override {}
	virtual void ClientFullyConnect(void *) override {}
	virtual void ClientActive(void *) override {}
	virtual void ClientDisconnect(void *) override {}
	virtual void ClientPutInServer(void *, char const *) override {}
	virtual void SetCommandClient(int) override {}
	virtual void ClientSettingsChanged(void *) override {}
	virtual int ClientConnect(bool *, void *, const char *, const char *, char *, int) override { return 0; }
	virtual int ClientCommand(void *, const void *&) override { return 0; }
	virtual int NetworkIDValidated(const char *, const char *) override { return 0; }
	virtual void OnQueryCvarValueFinished(int, void *, int, const char *, const char *) override {}
	virtual void OnEdictAllocated(void *) override {}
	virtual void OnEdictFreed(const void *) override {}
};

static FovPlugin g_plugin;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(FovPlugin, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS, g_plugin);
