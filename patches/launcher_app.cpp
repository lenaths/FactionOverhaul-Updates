#include "launcher_app.h"
#include "launcher_io.h"
#include <imgui.h>
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ac4fo::launcher {
namespace fs = std::filesystem;

namespace {
constexpr ImVec4 kBg{0.025f,0.055f,0.073f,1.0f};
constexpr ImVec4 kPanel{0.035f,0.082f,0.105f,0.98f};
constexpr ImVec4 kPanel2{0.027f,0.066f,0.088f,0.98f};
constexpr ImVec4 kGold{0.80f,0.68f,0.48f,1.0f};
constexpr ImVec4 kGoldDim{0.53f,0.43f,0.29f,1.0f};
constexpr ImVec4 kText{0.88f,0.86f,0.79f,1.0f};
constexpr ImVec4 kMuted{0.52f,0.56f,0.58f,1.0f};
constexpr ImVec4 kGreen{0.32f,0.83f,0.48f,1.0f};
constexpr ImVec4 kYellow{0.95f,0.72f,0.16f,1.0f};
constexpr ImVec4 kRed{0.53f,0.10f,0.075f,1.0f};

long long NowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
const char* NationName(int n){static const char* v[]={"British","Spanish","Pirate","Templar"};return n>=0&&n<4?v[n]:"British";}
const char* ClassName(int n){static const char* v[]={"Soldier","Agile","Brute","Gunner","Officer","Commander"};return n>=0&&n<6?v[n]:"Agile";}
const char* ShipName(int n){static const char* v[]={"Faction Man O' War","Frigate","Brig","Jackdaw (Vanilla)"};return n>=0&&n<4?v[n]:"Jackdaw (Vanilla)";}
const char* ClassDescription(int n){
    switch(n){case 1:return "Fast movement, fluid counters and agile combat";case 2:return "Heavy pressure, axe carrier and brute combat";case 0:return "Disciplined line fighter";case 3:return "Ranged pressure and firearms";case 4:return "Officer command style";case 5:return "High-rank battlefield command";default:return "Faction combat profile";}
}

void DrawCompass(ImDrawList* dl, ImVec2 c, float r){
    const ImU32 gold=ImGui::ColorConvertFloat4ToU32(ImVec4(kGold.x,kGold.y,kGold.z,0.42f));
    dl->AddCircle(c,r,gold,48,1.0f);dl->AddCircle(c,r*0.68f,gold,48,1.0f);
    dl->AddLine(ImVec2(c.x-r,c.y),ImVec2(c.x+r,c.y),gold,1.0f);dl->AddLine(ImVec2(c.x,c.y-r),ImVec2(c.x,c.y+r),gold,1.0f);
    dl->AddLine(ImVec2(c.x-r*.72f,c.y-r*.72f),ImVec2(c.x+r*.72f,c.y+r*.72f),gold,1.0f);
    dl->AddLine(ImVec2(c.x+r*.72f,c.y-r*.72f),ImVec2(c.x-r*.72f,c.y+r*.72f),gold,1.0f);
}

void SectionLabel(const char* text){ImGui::PushStyleColor(ImGuiCol_Text,kGold);ImGui::TextUnformatted(text);ImGui::PopStyleColor();}

} // namespace

LauncherApp::LauncherApp(fs::path launcherDir)
    : launcherDir_(std::move(launcherDir)), configPath_(launcherDir_/"launcher_config.json"),
      updater_(launcherDir_, [this](const std::string& m){Log(m);}),
      selfUpdater_(launcherDir_, [this](const std::string& m){Log(m);}) {}
LauncherApp::~LauncherApp(){SaveConfig();}

void LauncherApp::Initialize(){
    std::string error;
    if(!LoadLauncherConfig(configPath_,config_,error) && !error.empty()) Log("Launcher config rejected: "+error,"WARN");
    runtime_=DiscoverCurrentRuntime(launcherDir_);
    gameExe_=DiscoverGameExecutable(config_);
    if(config_.gameExecutable.empty() && !gameExe_.empty()) config_.gameExecutable=gameExe_;
    const auto profilePath=runtime_.root/"config/runtime_profile.json";
    if(!LoadLauncherProfile(profilePath,profile_,error) && !error.empty()) Log("Profile read failed: "+error,"WARN");
    updater_.SetConfig(config_); updater_.SetRuntime(runtime_); selfUpdater_.SetConfig(config_);
    std::snprintf(manifestUrlBuf_,sizeof(manifestUrlBuf_),"%s",config_.updateManifestUrl.c_str());
    std::snprintf(gamePathBuf_,sizeof(gamePathBuf_),"%s",Narrow(config_.gameExecutable.wstring()).c_str());
    Log("Faction Overhaul Launcher v"+std::string(kLauncherVersion)+" initialized");
    Log("Current mod runtime: v"+runtime_.version);
    if(gameExe_.empty()) Log("AC4 executable not found automatically; set it in Settings","WARN");
    else Log("AC4 detected at: "+gameExe_.string());
    RefreshEnvironment();
    if(config_.autoCheckUpdates) StartUpdateCycle();
}

void LauncherApp::Log(const std::string& message,const char* level){
    const std::string line="["+TimeStamp()+"] ["+level+"] "+message;
    {
        std::scoped_lock lock(logsMutex_);logs_.push_back(line);if(logs_.size()>500)logs_.erase(logs_.begin(),logs_.begin()+100);
    }
    try{fs::create_directories(launcherDir_/"logs");std::ofstream out(launcherDir_/"logs/launcher.log",std::ios::app);out<<line<<"\n";}catch(...){}
}

void LauncherApp::RefreshEnvironment(){
    gamePid_=FindGameProcessId();runtimeReady_=RuntimeReady(gamePid_);gameExe_=DiscoverGameExecutable(config_);
    runtime_=updater_.Runtime();
}

void LauncherApp::Tick(){
    const auto now=NowMs();
    if(now-lastEnvRefreshMs_>1000){RefreshEnvironment();lastEnvRefreshMs_=now;}
    if(selfUpdater_.RestartScheduled()){ wantsClose_=true; return; }

    if(pendingRuntimeCheckAfterSelf_ && !selfUpdater_.Busy()){
        const auto ls=selfUpdater_.Snapshot();
        if(ls.updateAvailable && !config_.autoInstallUpdates){
            pendingRuntimeCheckAfterSelf_=false; updateCycleActive_=false; firstUpdateCheckDone_=true; lastAutoCheckMs_=now;
        } else {
            pendingRuntimeCheckAfterSelf_=false;
            updater_.SetConfig(config_); updater_.SetRuntime(runtime_); updater_.CheckAsync(config_.autoInstallUpdates);
            runtimeCheckInFlight_=true;
        }
    }
    if(runtimeCheckInFlight_ && !updater_.Busy()){
        runtimeCheckInFlight_=false; updateCycleActive_=false; firstUpdateCheckDone_=true; lastAutoCheckMs_=now;
    }
    if(config_.autoCheckUpdates && firstUpdateCheckDone_ && !updateCycleActive_ && now-lastAutoCheckMs_>static_cast<long long>(config_.checkIntervalMinutes)*60000LL) StartUpdateCycle();
}

bool LauncherApp::BigButton(const char* id,const char* title,const char* subtitle,bool primary,bool enabled){
    const ImVec2 size(ImGui::GetContentRegionAvail().x,96.0f);
    const ImVec2 p=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id,size);
    const bool hovered=enabled&&ImGui::IsItemHovered();const bool clicked=enabled&&ImGui::IsItemClicked();
    auto* dl=ImGui::GetWindowDrawList();
    const ImU32 fill=ImGui::ColorConvertFloat4ToU32(primary?ImVec4(hovered?0.48f:0.38f,0.065f,0.045f,1.0f):ImVec4(hovered?0.055f:0.035f,hovered?0.11f:0.08f,hovered?0.135f:0.105f,1.0f));
    const ImU32 border=ImGui::ColorConvertFloat4ToU32(hovered?kGold:ImVec4(kGoldDim.x,kGoldDim.y,kGoldDim.z,0.92f));
    dl->AddRectFilled(p,ImVec2(p.x+size.x,p.y+size.y),fill,6.0f);
    dl->AddRect(p,ImVec2(p.x+size.x,p.y+size.y),border,6.0f,0,1.5f);
    dl->AddLine(ImVec2(p.x+1,p.y+3),ImVec2(p.x+size.x-1,p.y+3),ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,0.06f)),1.0f);
    dl->AddText(ImGui::GetFont(),25.0f,ImVec2(p.x+36,p.y+14),ImGui::ColorConvertFloat4ToU32(enabled?kText:kMuted),title);
    dl->AddText(ImGui::GetFont(),16.0f,ImVec2(p.x+36,p.y+55),ImGui::ColorConvertFloat4ToU32(kMuted),subtitle);
    dl->AddText(ImVec2(p.x+size.x-38,p.y+35),border,">");
    return clicked;
}

void LauncherApp::StatusCard(const char* title,const std::string& value,const std::string& detail,bool good,bool accent){
    const ImVec2 size(ImGui::GetContentRegionAvail().x,110.0f);const ImVec2 p=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(title,size);
    auto* dl=ImGui::GetWindowDrawList();const ImU32 border=ImGui::ColorConvertFloat4ToU32(accent?kGold:kGoldDim);
    dl->AddRectFilled(p,ImVec2(p.x+size.x,p.y+size.y),ImGui::ColorConvertFloat4ToU32(kPanel2),7.0f);
    dl->AddRect(p,ImVec2(p.x+size.x,p.y+size.y),border,7.0f,0,1.0f);
    dl->AddCircle(ImVec2(p.x+34,p.y+40),22,ImGui::ColorConvertFloat4ToU32(kGold),32,1.5f);
    dl->AddText(ImVec2(p.x+72,p.y+18),ImGui::ColorConvertFloat4ToU32(kText),title);
    dl->AddText(ImVec2(p.x+72,p.y+43),ImGui::ColorConvertFloat4ToU32(good?kGreen:kYellow),value.c_str());
    dl->AddText(ImVec2(p.x+72,p.y+70),ImGui::ColorConvertFloat4ToU32(kMuted),detail.c_str());
    ImGui::Dummy(ImVec2(0,6));
}

void LauncherApp::DrawHeader(){
    auto* dl=ImGui::GetWindowDrawList();const ImVec2 p=ImGui::GetWindowPos();const float w=ImGui::GetWindowWidth();
    const ImU32 sea0=IM_COL32(7,18,25,255), sea1=IM_COL32(10,35,43,255);\n    dl->AddRectFilledMultiColor(p,ImVec2(p.x+w,p.y+170),sea0,sea0,sea1,sea1);\n    for(int i=0;i<9;i++){float x=p.x+45.0f+i*(w-90.0f)/8.0f;dl->AddLine(ImVec2(x,p.y+8),ImVec2(x-70,p.y+164),IM_COL32(169,139,83,18),1.0f);}\n    dl->AddLine(ImVec2(p.x+18,p.y+156),ImVec2(p.x+w-18,p.y+156),IM_COL32(190,154,92,120),1.0f);\n    const float y=p.y+78.0f;
    DrawCompass(dl,ImVec2(p.x+w*.50f,y+2),58.0f);
    const char* title="FACTION OVERHAUL";const ImVec2 ts=ImGui::CalcTextSize(title);
    const float titleX=p.x+w*.5f-ts.x*1.30f;
    dl->AddText(ImGui::GetFont(),42.0f,ImVec2(titleX,y-30),ImGui::ColorConvertFloat4ToU32(kText),title);
    const char* sub="L  O  A  D  E  R";const ImVec2 ss=ImGui::CalcTextSize(sub);
    dl->AddText(ImGui::GetFont(),26.0f,ImVec2(p.x+w*.5f-ss.x*.78f,y+18),ImGui::ColorConvertFloat4ToU32(kText),sub);
    const char* small="BLACK FLAG MOD UTILITY";const ImVec2 sm=ImGui::CalcTextSize(small);
    dl->AddText(ImGui::GetFont(),15.0f,ImVec2(p.x+w*.5f-sm.x*.43f,y+53),ImGui::ColorConvertFloat4ToU32(kMuted),small);
    dl->AddLine(ImVec2(p.x+320,y+68),ImVec2(p.x+w-320,y+68),ImGui::ColorConvertFloat4ToU32(kGoldDim),1.0f);
    ImGui::Dummy(ImVec2(0,155));
}

void LauncherApp::DrawLeftStatus(float width){
    ImGui::BeginChild("left_status",ImVec2(width,0),false);
    StatusCard("Game Detected",gamePid_?"Running":"Ready",gamePid_?"Assassin's Creed IV is running":(gameExe_.empty()?"Set game path in Settings":"Executable found"),!gameExe_.empty(),true);
    StatusCard("Runtime Status",runtimeReady_?"Loaded":"Ready",runtimeReady_?"Faction Overhaul runtime initialized":"Ready to inject",true);
    const auto launcherUp=selfUpdater_.Snapshot();
    const bool anyUpdate=launcherUp.updateAvailable||updater_.Snapshot().updateAvailable;
    StatusCard("Build Version","Launcher v"+std::string(kLauncherVersion),"Mod v"+runtime_.version+(anyUpdate?" - update available":""),true);
    StatusCard("Selected Profile",std::string(NationName(profile_.nation))+" / "+ClassName(profile_.playerClass),ClassDescription(profile_.playerClass),true);
    ImGui::Spacing();ImGui::PushStyleColor(ImGuiCol_Text,kMuted);ImGui::TextWrapped("\"Same seas. New allegiances.\"");ImGui::PopStyleColor();
    ImGui::EndChild();
}

void LauncherApp::InjectCurrent(bool reload){
    std::string error;
    if(!gamePid_){Log("Start AC4 before injecting the mod","WARN");return;}
    runtime_=updater_.Runtime();
    if(InjectRuntime(runtime_,error)) Log(std::string(reload?"Runtime reloaded":"Mod injected")+" — v"+runtime_.version);
    else Log(std::string(reload?"Runtime reload failed: ":"Injection failed: ")+error,"ERROR");
}

void LauncherApp::StartUpdateCycle(){
    if(updateCycleActive_ || config_.updateManifestUrl.empty()) return;
    updateCycleActive_=true; pendingRuntimeCheckAfterSelf_=true; runtimeCheckInFlight_=false;
    selfUpdater_.SetConfig(config_); selfUpdater_.CheckAsync(config_.autoInstallUpdates);
}

void LauncherApp::CheckUpdates(bool userRequested){
    if(config_.updateManifestUrl.empty()){
        if(userRequested){settingsOpen_=true;Log("Configure an update manifest URL first","WARN");}
        return;
    }
    if(userRequested) Log("Checking launcher and mod updates...");
    StartUpdateCycle();
}

void LauncherApp::DrawCenter(float width){
    ImGui::BeginChild("center",ImVec2(width,0),false);
    if(BigButton("launch","LAUNCH GAME","Start Assassin's Creed IV: Black Flag",true,!gamePid_)){
        std::string error;if(LaunchGame(gameExe_,error))Log("Launching AC4");else Log("Launch failed: "+error,"ERROR");
    }
    ImGui::Spacing();
    if(BigButton("inject","INJECT MOD",runtimeReady_?"Runtime already loaded — inject current build again":"Load Faction Overhaul into the running game",false,gamePid_!=0))InjectCurrent(false);
    ImGui::Spacing();
    if(BigButton("reload","RELOAD RUNTIME","Hot-load the newest installed mod build",false,gamePid_!=0))InjectCurrent(true);
    ImGui::Spacing();
    if(BigButton("logs","OPEN LOGS","View launcher and runtime logs")){
        std::string error;fs::path logs=runtime_.root/"logs";if(!fs::exists(logs))logs=launcherDir_/"logs";if(!OpenFolder(logs,error))Log("Cannot open logs: "+error,"ERROR");
    }
    ImGui::Spacing();
    const auto lup=selfUpdater_.Snapshot();
    const auto up=updater_.Snapshot();
    const bool launcherBusy=lup.phase==LauncherUpdateSnapshot::Phase::Checking||lup.phase==LauncherUpdateSnapshot::Phase::Downloading||lup.phase==LauncherUpdateSnapshot::Phase::Installing;
    if(launcherBusy){
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,kGold);ImGui::ProgressBar(lup.progress,ImVec2(-1,20),lup.status.c_str());ImGui::PopStyleColor();
    } else if(up.phase==UpdateSnapshot::Phase::Checking||up.phase==UpdateSnapshot::Phase::Downloading||up.phase==UpdateSnapshot::Phase::Installing){
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,kGold);ImGui::ProgressBar(up.progress,ImVec2(-1,20),up.status.c_str());ImGui::PopStyleColor();
    } else if(lup.updateAvailable){
        ImGui::PushStyleColor(ImGuiCol_Text,kYellow);ImGui::TextWrapped("Launcher v%s is available. Automatic install is %s.",lup.remoteVersion.c_str(),config_.autoInstallUpdates?"enabled":"disabled in Settings");ImGui::PopStyleColor();
    } else if(up.updateAvailable){
        ImGui::PushStyleColor(ImGuiCol_Button,kRed);ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0.66f,0.13f,0.09f,1));
        if(ImGui::Button(("UPDATE MOD TO v"+up.remoteVersion).c_str(),ImVec2(-1,42)))updater_.InstallAsync();
        ImGui::PopStyleColor(2);
        if(!up.notes.empty()){ImGui::PushStyleColor(ImGuiCol_Text,kMuted);ImGui::TextWrapped("%s",up.notes.c_str());ImGui::PopStyleColor();}
    } else {
        const bool selfError=lup.phase==LauncherUpdateSnapshot::Phase::Error;
        ImGui::PushStyleColor(ImGuiCol_Text,(selfError||up.phase==UpdateSnapshot::Phase::Error)?kYellow:kMuted);
        const std::string status=selfError?lup.status:(up.status.empty()?lup.status:up.status);
        ImGui::TextWrapped("%s",status.empty()?"Launcher + mod auto-update ready":status.c_str());ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    if(ImGui::Button("SETTINGS",ImVec2(width*.38f,48)))settingsOpen_=true;
    ImGui::SameLine();if(ImGui::Button("PROFILES",ImVec2(width*.29f,48)))profilesOpen_=true;
    ImGui::SameLine();if(ImGui::Button("ABOUT",ImVec2(-1,48)))aboutOpen_=true;
    ImGui::EndChild();
}

void LauncherApp::DrawRightProfile(float width){
    ImGui::BeginChild("right_profile",ImVec2(width,0),true);
    SectionLabel("SELECTED PROFILE");ImGui::SameLine();ImGui::PushStyleColor(ImGuiCol_Text,kMuted);ImGui::Text("  LIVE LOADOUT");ImGui::PopStyleColor();ImGui::Separator();
    const ImVec2 p=ImGui::GetCursorScreenPos();const float panelW=ImGui::GetContentRegionAvail().x;
    auto* dl=ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(p,ImVec2(p.x+panelW,p.y+215),IM_COL32(9,27,34,255),IM_COL32(18,48,55,255),IM_COL32(5,16,22,255),IM_COL32(7,23,29,255));\n    dl->AddRect(p,ImVec2(p.x+panelW,p.y+215),IM_COL32(174,142,86,150),3,0,1.2f);
    DrawCompass(dl,ImVec2(p.x+panelW*.50f,p.y+105),78);
    dl->AddText(ImVec2(p.x+25,p.y+180),ImGui::ColorConvertFloat4ToU32(kGold),NationName(profile_.nation));
    ImGui::Dummy(ImVec2(0,228));
    SectionLabel("FACTION");ImGui::Text("%s",NationName(profile_.nation));ImGui::PushStyleColor(ImGuiCol_Text,kMuted);ImGui::TextWrapped("Discipline. Trade. Control.");ImGui::PopStyleColor();ImGui::Separator();
    SectionLabel("CLASS");ImGui::Text("%s",ClassName(profile_.playerClass));ImGui::PushStyleColor(ImGuiCol_Text,kMuted);ImGui::TextWrapped("%s",ClassDescription(profile_.playerClass));ImGui::PopStyleColor();ImGui::Separator();
    SectionLabel("STARTING EQUIPMENT");
    if(profile_.playerClass==2)ImGui::TextWrapped("British Cutlasses carrier -> Brute axe, Flintlock");
    else if(profile_.playerClass==1)ImGui::TextWrapped("Agile dagger carrier, Flintlock");
    else ImGui::TextWrapped("Faction loadout / current Edward carrier");
    ImGui::Separator();
    SectionLabel("RUNTIME RULES");ImGui::Text("Chain kills: %s",profile_.disableChainKills?"Disabled":"Original");
    ImGui::Text("Authentic moveset: %s",profile_.authenticMoveset?"On":"Off");
    ImGui::Spacing();
    if(ImGui::Button("EDIT PROFILE",ImVec2(-1,40)))profilesOpen_=true;
    ImGui::EndChild();
}

void LauncherApp::DrawRuntimeLog(float height){
    ImGui::BeginChild("runtime_log",ImVec2(0,height),true);
    SectionLabel("RUNTIME LOG");ImGui::SameLine(ImGui::GetContentRegionAvail().x-50);if(ImGui::SmallButton("CLEAR")){std::scoped_lock lock(logsMutex_);logs_.clear();}
    ImGui::Separator();
    ImGui::BeginChild("logscroll",ImVec2(0,0),false,ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::scoped_lock lock(logsMutex_);
        for(const auto& line:logs_){
            if(line.find("[ERROR]")!=std::string::npos)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.95f,0.35f,0.30f,1));
            else if(line.find("[WARN]")!=std::string::npos)ImGui::PushStyleColor(ImGuiCol_Text,kYellow);
            else ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.65f,0.77f,0.72f,1));
            ImGui::TextUnformatted(line.c_str());ImGui::PopStyleColor();
        }
        if(ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-5)ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();ImGui::EndChild();
}

void LauncherApp::DrawSettingsModal(){
    if(!settingsOpen_)return;ImGui::OpenPopup("Launcher Settings");settingsOpen_=false;
    ImGui::SetNextWindowSize(ImVec2(720,540),ImGuiCond_FirstUseEver);
    if(ImGui::BeginPopupModal("Launcher Settings",nullptr,ImGuiWindowFlags_NoResize)){
        SectionLabel("AUTOMATIC LAUNCHER + MOD UPDATES");
        ImGui::InputText("Manifest URL",manifestUrlBuf_,sizeof(manifestUrlBuf_));
        ImGui::Checkbox("Check automatically",&config_.autoCheckUpdates);ImGui::SameLine();ImGui::Checkbox("Install automatically",&config_.autoInstallUpdates);
        ImGui::Checkbox("Reload runtime after update when AC4 is running",&config_.autoReloadRuntimeAfterUpdate);
        ImGui::SliderInt("Check interval (minutes)",&config_.checkIntervalMinutes,5,240);
        ImGui::Spacing();SectionLabel("GAME");ImGui::InputText("AC4 executable",gamePathBuf_,sizeof(gamePathBuf_));
        ImGui::PushStyleColor(ImGuiCol_Text,kMuted);ImGui::TextWrapped("The launcher detects common Steam/Ubisoft locations automatically. You can override the path here.");ImGui::PopStyleColor();
        ImGui::Spacing();ImGui::Separator();
        if(ImGui::Button("SAVE",ImVec2(140,42))){config_.updateManifestUrl=manifestUrlBuf_;config_.gameExecutable=fs::u8path(gamePathBuf_);SaveConfig();updater_.SetConfig(config_);selfUpdater_.SetConfig(config_);gameExe_=DiscoverGameExecutable(config_);ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("CHECK NOW",ImVec2(160,42))){config_.updateManifestUrl=manifestUrlBuf_;CheckUpdates(true);}
        ImGui::SameLine();if(ImGui::Button("OPEN LAUNCHER FOLDER",ImVec2(210,42))){std::string e;OpenFolder(launcherDir_,e);}
        ImGui::SameLine();if(ImGui::Button("CLOSE",ImVec2(100,42)))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void LauncherApp::SaveConfig(){
    config_.updateManifestUrl=manifestUrlBuf_;config_.gameExecutable=fs::u8path(gamePathBuf_);
    std::string error;if(!SaveLauncherConfig(configPath_,config_,error))Log("Cannot save launcher config: "+error,"ERROR");
}

void LauncherApp::SaveProfile(bool reloadRuntime){
    runtime_=updater_.Runtime();std::string error;
    if(!SaveLauncherProfileRequest(runtime_.root,profile_,error)){Log("Profile save failed: "+error,"ERROR");return;}
    Log(std::string("Profile saved: ")+NationName(profile_.nation)+" / "+ClassName(profile_.playerClass));
    if(reloadRuntime && gamePid_)InjectCurrent(true);
}

void LauncherApp::DrawProfilesModal(){
    if(!profilesOpen_)return;ImGui::OpenPopup("Profiles");profilesOpen_=false;
    ImGui::SetNextWindowSize(ImVec2(760,650),ImGuiCond_FirstUseEver);
    if(ImGui::BeginPopupModal("Profiles",nullptr,ImGuiWindowFlags_NoResize)){
        SectionLabel("FACTION PROFILE");
        const char* nations[]={"British","Spanish","Pirate","Templar"};const char* classes[]={"Soldier","Agile","Brute","Gunner","Officer","Commander"};const char* ships[]={"Faction Man O' War","Frigate","Brig","Jackdaw (Vanilla)"};
        ImGui::Combo("Nationality",&profile_.nation,nations,IM_ARRAYSIZE(nations));ImGui::Combo("Class",&profile_.playerClass,classes,IM_ARRAYSIZE(classes));ImGui::Combo("Ship",&profile_.ship,ships,IM_ARRAYSIZE(ships));
        ImGui::Separator();SectionLabel("AUTHENTIC CLASS");
        ImGui::Checkbox("Authentic class moveset",&profile_.authenticMoveset);ImGui::Checkbox("Authentic weapons",&profile_.authenticWeapons);ImGui::Checkbox("Authentic locomotion",&profile_.authenticLocomotion);ImGui::Checkbox("Authentic finishers",&profile_.authenticFinishers);ImGui::Checkbox("Authentic hit reactions",&profile_.authenticHitReactions);ImGui::Checkbox("Custom loadout",&profile_.customLoadout);
        ImGui::Separator();SectionLabel("COMBAT RULES");ImGui::Checkbox("Disable chain kills",&profile_.disableChainKills);
        ImGui::Separator();SectionLabel("FACTION WORLD");ImGui::Checkbox("Faction soldiers are allies",&profile_.factionSoldiersAllies);ImGui::Checkbox("Faction ships are allies",&profile_.factionShipsAllies);ImGui::Checkbox("Faction crew",&profile_.factionCrew);ImGui::Checkbox("Faction recruits",&profile_.factionRecruits);ImGui::Checkbox("Faction flags",&profile_.factionFlags);ImGui::Checkbox("Captured outposts",&profile_.capturedOutposts);
        ImGui::Separator();
        if(ImGui::Button("SAVE PROFILE",ImVec2(180,44)))SaveProfile(false);
        ImGui::SameLine();if(ImGui::Button("SAVE + APPLY / RELOAD",ImVec2(240,44)))SaveProfile(true);
        ImGui::SameLine();if(ImGui::Button("CLOSE",ImVec2(120,44)))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void LauncherApp::DrawAboutModal(){
    if(!aboutOpen_)return;ImGui::OpenPopup("About");aboutOpen_=false;
    ImGui::SetNextWindowSize(ImVec2(600,410),ImGuiCond_FirstUseEver);
    if(ImGui::BeginPopupModal("About",nullptr,ImGuiWindowFlags_NoResize)){
        SectionLabel("FACTION OVERHAUL LOADER");ImGui::Text("Launcher v%s",kLauncherVersion);ImGui::Text("Installed mod runtime: v%s",runtime_.version.c_str());ImGui::Spacing();
        ImGui::TextWrapped("Native Windows launcher for Assassin's Creed IV: Black Flag Faction Overhaul. It can launch the game, inject or hot-reload the runtime, manage profiles, self-update the launcher, and automatically update the mod from one SHA-256 verified manifest.");
        ImGui::Spacing();ImGui::PushStyleColor(ImGuiCol_Text,kMuted);ImGui::TextWrapped("Auto-update packages are installed into versioned runtime folders. The launcher switches current.json only after download, SHA-256 verification, extraction and package validation succeed. This avoids overwriting a DLL that is already loaded by the game.");ImGui::PopStyleColor();
        ImGui::Spacing();if(ImGui::Button("CLOSE",ImVec2(120,40)))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
}

void LauncherApp::Draw(){
    ImGuiIO& io=ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,kBg);ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);
    ImGui::Begin("##root",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus);
    { auto* dl=ImGui::GetWindowDrawList(); const ImVec2 q=ImGui::GetWindowPos(); const ImVec2 z=ImGui::GetWindowSize(); dl->AddRectFilledMultiColor(q,ImVec2(q.x+z.x,q.y+z.y),IM_COL32(4,14,20,255),IM_COL32(7,27,34,255),IM_COL32(2,9,14,255),IM_COL32(3,14,19,255)); for(float x=q.x+32;x<q.x+z.x;x+=72) dl->AddLine(ImVec2(x,q.y),ImVec2(x,q.y+z.y),IM_COL32(190,160,105,9),1); for(float y=q.y+24;y<q.y+z.y;y+=72) dl->AddLine(ImVec2(q.x,y),ImVec2(q.x+z.x,y),IM_COL32(190,160,105,8),1); dl->AddRect(q,ImVec2(q.x+z.x-1,q.y+z.y-1),IM_COL32(177,145,88,120),0,0,2); }\n    DrawHeader();
    const float logH=205.0f;const float gap=14.0f;const float totalW=ImGui::GetContentRegionAvail().x;const float leftW=335.0f;const float rightW=330.0f;const float centerW=std::max(420.0f,totalW-leftW-rightW-gap*2);
    ImGui::BeginChild("upper",ImVec2(0,ImGui::GetContentRegionAvail().y-logH-gap),false);
    DrawLeftStatus(leftW);ImGui::SameLine(0,gap);DrawCenter(centerW);ImGui::SameLine(0,gap);DrawRightProfile(rightW);ImGui::EndChild();
    ImGui::Dummy(ImVec2(0,gap));DrawRuntimeLog(logH);
    ImGui::End();ImGui::PopStyleVar();ImGui::PopStyleColor();
    DrawSettingsModal();DrawProfilesModal();DrawAboutModal();
}

} // namespace ac4fo::launcher
