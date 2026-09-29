#pragma once
#include "launcher_types.h"
#include <filesystem>
#include <future>
#include <functional>
#include <mutex>

namespace ac4fo::launcher {

class SelfUpdateService {
public:
    using Logger = std::function<void(const std::string&)>;
    SelfUpdateService(std::filesystem::path launcherDir, Logger logger);
    ~SelfUpdateService();

    void SetConfig(LauncherConfig config);
    LauncherUpdateSnapshot Snapshot() const;
    bool Busy() const;
    bool RestartScheduled() const;
    void CheckAsync(bool installWhenAvailable);

private:
    void CheckWorker(bool installWhenAvailable);
    void InstallWorker(LauncherReleaseManifest manifest);
    bool PrepareAndLaunchUpdater(const LauncherReleaseManifest& manifest, std::string& error);
    void SetState(LauncherUpdateSnapshot::Phase phase, const std::string& status);

    std::filesystem::path launcherDir_;
    Logger logger_;
    mutable std::mutex mutex_;
    LauncherConfig config_;
    LauncherReleaseManifest pending_;
    LauncherUpdateSnapshot snapshot_;
    std::future<void> worker_;
};

} // namespace ac4fo::launcher
