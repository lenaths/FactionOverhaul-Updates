#pragma once
#include "profile.h"
#include <mutex>
#include <string>
#include <optional>
#include "Core/settings.h"
#include "Memory/modification_registry.h"
#include "Missions/mission_safety.h"
#include "Appearance/appearance_adapter.h"

namespace ac4fo {

class RuntimeCore {
public:
    static RuntimeCore& Instance();

    Profile& SelectedProfile();
    const Profile& SelectedProfile() const;

    bool ApplySelectedProfile();
    void ResetVanilla();
    void RestoreRuntimeForShutdown();
    std::string Status() const;
    void Initialize();
    bool SaveSelectedProfile();
    bool LoadProfileFile(const std::filesystem::path& path);
    std::optional<Profile> ActiveProfile() const;
    std::optional<Profile> RequestedProfile() const;
    std::vector<std::string> Unavailable() const;
    Settings& ModSettings() { return settings_; }
    bool PersistSettings();
    void OnMissionState(MissionState state);
    MissionState Mission() const;
    std::size_t ModificationCount() const { return registry_.Count(); }

private:
    RuntimeCore() = default;
    void SetStatus(std::string value);

    mutable std::mutex mutex_;
    Profile profile_{};
    std::optional<Profile> requestedProfile_, activeProfile_, suspendedProfile_;
    Settings settings_;
    ModificationRegistry registry_;
    AppearanceRuntimeAdapter& appearance_{AppearanceRuntimeAdapter::Instance()};
    MissionSafetyManager safety_;
    std::vector<std::string> unavailable_;
    std::string status_{"Vanilla active. Select a profile to begin."};
};

}
