// Preserve the original VDP configuration while adding visible-sprite provenance.
extern "C" {
#include "sonic3_video.h"
#include "sonic3_state.h"
const uint32_t *gvdp_visible_sprite_attributes(void);
void cmd_send_response(const char *s) {response(s);}
}
static uint8_t render_indices[GVDP_MAX_WIDTH];
static int render_width;
static void (*old_run_frame)(GenesisScanlineSink,void*);
static int (*old_render)(GVDP*,int,uint8_t*);
#define MIRROR_INT(name) \
static void (*old_##name)(int); \
static void mirror_##name(int v) {old_##name(v);name(v);}
MIRROR_INT(gvdp_set_unlimited_sprites)
MIRROR_INT(gvdp_set_ws_extra)
MIRROR_INT(gvdp_set_ws_canvas)
MIRROR_INT(gvdp_set_ws_bar_black)
MIRROR_INT(gvdp_set_bgdiag)
static int (*old_palette)(const uint32_t*,unsigned);
static int mirror_palette(const uint32_t *p,unsigned n) {int r=old_palette(p,n);gvdp_set_host_palette(p,n);return r;}
static void (*old_sprites)(GVDPHostSprites,void*);
static void mirror_sprites(GVDPHostSprites f,void *p) {old_sprites(f,p);gvdp_set_host_sprites(f,p);}
static int render(GVDP *v,int line,uint8_t *out) {
    if(!mod)return old_render(v,line,out);
    int n=gvdp_render_scanline(v,line,out);render_width=n;
    if(n>0&&n<=GVDP_MAX_WIDTH)memcpy(render_indices,out,n);
    return n;
}
struct SinkContext {GenesisScanlineSink sink;void *user;};
static void tinted_sink(void *ctx,int line,const uint32_t *argb,int width) {
    auto *s=static_cast<SinkContext*>(ctx);uint32_t row[GVDP_MAX_WIDTH];
    if(mod&&width>0&&width==render_width&&width<=GVDP_MAX_WIDTH) {
        memcpy(row,argb,width*sizeof(uint32_t));const uint32_t *a=gvdp_visible_sprite_attributes();
        for(int x=0;x<width;x++)if(a[x]&0x10000u)row[x]=mod->tint(uint16_t(a[x]),render_indices[x],row[x]);
        s->sink(s->user,line,row,width);
    } else s->sink(s->user,line,argb,width);
}
static void run_frame(GenesisScanlineSink sink,void *ctx) {
    SinkContext s{sink,ctx};old_run_frame(sink?tinted_sink:nullptr,&s);
}
static bool renderer_hooks() {
#define RH(name,replacement,old) if(!hook(oa_exe_base+OA_RVA_##name,(void*)replacement,(void**)old))return false
    RH(gvdp_render_scanline,render,&old_render);
    RH(machine_run_frame,run_frame,&old_run_frame);
    RH(gvdp_set_unlimited_sprites,mirror_gvdp_set_unlimited_sprites,&old_gvdp_set_unlimited_sprites);
    RH(gvdp_set_ws_extra,mirror_gvdp_set_ws_extra,&old_gvdp_set_ws_extra);
    RH(gvdp_set_ws_canvas,mirror_gvdp_set_ws_canvas,&old_gvdp_set_ws_canvas);
    RH(gvdp_set_ws_bar_black,mirror_gvdp_set_ws_bar_black,&old_gvdp_set_ws_bar_black);
    RH(gvdp_set_bgdiag,mirror_gvdp_set_bgdiag,&old_gvdp_set_bgdiag);
    RH(gvdp_set_host_palette,mirror_palette,&old_palette);
    RH(gvdp_set_host_sprites,mirror_sprites,&old_sprites);
    RH(s3_video_hook,s3_video_hook,nullptr);
    RH(s3_video_vblank,s3_video_vblank,nullptr);
    RH(s3_video_main_cpu_divisor,s3_video_main_cpu_divisor,nullptr);
    RH(s3_video_canvas_width,s3_video_canvas_width,nullptr);
    RH(s3_video_set_actor_overlay,s3_video_set_actor_overlay,nullptr);
    RH(s3_video_state,s3_video_state,nullptr);
#undef RH
    return true;
}
