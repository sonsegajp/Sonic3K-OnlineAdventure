/* Versioned, C-only boundary. No STL, ImGui or allocator objects cross it.
 * Bound to the v0.5.2 host bridge and the original combined World cartridge. */
#pragma once
#include <stdint.h>
#include "genesis_runtime.h"
#define ONLINE_MOD_ABI 1u
#define ONLINE_MOD_HOST "sonic3k-v0.5.2-online-1"
struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;
typedef struct OnlineModHost {
    unsigned abi, size, cpu_size, rom_size;
    const char *host_id;
    M68KState *cpu;
    uint8_t *ram;
    const uint8_t *rom;
    uint8_t *vram;
    const uint16_t *cram;
    void (*write8)(uint32_t,uint8_t);
    void (*write16)(uint32_t,uint16_t);
    void (*write32)(uint32_t,uint32_t);
    void (*call)(uint32_t);
    void (*tail_call)(uint32_t);
    void (*response)(const char *);
    void (*error)(int,const char *);
    uint32_t (*cram_argb)(int);
    void (*world_view)(int *left,int *top);
} OnlineModHost;
typedef struct OnlineMod {
    unsigned abi, size;
    void (*init)(void);
    void (*poll)(void);
    void (*pre)(uint64_t);
    void (*post)(uint64_t);
    uint16_t (*pad)(int,uint16_t);
    int (*active)(void);
    int (*event)(const union SDL_Event *);
    void (*draw)(struct SDL_Window *,struct SDL_Renderer *,int,int);
    void (*shutdown)(void);
    void (*debug)(int,const char *);
    int (*instruction)(uint32_t);
    uint32_t (*tint)(uint16_t,uint8_t,uint32_t);
} OnlineMod;
typedef const OnlineMod *(*OnlineModEntry)(const OnlineModHost *);
