#pragma once
#include <future>
#include <string>
#include <cstdint>

namespace ac4fo {
class RuntimeInspector {
public:
    static RuntimeInspector& Instance();
    void Rescan();
    void Tick();
    void Snapshot();
    void Dump();
    void ExportResourceGraph();
    bool Busy() const { return scan_.valid(); }
    const std::string& Status() const { return status_; }
    std::uintptr_t Root() const { return root_; }
    std::uintptr_t Actor() const { return actor_; }
    std::uintptr_t Entity() const { return entity_; }
private:
    std::future<bool> scan_;
    std::string status_{"Ready. No expensive scans run per frame."};
    std::uintptr_t root_{},actor_{},entity_{};
};
}
