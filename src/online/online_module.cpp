#include <SDL.h>
#include <cstring>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "online_mod_api.h"
#include "sonic3k_coop.h"
const OnlineModHost *online_host;
int online_margin;
int online_view_left,online_view_top;
int (*online_hooks[32])(void);
uint32_t (*online_tint)(uint16_t,uint8_t,uint32_t);
static ImGuiContext *context;
static SDL_Window *window;
static void create_ui(SDL_Window *w,SDL_Renderer *r) {
    if(context)return;
    context=ImGui::CreateContext(); window=w;
    ImGui::SetCurrentContext(context);
    auto &io=ImGui::GetIO();io.IniFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(w,r);ImGui_ImplSDLRenderer2_Init(r);
}
static int event(const SDL_Event *e) {
    if(context) {ImGui::SetCurrentContext(context);ImGui_ImplSDL2_ProcessEvent(e);}
    if(s3k_coop_event(e))return 1;
    if(context && s3k_coop_ui_visible()) {
        auto &io=ImGui::GetIO();
        if(io.WantCaptureMouse && e->type>=SDL_MOUSEMOTION && e->type<=SDL_MOUSEWHEEL)return 1;
        if(io.WantCaptureKeyboard && (e->type==SDL_KEYDOWN || e->type==SDL_KEYUP || e->type==SDL_TEXTINPUT))return 1;
    }
    return 0;
}
static void draw(SDL_Window *w,SDL_Renderer *r,int width,int height) {
    create_ui(w,r);ImGui::SetCurrentContext(context);
    online_margin=(width-320)/2;
    online_host->world_view(&online_view_left,&online_view_top);
    s3k_coop_draw_player(r);
    if(s3k_coop_ui_visible()) {
        SDL_RenderSetLogicalSize(r,0,0);
        ImGui_ImplSDLRenderer2_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();
        s3k_coop_draw_ui();ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),r);
        SDL_RenderSetLogicalSize(r,width,height);
    }
    s3k_coop_after_ui(r);
}
static void shutdown() {
    s3k_coop_shutdown();
    if(context) {ImGui::SetCurrentContext(context);ImGui_ImplSDLRenderer2_Shutdown();ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext(context);context=nullptr;}
}
static uint32_t tint(uint16_t attr,uint8_t i,uint32_t color) {return online_tint?online_tint(attr,i,color):color;}
static int instruction(uint32_t pc) {
    int slot=0;
    switch(pc) {
    case 0x02dce2: slot=11; break;
    case 0x061f10: slot=12; break;
    case 0x067712: slot=13; break;
    case 0x006238: slot=14; break;
    case 0x006276: slot=14; break;
    case 0x006894: slot=14; break;
    case 0x00694c: slot=14; break;
    case 0x0069ac: slot=14; break;
    case 0x006de4: slot=14; break;
    case 0x006eb0: slot=14; break;
    case 0x006eee: slot=14; break;
    case 0x0078b0: slot=14; break;
    case 0x0078c8: slot=14; break;
    case 0x007d78: slot=14; break;
    case 0x007da4: slot=14; break;
    case 0x00c46e: slot=14; break;
    case 0x01be72: slot=14; break;
    case 0x01bf2c: slot=14; break;
    case 0x01bf6e: slot=14; break;
    case 0x01c4b4: slot=14; break;
    case 0x01c5a6: slot=14; break;
    case 0x01c654: slot=14; break;
    case 0x026982: slot=14; break;
    case 0x0270dc: slot=14; break;
    case 0x032100: slot=14; break;
    case 0x0455a2: slot=14; break;
    case 0x045b9a: slot=14; break;
    case 0x04fa48: slot=14; break;
    case 0x04fe6e: slot=14; break;
    case 0x051eb6: slot=14; break;
    case 0x052160: slot=14; break;
    case 0x053614: slot=14; break;
    case 0x053754: slot=14; break;
    case 0x0537b4: slot=14; break;
    case 0x053eee: slot=14; break;
    case 0x053f40: slot=14; break;
    case 0x054112: slot=14; break;
    case 0x0541a2: slot=14; break;
    case 0x05438a: slot=14; break;
    case 0x054ad4: slot=14; break;
    case 0x054aee: slot=14; break;
    case 0x054fbe: slot=14; break;
    case 0x055354: slot=14; break;
    case 0x05561c: slot=14; break;
    case 0x055680: slot=14; break;
    case 0x0556de: slot=14; break;
    case 0x056b7c: slot=14; break;
    case 0x057124: slot=14; break;
    case 0x05a134: slot=14; break;
    case 0x05a1b0: slot=14; break;
    case 0x062118: slot=14; break;
    case 0x0622a4: slot=14; break;
    case 0x0624e4: slot=14; break;
    case 0x062646: slot=14; break;
    case 0x0628b0: slot=14; break;
    case 0x062caa: slot=14; break;
    case 0x062e4e: slot=14; break;
    case 0x0630e6: slot=14; break;
    case 0x0632e0: slot=14; break;
    case 0x06344c: slot=14; break;
    case 0x063a70: slot=14; break;
    case 0x063b28: slot=14; break;
    case 0x063d20: slot=14; break;
    case 0x065bda: slot=14; break;
    case 0x067b12: slot=14; break;
    case 0x067bde: slot=14; break;
    case 0x067c70: slot=14; break;
    case 0x068a7c: slot=14; break;
    case 0x068af2: slot=14; break;
    case 0x0691b8: slot=14; break;
    case 0x0693ac: slot=14; break;
    case 0x069552: slot=14; break;
    case 0x0697e4: slot=14; break;
    case 0x06aec0: slot=14; break;
    case 0x06afd4: slot=14; break;
    case 0x06b11a: slot=14; break;
    case 0x06c6ea: slot=14; break;
    case 0x06fd70: slot=14; break;
    case 0x07005c: slot=14; break;
    case 0x0700da: slot=14; break;
    case 0x07072a: slot=14; break;
    case 0x0707aa: slot=14; break;
    case 0x0707c2: slot=14; break;
    case 0x071406: slot=14; break;
    case 0x071cd2: slot=14; break;
    case 0x0724f4: slot=14; break;
    case 0x072568: slot=14; break;
    case 0x0728ba: slot=14; break;
    case 0x072a02: slot=14; break;
    case 0x0735f2: slot=14; break;
    case 0x075494: slot=14; break;
    case 0x075b0a: slot=14; break;
    case 0x076146: slot=14; break;
    case 0x07634c: slot=14; break;
    case 0x076838: slot=14; break;
    case 0x077736: slot=14; break;
    case 0x080412: slot=14; break;
    case 0x086404: slot=14; break;
    case 0x0886c0: slot=14; break;
    case 0x088726: slot=14; break;
    case 0x08a296: slot=14; break;
    case 0x08a512: slot=14; break;
    case 0x08cb5c: slot=14; break;
    case 0x08cdbe: slot=14; break;
    case 0x08ce90: slot=14; break;
    case 0x08cf3c: slot=14; break;
    case 0x08f0d0: slot=14; break;
    case 0x08f422: slot=14; break;
    case 0x006918: slot=15; break;
    case 0x01bed4: slot=15; break;
    case 0x010156: slot=16; break;
    case 0x01a350: slot=16; break;
    case 0x06de84: slot=25; break;
    case 0x02c626: slot=18; break;
    case 0x02c690: slot=18; break;
    case 0x042d76: slot=18; break;
    case 0x0642ca: slot=19; break;
    case 0x06f9a6: slot=20; break;
    case 0x079f6a: slot=21; break;
    case 0x07fba2: slot=22; break;
    case 0x082dec: slot=23; break;
    case 0x010c98: slot=24; break;
    case 0x0138fe: slot=24; break;
    case 0x01664a: slot=24; break;
    case 0x001488: slot=26; break;
    default:return s3k_coop_world_instruction(pc);
    }
    int replace=online_hooks[slot]?online_hooks[slot]():0;
    if(slot==11 && replace) {online_host->tail_call(0x02dcd4);return 1;}
    return 0;
}
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
EXPORT const OnlineMod *sonic3k_online_mod(const OnlineModHost *host) {
    if(!host || host->abi!=ONLINE_MOD_ABI || host->size!=sizeof(OnlineModHost) ||
       host->cpu_size!=sizeof(M68KState) || host->rom_size!=0x400000 ||
       !host->host_id || strcmp(host->host_id,ONLINE_MOD_HOST))return nullptr;
    online_host=host;
    static const OnlineMod api={ONLINE_MOD_ABI,sizeof(OnlineMod),s3k_coop_init,s3k_coop_poll,
        s3k_coop_pre,s3k_coop_frame,s3k_coop_pad,s3k_coop_active,event,draw,shutdown,
        s3k_coop_debug,instruction,tint};
    return &api;
}
