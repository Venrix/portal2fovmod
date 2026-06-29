#include "sdk.hpp"

#include <string>
#include <vector>

// Module enumeration, adapted from SourceAutoRecord (MIT).

static std::vector<Memory::ModuleInfo> g_moduleList;

bool Memory::TryGetModule(const char *moduleName, Memory::ModuleInfo *info) {
	if (g_moduleList.empty()) {
#ifdef _WIN32
		HMODULE hMods[1024];
		HANDLE pHandle = GetCurrentProcess();
		DWORD cbNeeded;
		if (EnumProcessModules(pHandle, hMods, sizeof(hMods), &cbNeeded)) {
			for (unsigned i = 0; i < (cbNeeded / sizeof(HMODULE)); ++i) {
				char buffer[MAX_PATH];
				if (!GetModuleFileName(hMods[i], buffer, sizeof(buffer))) continue;

				MODULEINFO modinfo;
				if (!GetModuleInformation(pHandle, hMods[i], &modinfo, sizeof(modinfo))) continue;

				Memory::ModuleInfo module;
				std::string temp(buffer);
				auto index = temp.find_last_of("\\/");
				temp = temp.substr(index + 1);

				std::snprintf(module.name, sizeof(module.name), "%s", temp.c_str());
				module.base = (uintptr_t)modinfo.lpBaseOfDll;
				module.size = (uintptr_t)modinfo.SizeOfImage;
				std::snprintf(module.path, sizeof(module.path), "%s", buffer);
				g_moduleList.push_back(module);
			}
		}
#else
		dl_iterate_phdr([](struct dl_phdr_info *info, size_t, void *) {
			std::string temp(info->dlpi_name);
			auto index = temp.find_last_of("\\/");
			temp = temp.substr(index + 1);

			for (int i = 0; i < info->dlpi_phnum; ++i) {
				if (info->dlpi_phdr[i].p_flags & 1) {  // executable segment
					Memory::ModuleInfo module;
					module.base = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
					module.size = info->dlpi_phdr[i].p_memsz;
					std::strncpy(module.name, temp.c_str(), sizeof(module.name) - 1);
					module.name[sizeof(module.name) - 1] = 0;
					std::strncpy(module.path, info->dlpi_name, sizeof(module.path) - 1);
					module.path[sizeof(module.path) - 1] = 0;
					g_moduleList.push_back(module);
					break;
				}
			}
			return 0;
		}, nullptr);
#endif
	}

	for (auto &item : g_moduleList) {
		if (!std::strcmp(item.name, moduleName)) {
			if (info) *info = item;
			return true;
		}
	}
	return false;
}

void *Memory::GetModuleHandleByName(const char *moduleName) {
	Memory::ModuleInfo info;
#ifdef _WIN32
	return Memory::TryGetModule(moduleName, &info) ? GetModuleHandleA(info.path) : nullptr;
#else
	return Memory::TryGetModule(moduleName, &info) ? dlopen(info.path, RTLD_NOLOAD | RTLD_NOW) : nullptr;
#endif
}

void Memory::CloseModuleHandle(void *moduleHandle) {
#ifndef _WIN32
	if (moduleHandle) dlclose(moduleHandle);
#endif
}
