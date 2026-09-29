#include "self_update_service.h"
#include "launcher_io.h"
#include <Windows.h>
#include <json.hpp>
#include <fstream>
#include <vector>
#include <stdexcept>

namespace ac4fo::launcher {
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
std::wstring QuoteArg(const fs::path& p) {
    return L"\"" + p.wstring() + L"\"";
}

bool LaunchUpdaterProcess(const fs::path& updater, const fs::path& staging, const fs::path& target,
                          const fs::path& launcherExe, std::string& error) {
    const DWORD pid = GetCurrentProcessId();
    std::wstring cmd = QuoteArg(updater) + L" --apply-launcher " + QuoteArg(staging) +
        L" --target-dir " + QuoteArg(target) + L" --pid " + std::to_wstring(pid) +
        L" --relaunch " + QuoteArg(launcherExe.filename());
    STARTUPINFOW si{}; si.cb=sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmd.begin(),cmd.end()); mutableCmd.push_back(L'\0');
    if(!CreateProcessW(nullptr,mutableCmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,target.c_str(),&si,&pi)) {
        error="Cannot start FactionOverhaulUpdater.exe (Windows error "+std::to_string(GetLastError())+")";
        return false;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}
}

SelfUpdateService::SelfUpdateService(fs::path launcherDir, Logger logger)
    : launcherDir_(std::move(launcherDir)), logger_(std::move(logger)) {
    snapshot_.currentVersion=kLauncherVersion;
}
SelfUpdateService::~SelfUpdateService(){ if(worker_.valid()) worker_.wait(); }
void SelfUpdateService::SetConfig(LauncherConfig config){std::scoped_lock lock(mutex_);config_=std::move(config);}
LauncherUpdateSnapshot SelfUpdateService::Snapshot() const {std::scoped_lock lock(mutex_);return snapshot_;}
bool SelfUpdateService::Busy() const {std::scoped_lock lock(mutex_);return snapshot_.phase==LauncherUpdateSnapshot::Phase::Checking||snapshot_.phase==LauncherUpdateSnapshot::Phase::Downloading||snapshot_.phase==LauncherUpdateSnapshot::Phase::Installing;}
bool SelfUpdateService::RestartScheduled() const {std::scoped_lock lock(mutex_);return snapshot_.restartScheduled;}
void SelfUpdateService::SetState(LauncherUpdateSnapshot::Phase phase,const std::string& status){std::scoped_lock lock(mutex_);snapshot_.phase=phase;snapshot_.status=status;}

void SelfUpdateService::CheckAsync(bool installWhenAvailable){
    if(Busy()) return;
    if(worker_.valid()) worker_.wait();
    worker_=std::async(std::launch::async,[this,installWhenAvailable]{CheckWorker(installWhenAvailable);});
}

void SelfUpdateService::CheckWorker(bool installWhenAvailable){
    LauncherConfig cfg;
    {std::scoped_lock lock(mutex_);cfg=config_;snapshot_.phase=LauncherUpdateSnapshot::Phase::Checking;snapshot_.status="Checking launcher update...";snapshot_.progress=0;snapshot_.updateAvailable=false;}
    if(cfg.updateManifestUrl.empty()){SetState(LauncherUpdateSnapshot::Phase::Idle,"Self-update ready - configure manifest URL");return;}
    std::string text,error;
    if(!DownloadText(cfg.updateManifestUrl,text,error)){SetState(LauncherUpdateSnapshot::Phase::Error,"Launcher update check failed: "+error);if(logger_)logger_("Launcher update check failed: "+error);return;}
    LauncherReleaseManifest manifest;
    if(!ParseLauncherReleaseManifest(text,manifest,error)){
        // Schema 1 is a runtime-only legacy manifest; do not break old setups.
        SetState(LauncherUpdateSnapshot::Phase::Ready,"Launcher channel not present in legacy manifest");
        if(logger_) logger_("Launcher self-update skipped: "+error);
        return;
    }
    const int cmp=CompareVersions(kLauncherVersion,manifest.version);
    {
        std::scoped_lock lock(mutex_);pending_=manifest;snapshot_.remoteVersion=manifest.version;snapshot_.notes=manifest.notes;snapshot_.updateAvailable=cmp<0;
        if(cmp<0){snapshot_.phase=LauncherUpdateSnapshot::Phase::Available;snapshot_.status="Launcher update available: v"+manifest.version;}
        else {snapshot_.phase=LauncherUpdateSnapshot::Phase::Ready;snapshot_.status=cmp==0?"Launcher up to date":"Local launcher is newer than update channel";}
    }
    if(cmp<0 && logger_) logger_("Launcher update available: "+std::string(kLauncherVersion)+" -> "+manifest.version);
    if(cmp<0 && installWhenAvailable) InstallWorker(manifest);
}

void SelfUpdateService::InstallWorker(LauncherReleaseManifest manifest){
    SetState(LauncherUpdateSnapshot::Phase::Downloading,"Downloading launcher v"+manifest.version+"...");
    std::string error;
    if(!PrepareAndLaunchUpdater(manifest,error)){
        SetState(LauncherUpdateSnapshot::Phase::Error,"Launcher update failed: "+error);
        if(logger_)logger_("Launcher update failed: "+error);
        return;
    }
    {
        std::scoped_lock lock(mutex_);snapshot_.phase=LauncherUpdateSnapshot::Phase::Installing;snapshot_.status="Restarting into launcher v"+manifest.version+"...";snapshot_.progress=1.0f;snapshot_.restartScheduled=true;
    }
    if(logger_)logger_("Launcher update staged; restarting into v"+manifest.version);
}

bool SelfUpdateService::PrepareAndLaunchUpdater(const LauncherReleaseManifest& manifest,std::string& error){
    const auto downloads=launcherDir_/"launcher/downloads";
    const auto zip=downloads/("FactionOverhaulLauncher-"+manifest.version+".zip");
    const auto staging=launcherDir_/"launcher/staging"/manifest.version;
    if(!DownloadFile(manifest.packageUrl,zip,[this](float p){std::scoped_lock lock(mutex_);snapshot_.progress=p;},error)) return false;
    const auto actual=Sha256File(zip,error);if(actual.empty())return false;
    if(actual!=manifest.sha256){error="Launcher SHA-256 mismatch. Expected "+manifest.sha256+", got "+actual;return false;}
    {std::scoped_lock lock(mutex_);snapshot_.phase=LauncherUpdateSnapshot::Phase::Installing;snapshot_.status="Preparing launcher v"+manifest.version+"...";}
    if(!ExpandZip(zip,staging,error))return false;
    const auto package=staging/"launcher-package.json";
    if(!fs::exists(package)){error="launcher-package.json missing from launcher update";return false;}
    try{
        json pkg;std::ifstream in(package);in>>pkg;
        if(pkg.value("schemaVersion",0)!=1) throw std::runtime_error("Unsupported launcher package schema");
        if(pkg.at("version").get<std::string>()!=manifest.version) throw std::runtime_error("Launcher package version mismatch");
        const auto exe=staging/fs::u8path(pkg.value("launcher",std::string("FactionOverhaulLauncher.exe")));
        if(!fs::exists(exe)) throw std::runtime_error("FactionOverhaulLauncher.exe missing from launcher package");
    }catch(const std::exception& e){error=e.what();return false;}
    const auto updater=launcherDir_/"FactionOverhaulUpdater.exe";
    if(!fs::exists(updater)){error="FactionOverhaulUpdater.exe missing next to launcher";return false;}
    return LaunchUpdaterProcess(updater,staging,launcherDir_,launcherDir_/"FactionOverhaulLauncher.exe",error);
}

} // namespace ac4fo::launcher
