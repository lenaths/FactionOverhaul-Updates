#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <mutex>
#include <atomic>

namespace ac4fo {
struct PlayerObservation {
    // Retained legacy report keys: root = World, actor = Entity, entity = Human.
    std::uintptr_t root{},actor{},entity{};
    bool readable{},rootUnchanged{};
    bool Complete() const { return readable && rootUnchanged; }
    bool SameAddresses(const PlayerObservation& other) const {
        return root==other.root && actor==other.actor && entity==other.entity;
    }
};

class PlayerRuntime {
public:
    static PlayerRuntime& Instance();

    bool Initialize();
    bool SupportedBuild() const;
    std::string Status() const;

    void* GetPlayerRoot() const;
    void* GetPlayerActor() const;
    void* GetPlayerEntity() const;
    PlayerObservation Observe() const;

    int GetHoodState() const;
    int GetCrouchState() const;

    bool ToggleHood();
    bool ToggleCrouch();

private:
    PlayerRuntime() = default;
    bool CheckExecutable();
    void SetStatus(std::string value);

    mutable std::mutex mutex_;
    std::atomic<bool> supportedBuild_{false};
    std::string status_{"Not initialized"};
};

}
