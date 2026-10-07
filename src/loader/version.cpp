#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>
#include <stdint.h>
static HMODULE module;
static HMODULE real_version;
static BYTE entry_bytes[14];
static BYTE *entry_address;
static FARPROC version_proc(const char *name) {
    if(!real_version) {
        wchar_t path[MAX_PATH];GetSystemDirectoryW(path,MAX_PATH);
        lstrcatW(path,L"\\version.dll");real_version=LoadLibraryW(path);
    }
    return real_version?GetProcAddress(real_version,name):nullptr;
}
#define FORWARD(ret,name,params,args) extern "C" ret WINAPI proxy_##name params { \
    auto fn=reinterpret_cast<ret(WINAPI*)params>(version_proc(#name));return fn?fn args:0; }
FORWARD(BOOL,GetFileVersionInfoA,(LPCSTR a,DWORD b,DWORD c,LPVOID d),(a,b,c,d))
FORWARD(BOOL,GetFileVersionInfoW,(LPCWSTR a,DWORD b,DWORD c,LPVOID d),(a,b,c,d))
FORWARD(BOOL,GetFileVersionInfoExA,(DWORD a,LPCSTR b,DWORD c,DWORD d,LPVOID e),(a,b,c,d,e))
FORWARD(BOOL,GetFileVersionInfoExW,(DWORD a,LPCWSTR b,DWORD c,DWORD d,LPVOID e),(a,b,c,d,e))
FORWARD(DWORD,GetFileVersionInfoSizeA,(LPCSTR a,LPDWORD b),(a,b))
FORWARD(DWORD,GetFileVersionInfoSizeW,(LPCWSTR a,LPDWORD b),(a,b))
FORWARD(DWORD,GetFileVersionInfoSizeExA,(DWORD a,LPCSTR b,LPDWORD c),(a,b,c))
FORWARD(DWORD,GetFileVersionInfoSizeExW,(DWORD a,LPCWSTR b,LPDWORD c),(a,b,c))
FORWARD(BOOL,VerQueryValueA,(LPCVOID a,LPCSTR b,LPVOID *c,PUINT d),(a,b,c,d))
FORWARD(BOOL,VerQueryValueW,(LPCVOID a,LPCWSTR b,LPVOID *c,PUINT d),(a,b,c,d))
FORWARD(DWORD,VerLanguageNameA,(DWORD a,LPSTR b,DWORD c),(a,b,c))
FORWARD(DWORD,VerLanguageNameW,(DWORD a,LPWSTR b,DWORD c),(a,b,c))
FORWARD(DWORD,VerFindFileA,(DWORD a,LPCSTR b,LPCSTR c,LPCSTR d,LPSTR e,PUINT f,LPSTR g,PUINT h),(a,b,c,d,e,f,g,h))
FORWARD(DWORD,VerFindFileW,(DWORD a,LPCWSTR b,LPCWSTR c,LPCWSTR d,LPWSTR e,PUINT f,LPWSTR g,PUINT h),(a,b,c,d,e,f,g,h))
FORWARD(DWORD,VerInstallFileA,(DWORD a,LPCSTR b,LPCSTR c,LPCSTR d,LPCSTR e,LPCSTR f,LPSTR g,PUINT h),(a,b,c,d,e,f,g,h))
FORWARD(DWORD,VerInstallFileW,(DWORD a,LPCWSTR b,LPCWSTR c,LPCWSTR d,LPCWSTR e,LPCWSTR f,LPWSTR g,PUINT h),(a,b,c,d,e,f,g,h))
static int startup() {
    DWORD old;VirtualProtect(entry_address,sizeof entry_bytes,PAGE_EXECUTE_READWRITE,&old);
    memcpy(entry_address,entry_bytes,sizeof entry_bytes);VirtualProtect(entry_address,sizeof entry_bytes,old,&old);
    FlushInstructionCache(GetCurrentProcess(),entry_address,sizeof entry_bytes);
    wchar_t path[32768];GetModuleFileNameW(module,path,32768);
    wchar_t *last=wcsrchr(path,L'\\');if(last)last[1]=0;
    lstrcatW(path,L"mods\\online-adventure\\OnlineAdventure.dll");
    HMODULE online=LoadLibraryW(path);
    auto initialize=online?reinterpret_cast<int(*)(HMODULE)>(GetProcAddress(online,"online_loader_start")):nullptr;
    if(!initialize || !initialize(GetModuleHandleW(nullptr)))
        MessageBoxW(nullptr,L"Online Adventure could not load for this game version. The original game will continue. Check that the complete mod is installed for the supported official Windows release.",L"Online Adventure",MB_OK|MB_ICONWARNING);
    return reinterpret_cast<int(*)()>(entry_address)();
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason!=DLL_PROCESS_ATTACH)return TRUE;
    module=instance;DisableThreadLibraryCalls(instance);
    auto base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return TRUE;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64)return TRUE;
    wchar_t path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);wchar_t *name=wcsrchr(path,L'\\');
    if(_wcsicmp(name?name+1:path,L"Sonic3KRecomp.exe"))return TRUE;
    entry_address=base+nt->OptionalHeader.AddressOfEntryPoint;
    DWORD old;if(!VirtualProtect(entry_address,sizeof entry_bytes,PAGE_EXECUTE_READWRITE,&old))return TRUE;
    memcpy(entry_bytes,entry_address,sizeof entry_bytes);
    BYTE jump[14]={0xff,0x25};uintptr_t target=reinterpret_cast<uintptr_t>(&startup);memcpy(jump+6,&target,8);
    memcpy(entry_address,jump,sizeof jump);VirtualProtect(entry_address,sizeof entry_bytes,old,&old);
    FlushInstructionCache(GetCurrentProcess(),entry_address,sizeof entry_bytes);return TRUE;
}
