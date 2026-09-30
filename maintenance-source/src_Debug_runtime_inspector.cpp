#include "runtime_inspector.h"
#include "resource_graph.h"
#include "resource_focus.h"
#include "resource_identity.h"
#include "Signatures/signature_database.h"
#include "Game/build_detector.h"
#include "Core/settings.h"
#include "Memory/safe_memory.h"
#include "player_runtime.h"
#include "Appearance/appearance_adapter.h"
#include "runtime_core.h"
#include "log.h"
#include <chrono>
#include <cstdio>

namespace ac4fo {
namespace {
nlohmann::json SessionMetadata() {
    SYSTEMTIME utc{}; GetSystemTime(&utc);
    char stamp[40]{};
    sprintf_s(stamp,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,utc.wSecond,utc.wMilliseconds);
    FILETIME created{},exited{},kernel{},user{};
    const bool times=GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=FALSE;
    const std::uint64_t createdTicks=(std::uint64_t{created.dwHighDateTime}<<32)|created.dwLowDateTime;
    return {{"pid",GetCurrentProcessId()},{"processCreationFileTime",times?nlohmann::json(createdTicks):nlohmann::json(nullptr)},
            {"capturedUtc",stamp},{"tickMilliseconds",GetTickCount64()},{"threadId",GetCurrentThreadId()}};
}
nlohmann::json Observation(const PlayerObservation& o) {
    return {{"root",o.root},{"actor",o.actor},{"entity",o.entity},{"readable",o.readable},{"rootUnchanged",o.rootUnchanged}};
}
}
RuntimeInspector& RuntimeInspector::Instance() { static auto* r=new RuntimeInspector; return *r; }
void RuntimeInspector::Rescan() {
    if (Busy()) return;
    status_="Scanning executable sections on worker thread...";
    scan_=std::async(std::launch::async,[]{ return SignatureDatabase::Instance().Rescan(); });
}
void RuntimeInspector::Tick() {
    if (!Busy() || scan_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
    try { status_=scan_.get()?"All declared signatures validated.":"One or more signatures unavailable. Affected access disabled."; }
    catch(const std::exception& e) { status_=e.what(); }
    PlayerRuntime::Instance().Initialize();
}
void RuntimeInspector::Snapshot() {
    if (Busy()) return;
    const auto observation=PlayerRuntime::Instance().Observe();
    root_=observation.root; actor_=observation.actor; entity_=observation.entity;
    status_=observation.Complete()?"On-demand snapshot captured. Addresses are observations, not stable write targets.":"Player unavailable or changed during lookup. No writable target established.";
}
void RuntimeInspector::Dump() {
    if(Busy()) return;
    Snapshot();
    const auto& b=GameBuildDetector::Instance().Build();
    nlohmann::json j{{"sha256",b.sha256},{"moduleBase",b.moduleBase},{"buildSupported",b.supported},
        {"root",root_},{"actor",actor_},{"entity",entity_},{"mission",MissionStateName(RuntimeCore::Instance().Mission())},
        {"modifications",RuntimeCore::Instance().ModificationCount()},{"signatures",nlohmann::json::array()},
        {"appearance",{{"available",false},
                        {"status",AppearanceRuntimeAdapter::Instance().Status()}}},
        {"session",SessionMetadata()}};
    const auto requested=RuntimeCore::Instance().RequestedProfile();
    j["profileApplication"]={{"strategy","checkpoint-player-initialization"},
        {"requestedProfile",requested?ProfileToJson(*requested):nlohmann::json(nullptr)},
        {"checkpointReloadValidated",false},{"playerInitializationValidated",false},{"engineApplicationComplete",false}};
    for (const auto& r:SignatureDatabase::Instance().Results()) j["signatures"].push_back({{"name",r.name},{"address",r.address},{"validated",r.validated},{"status",r.status}});
    // Bounded read-only player snapshot, never a process-wide memory dump.
    if (entity_) {
        std::vector<std::uint8_t> bytes(1024);
        if (memory::ReadBytes(entity_,bytes.data(),bytes.size())) j["entityFirst1024Bytes"]=bytes;
    }
    std::string error;
    status_=WriteJsonAtomic(paths::Logs()/"state.json",j,error)?"State written to logs/state.json":"State dump failed: "+error;
    log::Write("[INFO] "+status_);
}
void RuntimeInspector::ExportResourceGraph() {
    if(Busy()) return;
    const auto& build=GameBuildDetector::Instance().Build();
    const auto before=PlayerRuntime::Instance().Observe();
    if(!build.supported || !before.Complete()) {
        status_="Resource graph refused: validated build and readable player are required.";
        log::Write("[WARN] "+status_); return;
    }
    const auto session=SessionMetadata();
    auto graph=research::CaptureResourceGraph({{"PlayerRoot",static_cast<std::uint32_t>(before.root),0x2000},
        {"PlayerActor",static_cast<std::uint32_t>(before.actor),0x3000},
        {"PlayerEntity",static_cast<std::uint32_t>(before.entity),0x4000}},research::ReadProcessDataBlock,
        static_cast<std::uint32_t>(build.moduleBase),build.imageSize);
    const auto after=PlayerRuntime::Instance().Observe();
    const bool comparable=before.Complete() && after.Complete() && before.SameAddresses(after);
    graph["session"]=session; graph["sha256"]=build.sha256; graph["moduleBase"]=build.moduleBase;
    graph["before"]=Observation(before); graph["after"]=Observation(after);
    graph["samePlayerAddressesAtEndpoints"]=comparable;
    graph["atomicSnapshot"]=false; graph["ownerLifetimeVerified"]=false;
    graph["mission"]=MissionStateName(RuntimeCore::Instance().Mission());
    const auto filename="resource-graph-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".json";
    std::string error;
    if(WriteJsonAtomic(paths::Logs()/filename,graph,error)) {
        status_="Exported "+std::to_string(graph["nodes"].size())+" nodes to logs/"+filename;
        if(!comparable) status_+="; PLAYER CHANGED: comparison invalid";
        if(graph["truncated"].get<bool>()) status_+="; bounded capture: "+graph["stopReason"].get<std::string>();
        log::Write("[INFO] "+status_+". Read-only; lifetime remains unknown.");
    } else { status_="Resource graph export failed: "+error; log::Write("[ERROR] "+status_); return; }
    if(!comparable) return;
    try {
        const auto reader=research::ReadProcessDataBlock;
        const auto targets=research::SelectFocusCandidates(graph,build.sha256,static_cast<std::uint32_t>(before.actor),reader);
        if(targets.empty()) { log::Write("[INFO] Focused resource capture: no validated type candidates in this bounded graph."); return; }
        const auto focusBefore=PlayerRuntime::Instance().Observe();
        if(!focusBefore.Complete() || !before.SameAddresses(focusBefore)) {
            log::Write("[WARN] Focused resource capture skipped: player observation changed."); return;
        }
        const auto focusSession=SessionMetadata();
        auto focus=research::CaptureFocusedResources(targets,reader,static_cast<std::uint32_t>(build.moduleBase),build.imageSize);
        focus["resourceIdentityResearch"]=research::ObservePoolResourceIds(targets,build.sha256,
            static_cast<std::uint32_t>(build.moduleBase),[](std::uint32_t address,void* buffer,std::size_t size) {
                return memory::ReadBytes(address,buffer,size);
            });
        const auto focusAfter=PlayerRuntime::Instance().Observe();
        const bool same=focusAfter.Complete() && focusBefore.SameAddresses(focusAfter);
        focus["captureMode"]="in-game-focused-type-candidates";
        focus["session"]=focusSession; focus["sha256"]=build.sha256; focus["moduleBase"]=build.moduleBase;
        focus["sourceBroadCapture"]=filename; focus["before"]=Observation(focusBefore); focus["after"]=Observation(focusAfter);
        focus["samePlayerAddressesAtEndpoints"]=same;
        focus["mission"]=MissionStateName(RuntimeCore::Instance().Mission());
        const auto focusedFile="resource-focused-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".json";
        if(!WriteJsonAtomic(paths::Logs()/focusedFile,focus,error)) throw std::runtime_error(error);
        status_="Broad and focused graphs exported; "+std::to_string(targets.size())+" type candidates. Read-only.";
        if(!same || !focus.at("candidateAnchorsUnchanged").get<bool>()) status_+=" CHANGED ANCHORS: comparison invalid.";
        log::Write("[INFO] "+status_+" logs/"+focusedFile+"; no resource or ownership certification.");
    } catch(const std::exception& e) {
        status_="Broad graph saved; focused capture failed: "+std::string(e.what());
        log::Write("[WARN] "+status_);
    }
}
}
