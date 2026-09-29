#include <Windows.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <string>
#include <vector>

namespace fs=std::filesystem;
using json=nlohmann::json;

namespace {
std::wstring ArgValue(int argc,wchar_t** argv,const wchar_t* key){
    for(int i=1;i+1<argc;i++) if(_wcsicmp(argv[i],key)==0) return argv[i+1];
    return {};
}
void Log(const fs::path& root,const std::string& line){try{fs::create_directories(root/"logs");std::ofstream out(root/"logs/launcher-updater.log",std::ios::app);out<<line<<"\n";}catch(...) {}}
bool WaitForPid(DWORD pid){
    if(!pid)return true;HANDLE h=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!h)return true;
    const DWORD r=WaitForSingleObject(h,60000);CloseHandle(h);return r==WAIT_OBJECT_0;
}
bool ReplaceFileSafe(const fs::path& src,const fs::path& dst,std::string& error){
    std::error_code ec;fs::create_directories(dst.parent_path(),ec);
    const fs::path tmp=dst.wstring()+L".new";const fs::path bak=dst.wstring()+L".bak";
    fs::remove(tmp,ec);ec.clear();fs::copy_file(src,tmp,fs::copy_options::overwrite_existing,ec);if(ec){error="copy failed: "+ec.message();return false;}
    fs::remove(bak,ec);ec.clear();if(fs::exists(dst)){fs::rename(dst,bak,ec);if(ec){error="backup failed: "+ec.message();fs::remove(tmp);return false;}}
    ec.clear();fs::rename(tmp,dst,ec);if(ec){if(fs::exists(bak)){std::error_code rc;fs::rename(bak,dst,rc);}error="replace failed: "+ec.message();return false;}
    fs::remove(bak,ec);return true;
}
}

int wmain(int argc,wchar_t** argv){
    const fs::path staging=ArgValue(argc,argv,L"--apply-launcher");
    const fs::path target=ArgValue(argc,argv,L"--target-dir");
    const fs::path relaunch=ArgValue(argc,argv,L"--relaunch");
    const auto pidText=ArgValue(argc,argv,L"--pid");
    if(staging.empty()||target.empty()||relaunch.empty())return 2;
    DWORD pid=0;try{pid=static_cast<DWORD>(std::stoul(pidText));}catch(...){return 3;}
    if(!WaitForPid(pid)){Log(target,"Timed out waiting for launcher to exit");return 4;}
    try{
        json pkg;std::ifstream in(staging/"launcher-package.json");if(!in)return 5;in>>pkg;
        if(pkg.value("schemaVersion",0)!=1)return 6;
        std::vector<std::string> files=pkg.value("files",std::vector<std::string>{"FactionOverhaulLauncher.exe"});
        for(const auto& rel:files){
            fs::path src=staging/fs::u8path(rel),dst=target/fs::u8path(rel);if(!fs::exists(src)){Log(target,"Missing staged file: "+rel);return 7;}
            std::string error;if(!ReplaceFileSafe(src,dst,error)){Log(target,"Cannot replace "+rel+": "+error);return 8;}
        }
        json version;const auto vf=target/"version.json";if(fs::exists(vf)){try{std::ifstream vin(vf);vin>>version;}catch(...){version=json::object();}}
        version["schemaVersion"]=1;version["launcherVersion"]=pkg.at("version").get<std::string>();
        std::ofstream vout(vf,std::ios::trunc);vout<<version.dump(2)<<"\n";
        std::error_code ec;fs::remove_all(staging,ec);
        const fs::path exe=target/relaunch;
        auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",exe.c_str(),nullptr,target.c_str(),SW_SHOWNORMAL));
        if(result<=32){Log(target,"Launcher updated but relaunch failed");return 9;}
        return 0;
    }catch(const std::exception& e){Log(target,std::string("Updater exception: ")+e.what());return 10;}
}
