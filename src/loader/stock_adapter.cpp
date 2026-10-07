// Windows v0.5.2 adapter. Never changes the official executable on disk.
#include "stock_identity.inc"
#include <SDL.h>
#include <MinHook.h>
extern "C" {
#include "game_spec.h"
#include "sim_step.h"
#include "genesis_dac.h"
#include "sonic3k_mod_host.h"
#include "online_mod_api.h"
}
#include "stock_profile.h"
#include "stock_bindings.h"
extern "C" {
unsigned char *oa_exe_base;
uint32_t oa_rdb_current_func;
struct OAGuestHook {unsigned pc,rva;void(*fn)(void);};
extern const OAGuestHook oa_guest_hooks[];
extern const size_t oa_guest_hook_count;
const OnlineMod *sonic3k_online_mod(const OnlineModHost *);
}
static const OnlineMod *mod;
static OnlineModHost host;
static GameSpec original_spec;
static bool started;
// Mod-side helper calls run between native frames (or inside an instruction
// callback). They must not deliver an emulated V-int: a V-int can bookmark the
// helper's registers as an interrupted Kosinski decoder and corrupt RAM when
// the real frame resumes. Preserve the scheduler; callers own CPU registers.
static void call_helper(uint32_t pc) {
    const auto cycles=g_cycle_accumulator,audio=g_audio_cycle_counter,threshold=g_vblank_threshold;
    const auto instructions=g_native_insn_count;
    const int rte=g_rte_pending,split=g_split_sp_popped;
    g_cycle_accumulator=0;g_vblank_threshold=0xffffffffu;g_rte_pending=0;g_split_sp_popped=0;
    recomp_call_addr(pc);
    g_cycle_accumulator=cycles;g_audio_cycle_counter=audio;g_vblank_threshold=threshold;
    g_native_insn_count=instructions;g_rte_pending=rte;g_split_sp_popped=split;
}
static int (*step_original)(const GenesisSimInput*,const GenesisSimAudio*,const GenesisSimHooks*);
static int (SDLCALL *poll_original)(SDL_Event*);
static void (SDLCALL *present_original)(SDL_Renderer*);
static void (SDLCALL *destroy_original)(SDL_Renderer*);
static std::vector<void*> hooks;
static bool hook(void *target,void *replacement,void **original=nullptr) {
    auto result=MH_CreateHook(target,replacement,original);
    if(result!=MH_OK) {fprintf(stderr,"[Online Adventure] Hook failed at %p: %s\n",target,MH_StatusToString(result));return false;}
    hooks.push_back(target);return true;
}
static void undo_hooks() {for(void *p:hooks){MH_DisableHook(p);MH_RemoveHook(p);}hooks.clear();}
#include "stock_stack_compat.inc"

#if OA_TEST_CONTROL
static void test_send(const char *);
static void test_poll();
static void test_native_apply();
static void (*test_illegal_original)(uint32_t,uint16_t);
static void test_illegal(uint32_t pc,uint16_t opcode){
    fprintf(stderr,"[OA crash] native PC=%06x opcode=%04x frame=%llu split=%d rte=%d\n",pc,opcode,(unsigned long long)g_frame_count,g_split_sp_popped,g_rte_pending);
    for(unsigned i=0;i<8;i++)fprintf(stderr,"[OA crash] D%u=%08x A%u=%08x\n",i,g_cpu.D[i],i,g_cpu.A[i]);
    void *frames[64];unsigned count=CaptureStackBackTrace(0,64,frames,nullptr);
    for(unsigned i=0;i<count;i++)fprintf(stderr,"[OA crash] stack %u exe+rva=%llx\n",i,(unsigned long long)((uintptr_t)frames[i]-(uintptr_t)oa_exe_base));
    FILE *dump=nullptr;if(!fopen_s(&dump,"native-crash.ram","wb")){fwrite(g_ram,1,65536,dump);fclose(dump);}
    fflush(stderr);test_illegal_original(pc,opcode);
}
static unsigned test_pending_stage=~0u;
static void (*test_level_original)();
static void test_level_entry(){
    if(test_pending_stage!=~0u){
        const auto stage=uint16_t(test_pending_stage);test_pending_stage=~0u;
        m68k_write8(0xfff711,0);m68k_write8(0xfffe2a,0);m68k_write8(0xfffe48,0);
        m68k_write8(0xfffe12,9);m68k_write16(0xfffe10,stage);m68k_write16(0xffee4e,stage);
    }
    test_level_original();
}
#endif
static void response(const char *text) {
#if OA_TEST_CONTROL
    test_send(text);
#else
    fprintf(stderr,"[Online Adventure] %s\n",text);
#endif
}
static void error(int id,const char *message) {char line[512];snprintf(line,sizeof line,"{\"id\":%d,\"ok\":false,\"error\":\"%s\"}",id,message);response(line);}
#include "stock_renderer.inc"
static uint32_t cram_argb(int i) {auto *v=reinterpret_cast<GVDP*>(oa_exe_base+OA_RVA_g_machine);return i>=0&&i<64?genesis_dac_cram_to_argb(v->cram[i],GENESIS_DAC_NORMAL):0xff000000u;}
static void world_view(int *left,int *top) {if(s3_video_world_view(left,top))return;int width=((int(*)())(oa_exe_base+OA_RVA_s3_video_canvas_width))();*left=((g_ram[0xee80]<<8)|g_ram[0xee81])-(width-320)/2;*top=(g_ram[0xee84]<<8)|g_ram[0xee85];}
extern "C" int oa_instruction(uint32_t pc) {
    if(mod&&mod->instruction(pc))return 1;
#if OA_TEST_CONTROL
    if(pc==0x1aafc&&(g_cpu.A[0]&65535)==0xb000)test_native_apply();
#endif
    return original_spec.instruction_hook?original_spec.instruction_hook(pc):0;
}
extern "C" uint32_t oa_sprite_tint(uint16_t a,uint8_t i,uint32_t c) {return mod?mod->tint(a,i,c):c;}
static int dispatch(unsigned pc) {
    for(size_t i=0;i<oa_guest_hook_count;i++)if(!oa_guest_hooks[i].rva&&oa_guest_hooks[i].pc==pc){oa_guest_hooks[i].fn();return 1;}
#if OA_TEST_CONTROL
    static unsigned traced;
    if(pc==0x86904&&traced++<3){
        fprintf(stderr,"[OA dispatch] pc=%06x D0=%08x D1=%08x A0=%08x A1=%08x A2=%08x SP=%08x\n",pc,g_cpu.D[0],g_cpu.D[1],g_cpu.A[0],g_cpu.A[1],g_cpu.A[2],g_cpu.A[7]);
        for(unsigned i=0;i<32;i+=4)fprintf(stderr,"[OA dispatch] stack+%u=%08x\n",i,m68k_read32(g_cpu.A[7]+i));
        FILE *dump=nullptr;if(!fopen_s(&dump,"native-dispatch.ram","wb")){fwrite(g_ram,1,65536,dump);fclose(dump);}fflush(stderr);
    }
#endif
    return original_spec.dispatch_override?original_spec.dispatch_override(pc):0;
}
static void load_settings(const char *path) {if(original_spec.load_settings)original_spec.load_settings(path);s3k_online_load_settings(path);}
static const RecompLauncherCModProvider *mods(const RecompLauncherCModProvider *base) {return s3k_online_mods(original_spec.mods?original_spec.mods(base):base);}
static int netplay_allowed() {return !s3k_online_enabled()&&(!original_spec.netplay_allowed||original_spec.netplay_allowed());}
static const char *state_reason() {if(mod&&mod->active())return "Leave Online Adventure before using save states";return original_spec.state_unavailable_reason?original_spec.state_unavailable_reason():nullptr;}
static bool initialize_mod() {
    if(started)return mod!=nullptr;
    started=true;if(!s3k_online_enabled())return false;
    if(original_spec.netplay_allowed&&!original_spec.netplay_allowed()){fprintf(stderr,"[Online Adventure] Disable Knuckles & Knuckles before enabling Online Adventure.\n");return false;}
    const size_t first=hooks.size();
    for(size_t i=0;i<oa_guest_hook_count;i++)if(oa_guest_hooks[i].rva&&oa_guest_hooks[i].rva!=0x356980)
        if(!hook(oa_exe_base+oa_guest_hooks[i].rva,(void*)oa_guest_hooks[i].fn))goto failed;
    if(!paired_stack_hooks())goto failed;
#if OA_TEST_CONTROL
    if(!hook(oa_exe_base+661696,reinterpret_cast<void*>(test_level_entry),reinterpret_cast<void**>(&test_level_original)))goto failed;
    if(!hook(oa_exe_base+OA_RVA_m68k_illegal_trap,reinterpret_cast<void*>(test_illegal),reinterpret_cast<void**>(&test_illegal_original)))goto failed;
#endif
    for(size_t i=first;i<hooks.size();i++)if(MH_QueueEnableHook(hooks[i])!=MH_OK)goto failed;
    if(MH_ApplyQueued()!=MH_OK)goto failed;
    {
        auto *v=reinterpret_cast<GVDP*>(oa_exe_base+OA_RVA_g_machine);
        host={ONLINE_MOD_ABI,sizeof host,sizeof(M68KState),sizeof g_rom,ONLINE_MOD_HOST,&g_cpu,g_ram,g_rom,v->vram,v->cram,m68k_write8,m68k_write16,m68k_write32,call_helper,recomp_tail_call,response,error,cram_argb,world_view};
        mod=sonic3k_online_mod(&host);if(!mod)goto failed;
        // Only these three dispatch entries get the switch callback. The stock
        // linker folds 103 unrelated routines into the same native stub.
        struct Entry {unsigned rva;void(*fn)();};
        const Entry switches[]={{OA_RVA_switch_entry_2c626,func_02C626},{OA_RVA_switch_entry_2c690,func_02C690},{OA_RVA_switch_entry_42d76,func_042D76}};
        for(auto &e:switches) {auto target=reinterpret_cast<void(**)()>(oa_exe_base+e.rva);DWORD old;VirtualProtect(target,sizeof *target,PAGE_READWRITE,&old);*target=e.fn;VirtualProtect(target,sizeof *target,old,&old);}
        mod->init();fprintf(stderr,"[Online Adventure] Online mod initialized in unchanged official executable; %zu guest hooks.\n",oa_guest_hook_count);return true;
    }
failed:
    for(size_t i=first;i<hooks.size();i++){MH_DisableHook(hooks[i]);MH_RemoveHook(hooks[i]);}hooks.resize(first);
    fprintf(stderr,"[Online Adventure] Gameplay hook transaction aborted; original game remains available.\n");return false;
}
#if OA_TEST_CONTROL
#include "stock_test_control.inc"
#endif
static int step(const GenesisSimInput *input,const GenesisSimAudio *audio,const GenesisSimHooks *h) {
    initialize_mod();if(!mod)return step_original(input,audio,h);
    mod->poll();
#if OA_TEST_CONTROL
    test_poll();
#endif
    mod->pre(g_frame_count);
    #if OA_TEST_CONTROL
    test_frame_write();
    #endif
    GenesisSimInput in=input?*input:GenesisSimInput{};
    #if OA_TEST_CONTROL
    if(test_pad_set)in.pad[0]=test_pad;
#endif
    for(int p=0;p<GENESIS_SIM_MAX_PLAYERS;p++)in.pad[p]=mod->pad(p,in.pad[p]);
    int result=step_original(&in,audio,h);mod->post(g_frame_count);return result;
}
static int SDLCALL poll(SDL_Event *e) {if(mod)mod->poll();int result;while((result=poll_original(e))&&e&&mod&&mod->event(e)){}return result;}
static void SDLCALL present(SDL_Renderer *r) {static unsigned draws;if(!draws++)fprintf(stderr,"[Online Adventure] First presentation callback, module=%p\n",mod);if(mod){int w=0,h=0;SDL_RenderGetLogicalSize(r,&w,&h);if(!w||!h)SDL_GetRendererOutputSize(r,&w,&h);mod->draw(SDL_RenderGetWindow(r),r,w,h);}present_original(r);}
static void SDLCALL destroy(SDL_Renderer *r) {if(mod){mod->shutdown();mod=nullptr;}destroy_original(r);}
extern "C" __declspec(dllexport) int online_loader_start(HMODULE exe) {
    if(!official_image(exe))return 0;
    oa_exe_base=reinterpret_cast<unsigned char*>(exe);
    auto *spec=reinterpret_cast<GameSpec*>(oa_exe_base+OA_RVA_g_game_spec);original_spec=*spec;
    if(spec->expected_rom_size!=0x400000)return 0;
    if(MH_Initialize()!=MH_OK)return 0;
    if(!hook(oa_exe_base+OA_RVA_genesis_sim_step,(void*)step,(void**)&step_original)||!renderer_hooks()) {undo_hooks();return 0;}

    for(void *p:hooks)if(MH_QueueEnableHook(p)!=MH_OK){undo_hooks();return 0;}
    if(MH_ApplyQueued()!=MH_OK){undo_hooks();return 0;}
    // Patch this executable's imports, not SDL's runtime-dispatched entry stubs.
    struct Import {unsigned rva;void *replacement;void **original;};
    const Import imports[]={{0x1984598,(void*)poll,(void**)&poll_original},{0x1984578,(void*)present,(void**)&present_original},{0x1984588,(void*)destroy,(void**)&destroy_original}};
    for(auto &i:imports){void **slot=reinterpret_cast<void**>(oa_exe_base+i.rva);DWORD old;if(!VirtualProtect(slot,sizeof *slot,PAGE_READWRITE,&old)){undo_hooks();return 0;}*i.original=*slot;*slot=i.replacement;VirtualProtect(slot,sizeof *slot,old,&old);}
    auto *video=reinterpret_cast<GameVideo*>(oa_exe_base+OA_RVA_sonic3_video);
    DWORD video_old;if(!VirtualProtect(video,sizeof *video,PAGE_READWRITE,&video_old)){undo_hooks();return 0;}
    *video=sonic3_video;VirtualProtect(video,sizeof *video,video_old,&video_old);
    DWORD old;if(!VirtualProtect(spec,sizeof *spec,PAGE_READWRITE,&old)){undo_hooks();return 0;}
    spec->load_settings=load_settings;spec->mods=mods;spec->netplay_allowed=netplay_allowed;spec->state_unavailable_reason=state_reason;spec->dispatch_override=dispatch;
    VirtualProtect(spec,sizeof *spec,old,&old);
    fprintf(stderr,"[Online Adventure] Official v0.5.2 executable verified; drop-in startup adapter entered.\n");return 1;
}
