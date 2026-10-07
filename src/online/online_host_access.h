#pragma once
#include "online_mod_api.h"
extern const OnlineModHost *online_host;
extern int online_margin;
extern int online_view_left,online_view_top;
extern int (*online_hooks[32])(void);
extern uint32_t (*online_tint)(uint16_t,uint8_t,uint32_t);
#define g_cpu (*online_host->cpu)
#define g_ram (*reinterpret_cast<uint8_t (*)[0x10000]>(online_host->ram))
#define g_rom (*reinterpret_cast<const uint8_t (*)[0x400000]>(online_host->rom))
#define g_ws_margin online_margin
#define g_recomp_hooks online_hooks
#define m68k_write8 online_host->write8
#define m68k_write16 online_host->write16
#define m68k_write32 online_host->write32
#define recomp_call_addr online_host->call
#define cmd_send_response online_host->response
#define cmd_send_err online_host->error
static const uint8_t *sonikk_vram() { return online_host->vram; }
static uint16_t sonikk_cram_raw(int i) { return i>=0 && i<64?online_host->cram[i]:0; }
static uint32_t sonikk_cram_argb(int i) { return online_host->cram_argb(i); }
static void machine_set_sprite_tint(uint32_t (*fn)(uint16_t,uint8_t,uint32_t)) { online_tint=fn; }
