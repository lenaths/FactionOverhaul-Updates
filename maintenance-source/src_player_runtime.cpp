#include "player_runtime.h"
#include "Game/build_detector.h"
#include "Signatures/signature_database.h"
#include "Memory/safe_memory.h"
#include "Player/player_entity_reader.h"
#include "log.h"

namespace ac4fo {
PlayerRuntime& PlayerRuntime::Instance() { static PlayerRuntime runtime; return runtime; }
void PlayerRuntime::SetStatus(std::string value) { std::scoped_lock lock(mutex_); status_=std::move(value); log::Write("[INFO] "+status_); }
bool PlayerRuntime::CheckExecutable() { return GameBuildDetector::Instance().Build().supported; }
bool PlayerRuntime::Initialize() {
    auto& signatures=SignatureDatabase::Instance();
    supportedBuild_=CheckExecutable() && signatures.Address("GetPlayerRoot") && signatures.Address("GetActorFromRoot");
    SetStatus(supportedBuild_?"Current-player reader available; instruction bytes and owner links are checked on demand, without game calls.":"Player lookup disabled: build/signature validation unavailable.");
    return supportedBuild_;
}
bool PlayerRuntime::SupportedBuild() const { return supportedBuild_; }
std::string PlayerRuntime::Status() const { std::scoped_lock lock(mutex_); return status_; }
void* PlayerRuntime::GetPlayerRoot() const {
    return reinterpret_cast<void*>(Observe().root);
}
void* PlayerRuntime::GetPlayerActor() const {
    return reinterpret_cast<void*>(Observe().actor);
}
void* PlayerRuntime::GetPlayerEntity() const {
    return reinterpret_cast<void*>(Observe().entity);
}
PlayerObservation PlayerRuntime::Observe() const {
    PlayerObservation result;
    if(!supportedBuild_) return result;
    try {
        const auto& build=GameBuildDetector::Instance().Build();
        const auto observed=research::ObservePlayerEntity(build.sha256,static_cast<std::uint32_t>(build.moduleBase),
            [](std::uint32_t address,void* bytes,std::size_t size) {return memory::ReadBytes(address,bytes,size);});
        result.root=observed.at("world").get<std::uintptr_t>();
        result.actor=observed.at("playerEntity").get<std::uintptr_t>();
        result.entity=observed.at("humanCandidate").get<std::uintptr_t>();
        result.rootUnchanged=observed.at("anchorsUnchanged").get<bool>();
        result.readable=result.root && result.actor && result.entity;
    } catch(const std::exception&) { return {}; }
    return result;
}
int PlayerRuntime::GetHoodState() const { return -1; }
int PlayerRuntime::GetCrouchState() const { return -1; }
bool PlayerRuntime::ToggleHood() { SetStatus("Hood mutation disabled: native side effects and restoration not verified."); return false; }
bool PlayerRuntime::ToggleCrouch() { SetStatus("Crouch mutation disabled: field ownership/lifetime and restoration not verified."); return false; }
}
