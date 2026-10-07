#include <stdio.h>
#include <stdlib.h>
#pragma once
#include "genesis_runtime.h"
#include "reverse_debug.h"
#ifdef __cplusplus
extern "C" {
#endif
extern unsigned char *oa_exe_base;
int oa_instruction(uint32_t);
uint32_t oa_sprite_tint(uint16_t,uint8_t,uint32_t);
extern uint32_t oa_rdb_current_func;
#define g_rdb_current_func oa_rdb_current_func
#if !SONIC_REVERSE_DEBUG
static inline void rdb_on_block(uint32_t p) {(void)p;}
static inline void rdb_on_insn(uint32_t p) {(void)p;}
#endif
#define g_cpu (*(M68KState *)(oa_exe_base+0x2e55ee0))
#define g_ram (*(uint8_t (*)[0x10000])(oa_exe_base+0x2a45ea0))
#define g_rom (*(uint8_t (*)[0x400000])(oa_exe_base+0x2a55ee0))
#define g_frame_count (*(uint64_t *)(oa_exe_base+0x1dc1268))
#define g_native_insn_count (*(uint64_t *)(oa_exe_base+0x1dc1270))
#define g_cycle_accumulator (*(uint32_t *)(oa_exe_base+0x1dc1278))
#define g_audio_cycle_counter (*(uint32_t *)(oa_exe_base+0x1dc127c))
#define g_vblank_threshold (*(uint32_t *)(oa_exe_base+0x1bb2040))
#define g_rte_pending_ptr (*(int * *)(oa_exe_base+0x1bb2038))
#define g_split_sp_popped (*(int *)(oa_exe_base+0x26048b8))
#define m68k_read8 ((uint8_t(*)(uint32_t))(oa_exe_base+0xd620))
#define m68k_read16 ((uint16_t(*)(uint32_t))(oa_exe_base+0xd410))
#define m68k_read32 ((uint32_t(*)(uint32_t))(oa_exe_base+0xd500))
#define m68k_write8 ((void(*)(uint32_t,uint8_t))(oa_exe_base+0xd9a0))
#define m68k_write16 ((void(*)(uint32_t,uint16_t))(oa_exe_base+0xd7e0))
#define m68k_write32 ((void(*)(uint32_t,uint32_t))(oa_exe_base+0xd890))
#define glue_poke8 ((void(*)(uint32_t,uint8_t))(oa_exe_base+0xbe30))
#define glue_poke16 ((void(*)(uint32_t,uint16_t))(oa_exe_base+0xbd70))
#define glue_poke32 ((void(*)(uint32_t,uint32_t))(oa_exe_base+0xbdf0))
#define glue_check_vblank ((void(*)(void))(oa_exe_base+0xb630))
#define hybrid_jmp_interpret ((void(*)(uint32_t))(oa_exe_base+0xcd50))
#define recomp_push_return ((void(*)(uint32_t))(oa_exe_base+0xe220))
#define recomp_call_addr ((void(*)(uint32_t))(oa_exe_base+0x1853fe0))
#define recomp_call_func ((void(*)(RecompFuncPtr))(oa_exe_base+0x1854170))
#define func_00EAE6 ((void(*)(void))(oa_exe_base+0x186400))
#define func_01A596 ((void(*)(void))(oa_exe_base+0x2f5f80))
#define recomp_tail_call ((void(*)(uint32_t))(oa_exe_base+0x18542e0))
#define genesis_game_instruction_hook oa_instruction
#define func_001358 ((void(*)(void))(oa_exe_base+0x39ce0))
#define func_001380 ((void(*)(void))(oa_exe_base+0x3a020))
void func_001488(void);
#define func_0015BA ((void(*)(void))(oa_exe_base+0x3e240))
#define func_001D24 ((void(*)(void))(oa_exe_base+0x551a0))
#define func_001FE4 ((void(*)(void))(oa_exe_base+0x560d0))
#define func_003DDA ((void(*)(void))(oa_exe_base+0x74510))
void func_00622A(void);
void func_006268(void);
void func_006886(void);
void func_00690A(void);
void func_00693E(void);
void func_0069A6(void);
void func_006DDE(void);
void func_006EA4(void);
void func_006EE8(void);
#define func_0076A6 ((void(*)(void))(oa_exe_base+0xcaae0))
void func_007892(void);
void func_0078B2(void);
void func_007D6C(void);
void func_007D9E(void);
#define func_00C434 ((void(*)(void))(oa_exe_base+0x150430))
void func_00C464(void);
void func_0100BC(void);
void func_010C98(void);
void func_0138FE(void);
void func_01664A(void);
void func_01A31E(void);
#define func_01AB52 ((void(*)(void))(oa_exe_base+0x302d30))
#define func_01BAF2 ((void(*)(void))(oa_exe_base+0x332ee0))
void func_01BE5E(void);
void func_01BEC6(void);
void func_01BF1E(void);
void func_01BF68(void);
#define func_01C362 ((void(*)(void))(oa_exe_base+0x344a50))
void func_01C4A6(void);
void func_01C594(void);
void func_01C64E(void);
#define func_01DC56 ((void(*)(void))(oa_exe_base+0x370b10))
void func_02694E(void);
void func_027096(void);
void func_02C626(void);
void func_02C690(void);
void func_02DCD4(void);
void func_02DCE2(void);
void func_0320F2(void);
void func_042D76(void);
void func_045596(void);
void func_045B94(void);
#define func_04F8F8 ((void(*)(void))(oa_exe_base+0x8eb2c0))
void func_04FA42(void);
void func_04FE68(void);
#define func_04FF00 ((void(*)(void))(oa_exe_base+0x8f8880))
void func_051E60(void);
void func_05215A(void);
void func_053608(void);
void func_053744(void);
void func_0537A8(void);
void func_053886(void);
#define func_0539AC ((void(*)(void))(oa_exe_base+0x976650))
void func_053EDC(void);
void func_053F22(void);
void func_05410C(void);
void func_0541A2(void);
void func_054384(void);
#define func_0543A2 ((void(*)(void))(oa_exe_base+0x98a800))
void func_054AB6(void);
void func_054AE0(void);
#define func_054B80 ((void(*)(void))(oa_exe_base+0x9a21f0))
void func_054F8C(void);
void func_055312(void);
void func_05560C(void);
void func_05564A(void);
void func_0556C4(void);
void func_056B6C(void);
void func_05711E(void);
void func_05A12E(void);
void func_05A1A4(void);
void func_061F10(void);
void func_062112(void);
void func_06229E(void);
void func_0624DE(void);
void func_062640(void);
void func_0628AA(void);
void func_062CA4(void);
void func_062E28(void);
void func_0630E0(void);
void func_0632CA(void);
void func_063446(void);
void func_063A6A(void);
void func_063B22(void);
void func_063D1A(void);
void func_0642C6(void);
void func_065BD4(void);
#define func_0661E0 ((void(*)(void))(oa_exe_base+0xb57980))
void func_067712(void);
void func_067AEA(void);
#define func_067B14 ((void(*)(void))(oa_exe_base+0xb66cc0))
void func_067BBC(void);
void func_067C58(void);
void func_068A6E(void);
void func_068ADE(void);
void func_0691A8(void);
void func_0693A2(void);
void func_069546(void);
void func_0697D8(void);
#define func_069B2E ((void(*)(void))(oa_exe_base+0xb91a00))
void func_06AEB6(void);
void func_06AFC8(void);
void func_06B0E8(void);
void func_06C698(void);
void func_06DE40(void);
void func_06F9A2(void);
void func_06FD38(void);
void func_07002A(void);
void func_07009A(void);
void func_07071A(void);
void func_07078A(void);
void func_0707BC(void);
#define func_070DAC ((void(*)(void))(oa_exe_base+0xc3bac0))
void func_071400(void);
void func_071CC6(void);
void func_0724E2(void);
void func_072562(void);
void func_07289A(void);
void func_0729DE(void);
void func_0735B6(void);
void func_075480(void);
void func_075AD4(void);
void func_076136(void);
void func_076318(void);
void func_076822(void);
void func_07772C(void);
void func_079F66(void);
void func_07FB9E(void);
void func_08040C(void);
#define func_082C6A ((void(*)(void))(oa_exe_base+0xe05c10))
void func_082DE8(void);
#define func_084038 ((void(*)(void))(oa_exe_base+0xe1ef50))
#define func_084060 ((void(*)(void))(oa_exe_base+0xe1f480))
#define func_084114 ((void(*)(void))(oa_exe_base+0xe214d0))
#define func_084220 ((void(*)(void))(oa_exe_base+0xe24330))
#define func_084388 ((void(*)(void))(oa_exe_base+0xe27f10))
#define func_084400 ((void(*)(void))(oa_exe_base+0xe29270))
#define func_08458E ((void(*)(void))(oa_exe_base+0xe2cb70))
#define func_084B18 ((void(*)(void))(oa_exe_base+0xe39db0))
#define func_085022 ((void(*)(void))(oa_exe_base+0xe483d0))
#define func_0858C8 ((void(*)(void))(oa_exe_base+0xe5c660))
#define func_085C36 ((void(*)(void))(oa_exe_base+0xe64670))
#define func_085D6A ((void(*)(void))(oa_exe_base+0xe67370))
#define func_085F8A ((void(*)(void))(oa_exe_base+0xe6bdf0))
void func_0863EC(void);
void func_08867E(void);
void func_088716(void);
void func_08A280(void);
void func_08A50C(void);
void func_08CB56(void);
void func_08CDB8(void);
void func_08CE84(void);
void func_08CF36(void);
#define func_08D116 ((void(*)(void))(oa_exe_base+0xf33080))
#define func_08D1FC ((void(*)(void))(oa_exe_base+0xf33e10))
void func_08F0CA(void);
void func_08F3F6(void);
void func_01019A(void);
void func_01AAFC(void);
void func_01AB04(void);
void func_01ABB8(void);
void func_01D716(void);
void func_01BB14(void);
void func_00EAC6(void);
void func_00EAF0(void);
void func_01A7C2(void);
void func_01A572(void);
void func_02DAD0(void);
void func_02DBDE(void);
#define func_01BAE2 ((void(*)(void))(oa_exe_base+0x332be0))
void func_04FC0E(void);
void func_00EB86(void);
void func_00EBCE(void);
#ifdef __cplusplus
}
#endif

#define func_00DCCA ((void(*)(void))(oa_exe_base+0x1740a0))

void func_006040(void);
void func_01BF74(void);

void func_088572(void);
void func_06EE72(void);
void func_0738E0(void);

#define func_06F786 ((void (*)(void))(oa_exe_base+0xc1cc50))



#define func_073FE2 ((void (*)(void))(oa_exe_base+0xc83550))

#define func_088A62 ((void (*)(void))(oa_exe_base+0xeb9f50))

void func_062124(void);
void func_0622B2(void);
void func_0624F2(void);
void func_0630FC(void);
void func_06FD16(void);

#define func_070330 ((void (*)(void))(oa_exe_base+0xc2d200))

void func_0642BC(void);

void func_06F994(void);

void func_079F58(void);

void func_07FB92(void);

void func_082DCE(void);

void func_063D2E(void);

void func_069EAA(void);

void func_06AECC(void);

void func_06C6A2(void);

void func_06E488(void);

void func_07117E(void);

void func_071BD8(void);

void func_073004(void);

void func_075F5A(void);

void func_0784FA(void);

void func_07DDBE(void);

void func_07F076(void);

void func_08CB66(void);

void func_08CF8A(void);

void func_090104(void);

#define func_085D7E ((void (*)(void))(oa_exe_base+0xe675c0))

#define func_085940 ((void (*)(void))(oa_exe_base+0xe5e340))

#define func_08D0EA ((void (*)(void))(oa_exe_base+0xf32c10))

void func_0859DA(void);

void func_0859DE(void);

void func_085E3A(void);

void func_0016FA(void);

void func_00172C(void);

void func_001AD2(void);

void func_082B62(void);
