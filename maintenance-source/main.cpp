#include <Windows.h>
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#include "launcher_app.h"
#include "launcher_io.h"
#include <Windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")

namespace {
ID3D11Device* g_device=nullptr;
ID3D11DeviceContext* g_context=nullptr;
IDXGISwapChain* g_swapChain=nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView=nullptr;

void CreateRenderTarget(){
    ID3D11Texture2D* backBuffer=nullptr;
    if(g_swapChain && SUCCEEDED(g_swapChain->GetBuffer(0,IID_PPV_ARGS(&backBuffer)))){
        g_device->CreateRenderTargetView(backBuffer,nullptr,&g_mainRenderTargetView);
        backBuffer->Release();
    }
}
void CleanupRenderTarget(){if(g_mainRenderTargetView){g_mainRenderTargetView->Release();g_mainRenderTargetView=nullptr;}}
bool CreateDeviceD3D(HWND hwnd){
    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferCount=2;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=hwnd;sd.SampleDesc.Count=1;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    UINT flags=0;D3D_FEATURE_LEVEL featureLevel;const D3D_FEATURE_LEVEL levels[2]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0};
    const HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,levels,2,D3D11_SDK_VERSION,&sd,&g_swapChain,&g_device,&featureLevel,&g_context);
    if(FAILED(hr))return false;CreateRenderTarget();return true;
}
void CleanupDeviceD3D(){CleanupRenderTarget();if(g_swapChain){g_swapChain->Release();g_swapChain=nullptr;}if(g_context){g_context->Release();g_context=nullptr;}if(g_device){g_device->Release();g_device=nullptr;}}

LRESULT WINAPI WndProc(HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam){
    if(ImGui_ImplWin32_WndProcHandler(hWnd,msg,wParam,lParam))return true;
    switch(msg){
        case WM_SIZE:if(g_device&&wParam!=SIZE_MINIMIZED){CleanupRenderTarget();g_swapChain->ResizeBuffers(0,(UINT)LOWORD(lParam),(UINT)HIWORD(lParam),DXGI_FORMAT_UNKNOWN,0);CreateRenderTarget();}return 0;
        case WM_SYSCOMMAND:if((wParam&0xfff0)==SC_KEYMENU)return 0;break;
        case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hWnd,msg,wParam,lParam);
}

void ConfigureStyle(){
    ImGuiStyle& s=ImGui::GetStyle();s.WindowPadding=ImVec2(16,16);s.FramePadding=ImVec2(12,9);s.ItemSpacing=ImVec2(10,9);s.ItemInnerSpacing=ImVec2(8,6);s.ScrollbarSize=14;s.GrabMinSize=14;s.WindowBorderSize=0;s.ChildBorderSize=1;s.PopupBorderSize=1;s.FrameBorderSize=1;s.WindowRounding=0;s.ChildRounding=5;s.FrameRounding=4;s.PopupRounding=5;s.ScrollbarRounding=5;s.GrabRounding=4;s.TabRounding=4;
    auto& c=s.Colors;c[ImGuiCol_Text]=ImVec4(.88f,.86f,.79f,1);c[ImGuiCol_TextDisabled]=ImVec4(.45f,.48f,.49f,1);c[ImGuiCol_WindowBg]=ImVec4(.025f,.055f,.073f,1);c[ImGuiCol_ChildBg]=ImVec4(.025f,.055f,.073f,.92f);c[ImGuiCol_PopupBg]=ImVec4(.03f,.07f,.09f,.99f);c[ImGuiCol_Border]=ImVec4(.53f,.43f,.29f,.75f);c[ImGuiCol_FrameBg]=ImVec4(.035f,.082f,.105f,1);c[ImGuiCol_FrameBgHovered]=ImVec4(.06f,.12f,.15f,1);c[ImGuiCol_FrameBgActive]=ImVec4(.08f,.15f,.18f,1);c[ImGuiCol_TitleBg]=ImVec4(.02f,.045f,.06f,1);c[ImGuiCol_TitleBgActive]=c[ImGuiCol_TitleBg];c[ImGuiCol_CheckMark]=ImVec4(.80f,.68f,.48f,1);c[ImGuiCol_SliderGrab]=ImVec4(.80f,.68f,.48f,1);c[ImGuiCol_SliderGrabActive]=ImVec4(.95f,.80f,.56f,1);c[ImGuiCol_Button]=ImVec4(.035f,.082f,.105f,1);c[ImGuiCol_ButtonHovered]=ImVec4(.06f,.13f,.16f,1);c[ImGuiCol_ButtonActive]=ImVec4(.10f,.17f,.19f,1);c[ImGuiCol_Header]=ImVec4(.09f,.16f,.18f,1);c[ImGuiCol_HeaderHovered]=ImVec4(.13f,.22f,.24f,1);c[ImGuiCol_HeaderActive]=ImVec4(.18f,.26f,.27f,1);c[ImGuiCol_Separator]=ImVec4(.53f,.43f,.29f,.55f);c[ImGuiCol_ResizeGrip]=ImVec4(.8f,.68f,.48f,.22f);c[ImGuiCol_ResizeGripHovered]=ImVec4(.8f,.68f,.48f,.55f);
}
}

int WINAPI wWinMain(HINSTANCE hInstance,HINSTANCE,LPWSTR,int){
    WNDCLASSEXW wc{sizeof(wc),CS_CLASSDC,WndProc,0L,0L,hInstance,nullptr,nullptr,nullptr,nullptr,L"FactionOverhaulLauncher",nullptr};
    RegisterClassExW(&wc);
    HWND hwnd=CreateWindowW(wc.lpszClassName,L"Faction Overhaul Loader",WS_OVERLAPPEDWINDOW,100,60,1420,960,nullptr,nullptr,wc.hInstance,nullptr);
    if(!hwnd)return 1;
    BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));
    if(!CreateDeviceD3D(hwnd)){CleanupDeviceD3D();UnregisterClassW(wc.lpszClassName,wc.hInstance);return 2;}
    ShowWindow(hwnd,SW_SHOWDEFAULT);UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();ImGui::CreateContext();ImGuiIO& io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;io.IniFilename=nullptr;
    io.Fonts->AddFontDefault();
    const char* segoe="C:\\Windows\\Fonts\\segoeui.ttf";const char* georgia="C:\\Windows\\Fonts\\georgia.ttf";
    if(GetFileAttributesA(segoe)!=INVALID_FILE_ATTRIBUTES){ if(ImFont* f=io.Fonts->AddFontFromFileTTF(segoe,18.0f)) io.FontDefault=f; }
    if(GetFileAttributesA(georgia)!=INVALID_FILE_ATTRIBUTES)io.Fonts->AddFontFromFileTTF(georgia,22.0f);
    ConfigureStyle();
    ImGui_ImplWin32_Init(hwnd);ImGui_ImplDX11_Init(g_device,g_context);

    ac4fo::launcher::LauncherApp app(ac4fo::launcher::ExecutableDirectory());app.Initialize();
    bool done=false;while(!done&&!app.WantsClose()){
        MSG msg;while(PeekMessage(&msg,nullptr,0U,0U,PM_REMOVE)){TranslateMessage(&msg);DispatchMessage(&msg);if(msg.message==WM_QUIT)done=true;}if(done)break;
        app.Tick();
        ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();app.Draw();ImGui::Render();
        const float clear[4]={0.025f,0.055f,0.073f,1.0f};g_context->OMSetRenderTargets(1,&g_mainRenderTargetView,nullptr);g_context->ClearRenderTargetView(g_mainRenderTargetView,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1,0);
    }
    ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();CleanupDeviceD3D();DestroyWindow(hwnd);UnregisterClassW(wc.lpszClassName,wc.hInstance);return 0;
}
