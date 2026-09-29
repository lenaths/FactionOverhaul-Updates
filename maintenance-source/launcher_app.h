#pragma once
#include "launcher_types.h"
#include "update_service.h"
#include "self_update_service.h"
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

struct ImFont;

namespace ac4fo::launcher {

class LauncherApp {
public:
    explicit LauncherApp(std::filesystem::path launcherDir);
    ~LauncherApp();

    void Initialize();
    void Tick();
    void Draw();

    bool WantsClose() const { return wantsClose_; }
    void RequestClose() { wantsClose_=true; }

private:
    void Log(const std::string& message, const char* level="INFO");
    void RefreshEnvironment();
    void DrawHeader();
    void DrawLeftStatus(float width);
    void DrawCenter(float width);
    void DrawRightProfile(float width);
    void DrawRuntimeLog(float height);
    void DrawSettingsModal();
    void DrawProfilesModal();
    void DrawAboutModal();

    bool BigButton(const char* id, const char* title, const char* subtitle, bool primary=false, bool enabled=true);
    void StatusCard(const char* title, const std::string& value, const std::string& detail, bool good, bool accent=false);
    void SaveConfig();
    void SaveProfile(bool reloadRuntime);
    void CheckUpdates(bool userRequested);
    void StartUpdateCycle();
    void InjectCurrent(bool reload);

    std::filesystem::path launcherDir_;
    std::filesystem::path configPath_;
    LauncherConfig config_;
    RuntimePackage runtime_;
    LauncherProfile profile_;

    std::vector<std::string> logs_;
    mutable std::mutex logsMutex_;
    UpdateService updater_;
    SelfUpdateService selfUpdater_;
    unsigned long gamePid_{0};
    bool runtimeReady_{false};
    std::filesystem::path gameExe_;
    bool wantsClose_{false};
    bool settingsOpen_{false};
    bool profilesOpen_{false};
    bool aboutOpen_{false};
    bool firstUpdateCheckDone_{false};
    bool updateCycleActive_{false};
    bool pendingRuntimeCheckAfterSelf_{false};
    bool runtimeCheckInFlight_{false};
    long long lastEnvRefreshMs_{0};
    long long lastAutoCheckMs_{0};

    char manifestUrlBuf_[1024]{};
    char gamePathBuf_[1024]{};
};

} // namespace ac4fo::launcher
