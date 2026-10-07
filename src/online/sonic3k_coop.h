#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct SDL_Renderer;
union SDL_Event;
int s3k_coop_event(const union SDL_Event *event);
void s3k_coop_init(void);
int s3k_coop_world_instruction(uint32_t pc);
void s3k_coop_poll(void);
void s3k_coop_frame(uint64_t frame);
void s3k_coop_pre(uint64_t frame);
uint16_t s3k_coop_pad(int port, uint16_t buttons);
int s3k_coop_active(void);
int s3k_coop_ui_visible(void);
void s3k_coop_draw_player(struct SDL_Renderer *renderer);
void s3k_coop_draw_ui(void);
void s3k_coop_after_ui(struct SDL_Renderer *renderer);
void s3k_coop_shutdown(void);
void s3k_coop_debug(int id, const char *json);
#ifdef __cplusplus
}
#endif
