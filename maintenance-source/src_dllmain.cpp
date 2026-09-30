#include "overlay.h"
#include "log.h"
#include "player_runtime.h"
#include "Core/settings.h"
#include "Game/build_detector.h"
#include "Signatures/signature_database.h"
#include "runtime_core.h"
#include "Debug/reload_trace.h"
#include "Debug/entry_trace.h"
#include "Debug/chain_permission_trace.h"
#include "Classes/brute_action_force.h"
#include <Windows.h>
#include <filesystem>

namespace {
DWORD WINAPI InitThread(void* parameter) {
    const auto module=static_cast<HMODULE>(parameter);
    wchar_t modulePath[32768]{};
    GetModuleFileNameW(module,modulePath,32768);
    const auto suffix=std::to_wstring(GetCurrentProcessId())+L"."+std::filesystem::path(modulePath).filename().wstring();
    const auto stopName=L"Local\\FactionOverhaul.Stop."+suffix;
    const auto doneName=L"Local\\FactionOverhaul.Stopped."+suffix;
    HANDLE stopEvent=CreateEventW(nullptr,TRUE,FALSE,stopName.c_str());
    HANDLE doneEvent=CreateEventW(nullptr,TRUE,FALSE,doneName.c_str());
    try {
        ac4fo::paths::Initialize(module);
        ac4fo::log::Initialize();
        if(!ac4fo::GameBuildDetector::Instance().Initialize()) {
            FreeLibraryAndExitThread(module,1);
        }
        // Callbacks/trampolines stay mapped until process exit, including callbacks in flight.
        HMODULE pinned{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&InitThread),&pinned)) return 2;
        ac4fo::RuntimeCore::Instance().Initialize();
        ac4fo::SignatureDatabase::Instance().Rescan();
        ac4fo::PlayerRuntime::Instance().Initialize();
        if(!ac4fo::overlay::Install()) { ac4fo::log::Write("[ERROR] Overlay installation failed"); return 3; }
        ac4fo::research::InstallReloadTrace();
        ac4fo::research::InstallChainPermissionTrace();
        ac4fo::InstallBruteActionForce();
        const auto name=L"Local\\FactionOverhaul.Ready."+std::to_wstring(GetCurrentProcessId());
        if(HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str())) { SetEvent(ready); CloseHandle(ready); }
        while(!ac4fo::overlay::Stopped()) {
            if(stopEvent && WaitForSingleObject(stopEvent,250)==WAIT_OBJECT_0) {
                ac4fo::overlay::Shutdown();
                Sleep(50);
            } else if(!stopEvent) Sleep(250);
            ac4fo::research::PollReloadTrace();
            ac4fo::research::PollEntryTrace();
            ac4fo::research::PollChainPermissionTrace();
            ac4fo::PollBruteActionForce();
            ac4fo::overlay::ShutdownIfWindowClosed();
        }
        // Acquire the rendering lock after Stopped was published by Cleanup.
        // Older code and trampolines remain pinned; never FreeLibrary here.
        if(!ac4fo::overlay::ShutdownCompleted()) return 4;
        ac4fo::research::StopReloadTrace();
        ac4fo::research::StopEntryTrace();
        ac4fo::research::StopChainPermissionTrace();
        ac4fo::StopBruteActionForce();
        ac4fo::log::Write("[HOTUPDATE] Old generation hooks stopped; code retained until game exit.");
        if(doneEvent) SetEvent(doneEvent);
    } catch(const std::exception& e) { ac4fo::log::Write(std::string("[ERROR] Initialization: ")+e.what()); }
    // Keep event handles alive with this pinned generation, allowing the loader
    // to distinguish an already stopped generation from a legacy DLL.
    return 0;
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (HANDLE thread = CreateThread(nullptr, 0, InitThread, module, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
