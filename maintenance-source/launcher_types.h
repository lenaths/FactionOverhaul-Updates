#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace ac4fo::launcher {

inline constexpr const char* kLauncherVersion = "1.1.0";
inline constexpr const char* kDefaultUpdateManifestUrl = "https://raw.githubusercontent.com/lenaths/FactionOverhaul-Updates/main/update.json";

struct LauncherConfig {
    std::string updateManifestUrl{kDefaultUpdateManifestUrl};
    bool autoCheckUpdates{true};
    bool autoInstallUpdates{true};
    bool autoReloadRuntimeAfterUpdate{true};
    int checkIntervalMinutes{30};
    std::filesystem::path gameExecutable;
};

struct RuntimePackage {
    std::string version{"0.10.0"};
    std::filesystem::path root;
    std::filesystem::path dll;
    std::filesystem::path injector;
};

struct LauncherProfile {
    int nation{0};
    int playerClass{1};
    int ship{3};
    bool authenticMoveset{true};
    bool authenticWeapons{true};
    bool authenticLocomotion{true};
    bool authenticFinishers{true};
    bool authenticHitReactions{true};
    bool customLoadout{false};
    bool disableChainKills{false};
    bool factionSoldiersAllies{true};
    bool factionShipsAllies{true};
    bool factionCrew{true};
    bool factionRecruits{true};
    bool factionFlags{true};
    bool capturedOutposts{false};
    bool missionAutoDisable{true};
    bool missionAutoRestore{true};
    bool keepInCutscenes{false};
    bool showNotification{false};
};


struct LauncherReleaseManifest {
    std::string version;
    std::string packageUrl;
    std::string sha256;
    std::string notes;
    std::string publishedUtc;
};

struct LauncherUpdateSnapshot {
    enum class Phase { Idle, Checking, Available, Downloading, Installing, Ready, Error };
    Phase phase{Phase::Idle};
    std::string currentVersion{kLauncherVersion};
    std::string remoteVersion;
    std::string status;
    std::string notes;
    float progress{0.0f};
    bool updateAvailable{false};
    bool restartScheduled{false};
};

struct UpdateManifest {
    std::string version;
    std::string packageUrl;
    std::string sha256;
    std::string notes;
    std::string publishedUtc;
    std::string minimumLauncherVersion;
};

struct UpdateSnapshot {
    enum class Phase { Idle, Checking, Available, Downloading, Installing, Ready, Error };
    Phase phase{Phase::Idle};
    std::string currentVersion;
    std::string remoteVersion;
    std::string status;
    std::string notes;
    float progress{0.0f};
    bool updateAvailable{false};
};

} // namespace ac4fo::launcher
