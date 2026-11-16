#include "ps2native/engine/console.hpp"
#include "ps2native/engine/graphics_output.hpp"
#include "ps2native/io.hpp"
#include "gs/gpu/gpu.hpp"
#include <chrono>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif
namespace ps2native::engine {
void capture_graphics(Console& c,const std::filesystem::path& path) {
    if(!c.graphics_initialized)throw std::runtime_error("Guest did not initialize graphics; no frame to capture");
    std::vector<std::uint32_t> rgba;std::uint32_t w=0,h=0;
    if(!ps2::gs::display(rgba,w,h))throw std::runtime_error("Guest has no active GS display circuit");
    std::vector<std::uint8_t> bmp(54+rgba.size()*4);
    const auto put=[&](unsigned at,std::uint32_t value){ps2::store(bmp.data()+at,value);};
    bmp[0]='B';bmp[1]='M';put(2,bmp.size());put(10,54);put(14,40);put(18,w);put(22,0u-h);bmp[26]=1;bmp[28]=32;put(34,rgba.size()*4);
    for(std::size_t i=0;i<rgba.size();++i){const auto p=rgba[i];put(54+i*4,(p&0xff00ff00)|((p&0xff)<<16)|((p>>16)&0xff));}
    ps2native::write_file_atomically(path,bmp);
    std::fprintf(stderr,"[frame] GS compute capture %ux%u\n",w,h);
}
void present_graphics(Console& c,unsigned milliseconds) {
    if(!c.graphics_initialized || !milliseconds || milliseconds>5000)throw std::runtime_error("Invalid guest presentation request");
#ifdef _WIN32
    const auto instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=instance;wc.lpszClassName=L"PS2NativeEngineGraphics";
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register graphics window");
    const auto window=CreateWindowW(wc.lpszClassName,L"PS2Native Engine - guest framebuffer",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,640,480,nullptr,nullptr,instance,nullptr);
    if(!window)throw std::runtime_error("Cannot create graphics window");
    struct Cleanup {HWND window;~Cleanup(){ps2::gs::gpu::detach_surface();if(IsWindow(window))DestroyWindow(window);}} cleanup{window};
    ShowWindow(window,SW_SHOW);RECT client{};GetClientRect(window,&client);
    if(!ps2::gs::attach_window(3,instance,0,window,nullptr,client.right,client.bottom))throw std::runtime_error("Cannot attach WebGPU presentation surface");
    const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    unsigned frames=0;auto width=client.right,height=client.bottom;
    while(IsWindow(window) && std::chrono::steady_clock::now()<until) {
        MSG msg{};while(PeekMessageW(&msg,window,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        if(!IsWindow(window))break;
        GetClientRect(window,&client);
        if(!client.right || !client.bottom){Sleep(10);continue;}
        if(client.right!=width || client.bottom!=height){width=client.right;height=client.bottom;ps2::gs::resize_window(width,height);}
        if(!ps2::gs::present())throw std::runtime_error("GS presentation failed");
        ++frames;
    }
    std::fprintf(stderr,"[frame] WebGPU surface presentations=%u\n",frames);
#else
    throw std::runtime_error("Native window presentation adapter is not implemented for this host");
#endif
}
}
