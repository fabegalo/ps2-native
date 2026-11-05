// Windows debugger bridge for a running, explicitly pinned ps2-native image.
// Guest execution is paused at run_ee_boot entry; no guest state is patched.
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include "checkpoint_capture_api.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(std::string(text)+" (Win32 "+std::to_string(GetLastError())+")");}
struct Handle {HANDLE h{};~Handle(){if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h);}operator HANDLE()const{return h;}};
struct Image {std::vector<std::uint8_t> bytes;std::uint64_t base{};std::uint32_t text_rva{};std::string text_hash;};
Image image(const std::filesystem::path& path) {
    Image x;x.bytes=ps2native::read_file(path);
    if(x.bytes.size()<sizeof(IMAGE_DOS_HEADER))throw std::runtime_error("truncated image");
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(x.bytes.data());
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || std::size_t(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS64)>x.bytes.size())throw std::runtime_error("invalid image header");
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(x.bytes.data()+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64)throw std::runtime_error("capture requires a matching x64 image");
    x.base=nt->OptionalHeader.ImageBase;
    const auto* s=IMAGE_FIRST_SECTION(nt);
    for(unsigned n=0;n<nt->FileHeader.NumberOfSections;++n,++s) {
        if(reinterpret_cast<const std::uint8_t*>(s+1)>x.bytes.data()+x.bytes.size())throw std::runtime_error("truncated section header");
        if(std::string_view(reinterpret_cast<const char*>(s->Name),5)==".text") {
            if(std::uint64_t(s->PointerToRawData)+s->SizeOfRawData>x.bytes.size())throw std::runtime_error("truncated text section");
            x.text_rva=s->VirtualAddress;x.text_hash=ps2native::sha256_hex(std::span<const std::uint8_t>(x.bytes).subspan(s->PointerToRawData,s->SizeOfRawData));
        }
    }
    if(x.text_hash.empty())throw std::runtime_error("image has no text section");return x;
}
struct Debugger {
    DWORD pid{};Handle process;bool attached{},pending{},break_set{},suspended{};DEBUG_EVENT event{};
    Handle paused_thread;std::uint64_t breakpoint{};std::uint8_t original{};
    void read(std::uint64_t at,void* out,std::size_t size){SIZE_T done{};check(ReadProcessMemory(process,reinterpret_cast<void*>(at),out,size,&done)&&done==size,"read target");}
    void write(std::uint64_t at,const void* in,std::size_t size){SIZE_T done{};check(WriteProcessMemory(process,reinterpret_cast<void*>(at),in,size,&done)&&done==size,"write target");check(FlushInstructionCache(process,reinterpret_cast<void*>(at),size),"flush breakpoint");}
    void restore_break(){if(break_set){write(breakpoint,&original,1);break_set=false;}}
    void resume_event(DWORD status=DBG_CONTINUE){check(ContinueDebugEvent(event.dwProcessId,event.dwThreadId,status),"continue debug event");pending=false;}
    bool next(std::chrono::steady_clock::time_point deadline) {
        while(std::chrono::steady_clock::now()<deadline){if(WaitForDebugEvent(&event,500)){pending=true;return true;}if(GetLastError()!=ERROR_SEM_TIMEOUT)check(false,"wait debug event");}return false;
    }
    void close_event_files() {
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);CloseHandle(event.u.CreateProcessInfo.hProcess);CloseHandle(event.u.CreateProcessInfo.hThread);}
        if(event.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT)CloseHandle(event.u.CreateThread.hThread);
        if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT && event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);
    }
    DWORD disposition()const {return event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT && event.u.Exception.ExceptionRecord.ExceptionCode!=EXCEPTION_BREAKPOINT ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE;}
    void remote_call(std::uint64_t function,void* parameter,std::chrono::steady_clock::time_point deadline) {
        DWORD id{};Handle thread{CreateRemoteThread(process,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(function),parameter,0,&id)};check(thread.h!=nullptr,"start checkpoint adapter thread");
        while(next(deadline)) {
            if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT)throw std::runtime_error("target exited during capture");
            const bool done=event.dwDebugEventCode==EXIT_THREAD_DEBUG_EVENT && event.dwThreadId==id;
            const auto status=disposition();close_event_files();resume_event(status);if(done)return;
        }
        // A capture thread must finish before the guest can safely resume.
        throw std::runtime_error("checkpoint adapter exceeded its bounded operation deadline");
    }
    ~Debugger(){if(!attached)return;try{restore_break();}catch(...){}if(suspended)ResumeThread(paused_thread);if(pending)ContinueDebugEvent(event.dwProcessId,event.dwThreadId,disposition());DebugActiveProcessStop(pid);}
};
std::uint64_t module_base(DWORD pid,const std::filesystem::path& dll) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid)};check(snapshot.h!=INVALID_HANDLE_VALUE,"enumerate target modules");
    MODULEENTRY32W m{};m.dwSize=sizeof(m);if(Module32FirstW(snapshot,&m))do{if(_wcsicmp(m.szExePath,dll.c_str())==0)return reinterpret_cast<std::uint64_t>(m.modBaseAddr);}while(Module32NextW(snapshot,&m));
    throw std::runtime_error("verified adapter was not loaded");
}
}
int wmain(int argc,wchar_t** argv)try {
    if(argc!=10){std::cerr<<"Usage: ps2n-capture PID IMAGE MAP_IMAGE MAP_FILE ADAPTER OUTPUT SCENARIO_SHA256 DISC_SHA256 MIN_RETIRED\n";return 2;}
    const auto pid=static_cast<DWORD>(std::stoul(argv[1]));const auto target=std::filesystem::absolute(argv[2]),map_image=std::filesystem::absolute(argv[3]),dll=std::filesystem::absolute(argv[5]);
    const auto original=image(target),mapped=image(map_image);
    if(original.text_hash!=mapped.text_hash || original.text_rva!=mapped.text_rva || original.base!=mapped.base)throw std::runtime_error("map image code does not match the running image");
    std::ifstream map(argv[4]);std::string line;std::uint64_t address{};
    while(std::getline(map,line))if(line.find("?run_ee_boot@ps2native@@")!=std::string::npos){std::istringstream s(line);std::string section,symbol,absolute;s>>section>>symbol>>absolute;address=std::stoull(absolute,nullptr,16);break;}
    if(address<original.base || address-original.base<original.text_rva)throw std::runtime_error("map does not identify the verified run_ee_boot entry");
    Handle local{nullptr};HMODULE adapter=LoadLibraryW(dll.c_str());check(adapter!=nullptr,"load verified capture adapter");
    const auto get_abi=reinterpret_cast<const CaptureAbi*(*)()>(GetProcAddress(adapter,"ps2n_capture_abi"));check(get_abi!=nullptr,"adapter ABI export");const auto abi=*get_abi();
    if(abi.version!=1 || ps2native::sha256_hex(original.bytes)!=abi.image_sha256)throw std::runtime_error("adapter does not match the pinned target image SHA-256");
    const auto capture=GetProcAddress(adapter,"ps2n_capture_request");check(capture!=nullptr,"adapter capture export");const auto capture_rva=reinterpret_cast<std::uint64_t>(capture)-reinterpret_cast<std::uint64_t>(adapter);
    auto request=std::make_unique<CaptureRequest>();const auto output=std::filesystem::absolute(argv[6]).wstring();if(output.size()>=32768)throw std::runtime_error("output path too long");std::copy(output.begin(),output.end(),request->output);
    const auto ascii=[](const wchar_t* s){std::string r;for(;*s;++s){if(*s>127)throw std::runtime_error("digest must be ASCII");r+=static_cast<char>(*s);}return r;};
    const auto scenario=ascii(argv[7]),disc=ascii(argv[8]);if(scenario.size()!=64 || disc.size()!=64)throw std::runtime_error("explicit SHA-256 identities required");
    std::copy(scenario.begin(),scenario.end(),request->scenario);std::copy(disc.begin(),disc.end(),request->disc_sha256);const auto minimum=std::stoull(argv[9]);
    Debugger d;d.pid=pid;d.process.h=OpenProcess(PROCESS_ALL_ACCESS,FALSE,pid);check(d.process.h!=nullptr,"open the pinned emulator process");wchar_t actual[32768];DWORD length=32768;check(QueryFullProcessImageNameW(d.process,0,actual,&length),"resolve target image");
    if(_wcsicmp(actual,target.c_str())!=0)throw std::runtime_error("PID is not the explicitly pinned executable");
    check(DebugActiveProcess(pid),"attach to verified emulator");d.attached=true;check(DebugSetProcessKillOnExit(FALSE),"disable debugger-exit process termination");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(12);DWORD stepping{};
    while(d.next(deadline)) {
        if(d.event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT)throw std::runtime_error("emulator exited before a safe checkpoint boundary");
        if(d.event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){d.breakpoint=reinterpret_cast<std::uint64_t>(d.event.u.CreateProcessInfo.lpBaseOfImage)+(address-original.base);d.read(d.breakpoint,&d.original,1);if(d.original==0xCC)throw std::runtime_error("entry already has a breakpoint");const std::uint8_t cc=0xCC;d.write(d.breakpoint,&cc,1);d.break_set=true;}
        if(d.event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT) {
            const auto& e=d.event.u.Exception.ExceptionRecord;
            if(e.ExceptionCode==EXCEPTION_SINGLE_STEP && stepping==d.event.dwThreadId){Handle thread{OpenThread(THREAD_ALL_ACCESS,FALSE,stepping)};CONTEXT c{};c.ContextFlags=CONTEXT_ALL;check(GetThreadContext(thread,&c),"read single-step context");c.EFlags&=~0x100u;check(SetThreadContext(thread,&c),"clear single-step");const std::uint8_t cc=0xCC;d.write(d.breakpoint,&cc,1);d.break_set=true;stepping=0;d.resume_event();continue;}
            if(e.ExceptionCode==EXCEPTION_BREAKPOINT && reinterpret_cast<std::uint64_t>(e.ExceptionAddress)==d.breakpoint) {
                d.paused_thread.h=OpenThread(THREAD_ALL_ACCESS,FALSE,d.event.dwThreadId);check(d.paused_thread.h!=nullptr,"open execution thread");CONTEXT c{};c.ContextFlags=CONTEXT_ALL;check(GetThreadContext(d.paused_thread,&c),"read boundary context");d.restore_break();c.Rip=d.breakpoint;
                // MS x64: nontrivial BootReport return in RCX, state in RDX,
                // Memory in R8. Verify the modeled clock binding before use.
                check(SetThreadContext(d.paused_thread,&c),"rewind breakpoint before ABI validation");
                std::uint64_t retired{},clock{};d.read(c.Rdx+abi.retired_offset,&retired,8);d.read(c.R8+abi.cache_clock_offset,&clock,8);
                if(clock!=c.Rdx+abi.retired_offset)throw std::runtime_error("boundary arguments do not match the verified state ABI");
                if(retired<minimum){c.EFlags|=0x100u;stepping=d.event.dwThreadId;check(SetThreadContext(d.paused_thread,&c),"single-step original host entry");CloseHandle(d.paused_thread.h);d.paused_thread.h=nullptr;d.resume_event();continue;}
                check(SetThreadContext(d.paused_thread,&c),"restore original host entry PC");check(SuspendThread(d.paused_thread)!=DWORD(-1),"hold successful execution boundary");d.suspended=true;request->state=c.Rdx;request->memory=c.R8;
                const auto metadata=std::string("{\"capture\":\"verified-run-entry\",\"target_sha256\":\"")+abi.image_sha256+"\",\"retired\":"+std::to_string(retired)+"}";std::copy(metadata.begin(),metadata.end(),request->metadata);
                std::cout<<"SAFE_BOUNDARY retired="<<retired<<" state="<<std::hex<<c.Rdx<<" memory="<<c.R8<<std::dec<<std::endl;d.resume_event();break;
            }
        }
        const auto status=d.disposition();d.close_event_files();d.resume_event(status);
    }
    if(!d.suspended)throw std::runtime_error("no safe run entry before the deadline");
    const auto operation_deadline=std::chrono::steady_clock::now()+std::chrono::minutes(3);
    const auto path_bytes=(dll.wstring().size()+1)*sizeof(wchar_t);auto* remote_path=VirtualAllocEx(d.process,nullptr,path_bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);check(remote_path!=nullptr,"allocate adapter path");d.write(reinterpret_cast<std::uint64_t>(remote_path),dll.c_str(),path_bytes);
    d.remote_call(reinterpret_cast<std::uint64_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW")),remote_path,operation_deadline);VirtualFreeEx(d.process,remote_path,0,MEM_RELEASE);
    const auto remote_module=module_base(pid,dll);auto* remote_request=VirtualAllocEx(d.process,nullptr,sizeof(CaptureRequest),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);check(remote_request!=nullptr,"allocate capture request");d.write(reinterpret_cast<std::uint64_t>(remote_request),request.get(),sizeof(*request));
    d.remote_call(remote_module+capture_rva,remote_request,operation_deadline);d.read(reinterpret_cast<std::uint64_t>(remote_request),request.get(),sizeof(*request));VirtualFreeEx(d.process,remote_request,0,MEM_RELEASE);
    if(request->status!=1)throw std::runtime_error(std::string("capture refused: ")+request->error);
    check(ResumeThread(d.paused_thread)!=DWORD(-1),"resume emulator");d.suspended=false;check(DebugActiveProcessStop(pid),"detach after checkpoint");d.attached=false;FreeLibrary(adapter);
    std::cout<<"COMPLETE_CHECKPOINT_SAVED; original emulator resumed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
