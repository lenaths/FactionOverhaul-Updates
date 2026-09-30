#include "runtime_core.h"
#include "log.h"
#include "Game/build_detector.h"
#include "Classes/class_adapter.h"
#include "Classes/chain_kill_rule.h"
#include "Profiles/profile_request.h"
#include <sstream>

namespace ac4fo {

RuntimeCore& RuntimeCore::Instance() {
    static RuntimeCore instance;
    return instance;
}

Profile& RuntimeCore::SelectedProfile() { return profile_; }
const Profile& RuntimeCore::SelectedProfile() const { return profile_; }

void RuntimeCore::SetStatus(std::string value) {
    std::scoped_lock lock(mutex_);
    status_ = std::move(value);
    log::Write("[INFO] "+status_);
}

std::string RuntimeCore::Status() const {
    std::scoped_lock lock(mutex_);
    return status_;
}

bool RuntimeCore::ApplySelectedProfile() {
    const Profile requested=profile_;
    std::string error;
    if (!ValidateProfile(requested,error)) { SetStatus("Apply refused: "+error); return false; }
    log::Write("[INFO] Profile requested: "+std::string(NationName(requested.nation))+" / "+std::string(ClassName(requested.playerClass)));
    if(!SaveRuntimeProfileRequest(paths::Root()/"config/runtime_profile.json",requested,error)) {
        SetStatus("Profile request could not be saved: "+error); return false;
    }
    {
        std::scoped_lock lock(mutex_);
        requestedProfile_=requested;
        unavailable_={"Checkpoint reload entry point and safe dispatch unresolved",
            "Player creation / character initialization hook unresolved",
            "Character package assignment unresolved", "Weapon creation / equip / sockets / handling unresolved"};
        if(requested.playerClass!=PlayerClass::Brute) {
            unavailable_.push_back("Player-compatible animation initialization unresolved");
            unavailable_.push_back("FightStyle assignment unresolved");
        } else {
            unavailable_.push_back("Brute finishers and non-mapped hit reactions remain experimental");
        }
        if(requested.nation!=Nation::British || (requested.playerClass!=PlayerClass::Agile && requested.playerClass!=PlayerClass::Brute))
            unavailable_.push_back("Current vertical slice targets British / Agile or British / Brute");
        if(requested.ship!=ShipType::JackdawVanilla)
            unavailable_.push_back("Ship replacement unresolved; Jackdaw remains the fallback");
    }
    log::Write("[INFO] Pending profile saved to config/runtime_profile.json; actual ActiveProfile unchanged");
    for(const auto& reason:Unavailable()) log::Write("[WARN] Profile initialization blocked: "+reason);
    auto& chain=ChainKillRule::Instance();
    const bool wasRequested=chain.ProfileRequested();
    chain.SetDebugRequested(false);
    chain.SetProfileRequested(requested.disableChainKills);
    log::Write(std::string("[CHAIN] Profile rule requested: ")+(requested.disableChainKills?"disable chain kills":"original chain permission"));
    // Other components remain durable requests for the initialization route.
    // Brute is the exception: its combat/stance layer is now a Player-only runtime
    // ActionAlias redirect installed by the DLL; no weapon/catalog mutation is made.
    if(!GameBuildDetector::Instance().Build().supported) {
        SetStatus("Profile saved and pending. This process is not a supported game; no reload or application occurred.");
        return false;
    }
    if(requested.playerClass==PlayerClass::Brute && requested.authenticMoveset) {
        SetStatus(std::string("Brute profile requested. Runtime Player-only Heavy/Brute alias force will activate automatically; ")+
            "keep the validated British Cutlasses axe carrier equipped. Press H then perform a melee attack for the Brute Special test. "+
            (requested.disableChainKills?"Chain kills are requested OFF.":"Chain kills keep their original profile setting."));
        return true;
    }
    if(chain.Available()&&(requested.disableChainKills||wasRequested)) {
        SetStatus(std::string(requested.disableChainKills?"Chain-kill rule applied: Player chain executions disabled. ":"Chain-kill rule removed: original permission restored. ")+
            "Other runtime profile components remain unavailable. Installed static mod is unchanged; automatic mission detection is unavailable.");
        return true;
    }
    SetStatus("Profile saved and pending. Checkpoint reload and player-initialization bindings are unresolved; no reload or gameplay change occurred.");
    return false;
}

void RuntimeCore::ResetVanilla() {
    ChainKillRule::Instance().Reset();
    std::string saveError,restoreError;
    const bool disabled=SaveRuntimeProfileRequest(paths::Root()/"config/runtime_profile.json",std::nullopt,saveError);
    if(disabled) { std::scoped_lock lock(mutex_); requestedProfile_.reset(); }
    // A disk error must never prevent restoration of existing runtime changes.
    const bool restored=registry_.RestoreAll(restoreError);
    if (!restored) {
        SetStatus(std::string(disabled?"Profile request disabled. ":"Persistent request could not be disabled: "+saveError+". ")+
            "Vanilla restore incomplete: "+restoreError); return;
    }
    { std::scoped_lock lock(mutex_); activeProfile_.reset(); suspendedProfile_.reset(); safety_.Reset(); }
    if(!disabled) {SetStatus("Runtime restored, but persistent profile request could not be disabled: "+saveError);return;}
    SetStatus("Runtime rules restored, including chain kills. Persistent request disabled. Installed static archives are unchanged.");
}

void RuntimeCore::RestoreRuntimeForShutdown() {
    ChainKillRule::Instance().Reset();
    std::string error;
    if(!registry_.RestoreAll(error)) {SetStatus("Shutdown restoration incomplete: "+error);return;}
    {std::scoped_lock lock(mutex_);activeProfile_.reset();suspendedProfile_.reset();safety_.Reset();}
    SetStatus("Runtime restored for shutdown; persistent profile request retained for a future validated player initialization.");
}

void RuntimeCore::Initialize() {
    ChainKillRule::Instance().Reset();
    std::string error;
    settings_=DiscoverSettings();
    if (!LoadSettings(settings_,error)) log::Write("[INFO] Settings discovery: "+error);
    std::optional<Profile> requested;
    if(!LoadRuntimeProfileRequest(paths::Root()/"config/runtime_profile.json",requested,error)) {
        SetStatus("Saved runtime profile request rejected: "+error); return;
    }
    {std::scoped_lock lock(mutex_);requestedProfile_=requested;}
    ChainKillRule::Instance().SetProfileRequested(requested&&requested->disableChainKills);
    if(requested) {
        profile_=*requested;
        SetStatus("Saved profile request loaded. Chain-kill rule awaits its validated binding; other runtime components remain unavailable.");
    }
}
bool RuntimeCore::SaveSelectedProfile() {
    std::string error;
    const auto name=ProfileFilename(profile_);
    if (name.empty() || !SaveProfile(paths::Profiles()/name,profile_,error)) { SetStatus("Save failed: "+error); return false; }
    SetStatus("Profile saved: "+name); return true;
}
bool RuntimeCore::LoadProfileFile(const std::filesystem::path& path) {
    std::string error;
    if (!LoadProfile(path,profile_,error)) { SetStatus("Load failed: "+error); return false; }
    SetStatus("Profile loaded. Apply when ready."); return true;
}
std::optional<Profile> RuntimeCore::ActiveProfile() const { std::scoped_lock lock(mutex_); return activeProfile_; }
std::optional<Profile> RuntimeCore::RequestedProfile() const { std::scoped_lock lock(mutex_); return requestedProfile_; }
std::vector<std::string> RuntimeCore::Unavailable() const { std::scoped_lock lock(mutex_); return unavailable_; }
MissionState RuntimeCore::Mission() const { std::scoped_lock lock(mutex_); return safety_.State(); }
bool RuntimeCore::PersistSettings() {
    std::string error;
    if (!SaveSettings(settings_,error)) { SetStatus("Settings save failed: "+error); return false; }
    SetStatus("Settings saved. Hotkey takes effect immediately."); return true;
}
void RuntimeCore::OnMissionState(MissionState state) {
    auto active=ActiveProfile();
    const auto policy=active?*active:(suspendedProfile_?*suspendedProfile_:profile_);
    auto action=safety_.Update(state,policy,active.has_value());
    if (action==SafetyAction::Suspend) {
        suspendedProfile_=active;
        std::string error;
        const bool registryRestored=registry_.RestoreAll(error);
        if (registryRestored) { std::scoped_lock lock(mutex_); activeProfile_.reset(); }
        SetStatus(error.empty()?"Mission safety: restored vanilla during unsafe state.":"Mission restoration failed: "+error);
    } else if (action==SafetyAction::Resume && suspendedProfile_) {
        SetStatus("Mission ended: saved profile awaits a validated player-initialization adapter. No live character swap or checkpoint loop was triggered.");
    }
}

}
