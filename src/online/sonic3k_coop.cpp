/* Up to eight players, independent-camera adventure sessions. Original game code owns
 * physics, enemies and cutscenes; the session replicates player presentation
 * and commits progression through reliable, numbered barriers. No ROM edits.
 * Hook/RAM references: sonicretro/skdisasm sonic3k.asm and constants.asm. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET CoopSocket;
#define BAD_SOCKET INVALID_SOCKET
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
typedef int CoopSocket;
#define BAD_SOCKET (-1)
#endif
#include <SDL.h>
#include "imgui.h"
#include "sonic3k_coop.h"
#include "adventure_progress.h"
#include "remote_motion.h"
#include "shared_world.h"
#include <set>
#include "foreground_tiles.h"
#include "shared_terrain.h"
#include "world_codec.h"

#include "png_write.h"
#include "s3r_codec.h"
#include "online_host_access.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <random>
#include <string>


namespace {
constexpr unsigned VERSION=15, MAX_PLAYERS=8, MAX_CHANNELS=16, MAX_PACKET=10000, CAPACITY=65536, SIDE=96;
constexpr unsigned SPRITE_BYTES=SIDE*SIDE, STATE_HEADER=32, SPRITE_OFFSET=STATE_HEADER+256, STATE_BYTES=SPRITE_OFFSET+SPRITE_BYTES;
enum Message { HELLO=1, CHOICE, READY, START, ARRIVE, RELEASE, STATE, PING, RINGS, ROSTER, EMERALD, NAME, POSE, MAP_PING, OPTIONS, CARRY, RESCUE, WORLD, WATCH, VIDEO, RESUME, TEAM_REPLY, TEAM_EMERALDS, PROGRESS_CONFIG };
enum Phase { OFF, LISTENING, CONNECTING, LOBBY, INTRO, PLAY, FAILED, RECONNECTING };
struct Channel {
    CoopSocket fd=BAD_SOCKET;
    bool hello=false,resume_waiting=false;
    int peer=-1;
    unsigned tx_size=0,rx_size=0,last_send=0,last_recv=0;
    std::array<unsigned char,CAPACITY> tx{},rx{};
};
// Channel 0 connects a client to its host; a host uses channels 1..7.
// Separate storage keeps reset temporaries well below the host stack limit.
std::array<Channel,MAX_CHANNELS> channels;
struct SafeAnchor { bool valid=false;unsigned stage=0,x=0,y=0,actual=0; };
struct PlayerPose {
    unsigned epoch=0,stage=0,x=0,y=0,buttons=0,routine=0,status=0,anim=0,flight=0,control=0,flags=0,actual=0,wrap_y=0;
    int vx=0,vy=0;
    SafeAnchor safe{},checkpoint{};
};
struct Player {
    bool present=false,ready=false,state_valid=false,connected=false,pose_valid=false;
    uint64_t token=0;
    char name[25]{};
    PlayerPose pose{};
    unsigned pose_time=0,chaos_mask=0,super_mask=0,chaos_count=0,super_count=0;
    std::array<unsigned char,7> emerald_state{};
    int character=0;
    unsigned arrival_epoch=~0u,stage=0,emeralds_announced=0;
    std::array<unsigned char,STATE_BYTES> state{};
};
using PlayerMotion=adventure::RemoteMotion<std::array<unsigned char,STATE_BYTES>>;
std::array<PlayerMotion,MAX_PLAYERS> remote_motion;
PlayerMotion::View player_view(unsigned who,double now);
unsigned carry_sprite_flip(unsigned who,const unsigned char *state,double now);
struct CarryGrip {int x,y;};
unsigned motion_warp=0;
struct EmeraldNotice { unsigned player=0,started=0; bool active=false; };
struct Session {
    CoopSocket listener=BAD_SOCKET;
    Phase phase=OFF,resume_phase=PLAY;
    uint64_t room=0,token=0;
    bool reconnecting=false,resume_menu=false,connecting_socket=false;
    unsigned retry_at=0,saved_pause=0,last_release=~0u;
    SafeAnchor restore_anchor{};
    bool restore_pending=false,restore_loading=false;
    unsigned restore_epoch=0,restore_actual=0;
    bool host=false,hello=false,shared_intro=false,released=false,waiting=false;
    bool hit=false,intro_finished=false,handoff=false,start_pending=false;
    int character=0,local_id=-1,start_frames=0;
    unsigned epoch=0,barrier_stage=0,sent=0,received=0,connect_at=0;
    unsigned knux_x=0,knux_y=0,frames=0,route_hits=0;
    unsigned chaos_observed=0,notice_next=0,notice_count=0;
    int last_emerald_player=-1;
    std::array<EmeraldNotice,MAX_PLAYERS> notices;
    bool knux_captured=false,hud_pending=false;
    std::array<Player,MAX_PLAYERS> players;
    // IDs are per-zone ring bits. Preserve them across deaths and private
    // special-stage visits, and across act 1 -> act 2 within the same zone.
    std::array<uint32_t,14> used_rings{},entering_rings{};
    int observed_ring_zone=-1;
    uint32_t observed_rings=0;
    char error[160]{};
} s;
CarryGrip carry_grip(unsigned passenger,unsigned flags) {
    bool knuckles=passenger<MAX_PLAYERS&&s.players[passenger].character==2;
    return {knuckles?((flags&1)?2:-2):0,(flags&2)?-(knuckles?29:28):(knuckles?29:28)};
}
adventure::Progress progress;
unsigned progress_generation=0,progress_arrival=~0u;
std::array<uint32_t,14> progress_wanted_rings{},progress_sent_rings{};
std::array<uint8_t,7> progress_wanted_gems{},progress_sent_gems{};
bool progress_wanted_conversion=false,progress_sent_conversion=false;
void progress_clear();bool progress_restart(bool);void progress_poll();
bool progress_handle(const unsigned char*,unsigned);
void progress_observe_emeralds(const unsigned char*,bool);
// Survives session cleanup until the native pause loop has actually exited.
bool native_unpause=false;
bool socket_ready=false;
unsigned rom_crc=0,remote_drawn=0;
std::array<SDL_Texture*,MAX_PLAYERS> remote_textures{};
char peer_address[64]="127.0.0.1",capture_path[512]{};
int port_number=7777;
bool lobby_open=false;
const char *characters[]={"Sonic","Tails","Knuckles"};
uint8_t r8(unsigned a) { return g_ram[a&65535]; }
uint16_t r16(unsigned a) { return (r8(a)<<8)|r8(a+1); }
uint32_t r32(unsigned a) { return (uint32_t(r16(a))<<16)|r16(a+2); }
void w8(unsigned a,unsigned v) { m68k_write8(0xff0000|(a&65535),uint8_t(v)); }
void w16(unsigned a,unsigned v) { m68k_write16(0xff0000|(a&65535),uint16_t(v)); }
void w32(unsigned a,unsigned v) { m68k_write32(0xff0000|(a&65535),v); }
unsigned get16(const unsigned char *p) { return (unsigned(p[0])<<8)|p[1]; }
// The visual header carries the internal layout and vertical loop size.
// Bits 0..1 retain native render flips; supported zone IDs fit five bits.
unsigned state_layout(const unsigned char *p) { return (unsigned((p[20]>>2)&31)<<8)|(p[23]&127); }
unsigned state_y_period(const unsigned char *p) {return (p[20]&128)?((p[23]&128)?4096:2048):0;}
unsigned local_y_period(){unsigned mask=r16(0xeeaa);return int16_t(r16(0xee18))<0&&(mask==0x7ff||mask==0xfff)?mask+1:0;}
unsigned get32(const unsigned char *p) { return (get16(p)<<16)|get16(p+2); }
void put16(unsigned char *p,unsigned v) { p[0]=uint8_t(v>>8);p[1]=uint8_t(v); }
void put32(unsigned char *p,unsigned v) { put16(p,v>>16);put16(p+2,v); }
uint64_t get64(const unsigned char *p){return (uint64_t(get32(p))<<32)|get32(p+4);}
void put64(unsigned char *p,uint64_t value){put32(p,unsigned(value>>32));put32(p+4,unsigned(value));}
uint64_t new_token(){std::random_device r;uint64_t value=(uint64_t(r())<<32)|r();return value?value:1;}
struct ResumeTicket { uint64_t room=0,token=0;int character=0,port=7777;char address[64]{}; } ticket;
char local_name[25]="Player";
bool show_names=true;
void clean_name(char *out,const unsigned char *in,unsigned count) {
    unsigned n=0;for(unsigned i=0;i<count&&in[i]&&n<24;i++) {
        unsigned c=in[i];if(c>=32&&c<127&&c!='"'&&c!='\\')out[n++]=char(c);
    }
    while(n&&out[n-1]==' ')n--;out[n]=0;if(!n)snprintf(out,25,"Player");
}
const char *player_name(unsigned who){return who<MAX_PLAYERS&&s.players[who].name[0]?s.players[who].name:"Player";}
void save_identity(){FILE *f=fopen("sonic3k_player.ini","w");if(f){fprintf(f,"%s\n%d\n",local_name,show_names?1:0);fclose(f);}}
void save_ticket() {
    if(s.host||s.local_id<1||!s.room||!s.token)return;
    ticket.room=s.room;ticket.token=s.token;ticket.character=s.character;ticket.port=port_number;
    snprintf(ticket.address,sizeof(ticket.address),"%s",peer_address);
    FILE *f=fopen("sonic3k_resume.ini","w");if(f){fprintf(f,"%llu %llu %d %d %s\n",(unsigned long long)ticket.room,(unsigned long long)ticket.token,ticket.character,ticket.port,ticket.address);fclose(f);}
}
void load_identity() {
    FILE *f=fopen("sonic3k_player.ini","r");if(f){char name[64]{};if(fgets(name,sizeof(name),f))clean_name(local_name,(unsigned char*)name,24);int names=1;fscanf(f,"%d",&names);show_names=names!=0;fclose(f);}
    f=fopen("sonic3k_resume.ini","r");if(f){unsigned long long room=0,token=0;int character=0,port=0;char address[64]{};
        if(fscanf(f,"%llu %llu %d %d %63s",&room,&token,&character,&port,address)==5&&room&&token&&character>=0&&character<=2&&port>0&&port<65536){ticket.room=room;ticket.token=token;ticket.character=character;ticket.port=port;snprintf(ticket.address,sizeof(ticket.address),"%s",address);}fclose(f);}
}
bool in_game();bool side_trip();bool title_card_active();
void close_socket(CoopSocket &fd);void roster();void send_resume(unsigned channel);void reconnect_now();
void team_reset_peer(unsigned);void team_reset();void team_disconnect(unsigned who);void team_frame(uint64_t frame);void team_pre_frame();
bool team_handle(unsigned channel,unsigned kind,const unsigned char *data,unsigned length);
void draw_team_ui();void draw_team_map_tools();void draw_team_pings(ImDrawList*,ImVec2,float,unsigned,unsigned,unsigned);
void team_map_click(unsigned x,unsigned y);bool team_event(const SDL_Event*);uint16_t team_pad(uint16_t);
void team_video_textures(SDL_Renderer*);void team_capture(SDL_Renderer*);void team_toggle();void team_nameplate(SDL_Renderer*,unsigned,int,int);
bool team_debug(int,const char*,const char*);
struct PaletteStyle {
    unsigned mask=0;
    float body[3]={.15f,.25f,1.0f},shoes[3]={1,0,0},gloves[3]={1,1,1};
};
std::array<PaletteStyle,3> palette_styles;
bool palette_editor=false,palette_dirty=false;
unsigned palette_tint_hits=0,special_palette_hits=0;
unsigned palette_edit_time=0;
uint32_t rgb_value(const float *v) {
    return (unsigned(v[0]*255+.5f)<<16)|(unsigned(v[1]*255+.5f)<<8)|unsigned(v[2]*255+.5f);
}
void rgb_set(float *v,unsigned rgb) { for(unsigned i=0;i<3;i++)v[i]=float((rgb>>(16-i*8))&255)/255; }
void palette_defaults() {
    const unsigned colors[]={0x2048ee,0xee9900,0xcc202c};
    for(unsigned i=0;i<3;i++){palette_styles[i]=PaletteStyle{};rgb_set(palette_styles[i].body,colors[i]);}
}
void save_palettes() {
    if(!palette_dirty)return;
    FILE *f=fopen("sonic3k_palettes.ini","w");if(!f)return;
    for(auto &p:palette_styles)fprintf(f,"%u %06x %06x %06x\n",p.mask,rgb_value(p.body),rgb_value(p.shoes),rgb_value(p.gloves));
    fclose(f);palette_dirty=false;
}
void load_palettes() {
    palette_defaults();FILE *f=fopen("sonic3k_palettes.ini","r");if(!f)return;
    for(auto &p:palette_styles) {
        unsigned mask,body,shoes,gloves;
        if(fscanf(f,"%u %x %x %x",&mask,&body,&shoes,&gloves)!=4||mask>7||body>0xffffff||shoes>0xffffff||gloves>0xffffff)break;
        p.mask=mask;rgb_set(p.body,body);rgb_set(p.shoes,shoes);rgb_set(p.gloves,gloves);
    }
    fclose(f);
}
void palette_changed() { palette_dirty=true;palette_edit_time=SDL_GetTicks(); }
unsigned character_palette_source(unsigned character,bool special,unsigned i) {
    // Stable native colors identify a shade even during underwater palettes,
    // palette animation and fades. Blue Spheres has a separate Knuckles patch.
    if(special) {
        if(character==2&&i>=8)return get16(g_rom+0x89ee+(i-8)*2);
        return get16(g_rom+0x896e+(character==1?32:0)+i*2);
    }
    if(character==2)return get16(g_rom+0x0a8afc+i*2);
    static const uint16_t sonic_tails[]={0x000,0xeee,0xe66,0xc42,0x822,0x080,0x00e,0x008,
                                        0x0ae,0x08e,0x8ae,0x46a,0xecc,0xcaa,0x866,0x222};
    return sonic_tails[i];
}
void palette_source_rgb(unsigned color,float &r,float &g,float &b) {
    r=float(color&14)/14;g=float((color>>4)&14)/14;b=float((color>>8)&14)/14;
}
uint32_t palette_color(unsigned index,uint32_t original,bool special) {
    const auto &p=palette_styles[s.character];unsigned i=index&15;
    const float *rgb=nullptr;float shade=1,white=0;
    float shifted[3]{};
    // Neutral grays are shared by gloves, white fur and tail tips. Body tint
    // must never touch them; only the explicitly enabled glove control may.
    bool glove_shade=s.character==1&&(special?((i>=2&&i<=5)||i==15):(i>=12&&i<=14));
    if(p.mask&1) {
        float sr,sg,sb,h,saturation,v;
        palette_source_rgb(character_palette_source(s.character,special,i),sr,sg,sb);
        ImGui::ColorConvertRGBtoHSV(sr,sg,sb,h,saturation,v);
        const float family[]={2.0f/3,.11f,.98f};
        float distance=h-family[s.character];if(distance<0)distance=-distance;
        distance=(std::min)(distance,1-distance);
        // Equipment shares some body hues in the native palette. Retain its
        // separate controls; include Tails' extra tan/brown fur and tail shades.
        bool equipment=special?(i>=2&&i<=7)||i==12:((i==6||i==7)||(i>=12&&i<=14));
        // The red family wraps through orange. Knuckles' tan mouth must not
        // be included simply because it is near his red fur on the hue wheel.
        bool face=s.character!=1&&(special?(i==13||i==14||i==15):(i>=8&&i<=11));
        if(!equipment&&!face&&distance<=.09f&&saturation>.18f&&v>.05f) {
            float target_h,target_s,target_v,br,bg,bb,bh,bs,bv;
            ImGui::ColorConvertRGBtoHSV(p.body[0],p.body[1],p.body[2],target_h,target_s,target_v);
            if(s.character==1)target_s*=.94f; // Slightly softer fur; retain the selected hue.
            unsigned base=special?(s.character==1?8:9):(s.character==1?8:3);
            palette_source_rgb(character_palette_source(s.character,special,base),br,bg,bb);
            ImGui::ColorConvertRGBtoHSV(br,bg,bb,bh,bs,bv);
            float light=(.2126f*sr+.7152f*sg+.0722f*sb)/(.2126f*br+.7152f*bg+.0722f*bb);
            // Sonic's Blue Spheres ramp adds red/green to its blue highlights.
            // Luminance ratios exaggerate those additions and crush pure-blue
            // shadows. Preserve the native HSV value ramp for this sprite.
            if(special&&s.character==0)light=v/bv;
            float colorfulness=(std::min)(1.0f,target_s*saturation/bs);
            ImGui::ColorConvertHSVtoRGB(target_h,colorfulness,(std::min)(1.0f,target_v*light),shifted[0],shifted[1],shifted[2]);
            rgb=shifted;
        }
    }
    if(p.mask&2) {
        if(!special&&(i==6||i==7)){rgb=p.shoes;shade=i==7?.55f:1;white=0;}
        else if(special&&(i==6||i==7||i==12||(s.character!=1&&i>=2&&i<=5))) {
            rgb=p.shoes;white=(i==2||i==6)?.30f:0;shade=i==4?.55f:i==5?.28f:1;
        }
    }
    if((p.mask&4)&&(i==1||glove_shade)) {
        rgb=p.gloves;white=0;shade=1;
        if(glove_shade) {
            float r,g,b;palette_source_rgb(character_palette_source(s.character,special,i),r,g,b);
            shade=(std::max)(r,(std::max)(g,b));
        }
    }
    if(!rgb)return original;
    unsigned bank=(r16(0xb00a)>>13)&3;
    unsigned base_index=special?9:(s.character==1?8:3),dark_index=special?11:(s.character==1?9:4);
    unsigned native_base=sonikk_cram_raw(int(bank*16+base_index))&0x0eee;
    unsigned native_dark=sonikk_cram_raw(int(bank*16+dark_index))&0x0eee;
    if((native_base==0&&native_dark==0)||(native_base==0x0eee&&native_dark==0x0eee))return original;
    // Preserve native black/white fades and the scanline's shadow brightness.
    unsigned r=(original>>16)&255,g=(original>>8)&255,b=original&255;
    if(r==g&&g==b&&(r<4||r>250)&&i!=1)return original;
    float brightness=index>=128?1.20f:index>=64?.50f:1.0f;
    unsigned result=original&0xff000000u;
    for(unsigned c=0;c<3;c++) {
        float value=(rgb[c]*shade*(1-white)+white)*brightness;
        result|=unsigned((std::min)(1.0f,(std::max)(0.0f,value))*255+.5f)<<(16-c*8);
    }
    return result;
}
bool own_character_sprite(unsigned attr) {
    unsigned tile=attr&0x7ff,mode=r8(0xf600);
    if(mode==0x34) {
        if(s.character==1)return tile>=0x7eb||(tile>=0x7b0&&tile<0x7d4);
        return tile>=0x7d4&&tile<0x7eb;
    }
    if(mode!=0x0c||!r32(0xb000)||r8(0xb038)!=s.character)return false;
    unsigned base=r16(0xb00a)&0x7ff;
    if(tile>=base&&tile<base+32)return true;
    if(s.character==1&&r32(0xcc0a)&&r16(0xcc3a)==0xb000) {
        unsigned tail=r16(0xcc14)&0x7ff;
        if(tile>=tail&&tile<tail+16)return true;
    }
    return false;
}
uint32_t tint_character(uint16_t attr,uint8_t index,uint32_t original) {
    if((s.phase!=PLAY&&s.phase!=FAILED)||!palette_styles[s.character].mask||!own_character_sprite(attr))return original;
    uint32_t result=palette_color(index,original,r8(0xf600)==0x34);
    if(result!=original){palette_tint_hits++;if(r8(0xf600)==0x34)special_palette_hits++;}
    return result;
}
void draw_palette_editor() {
    if(palette_dirty&&int32_t(SDL_GetTicks()-palette_edit_time)>500)save_palettes();
    if(!palette_editor)return;
    ImGui::SetNextWindowSize(ImVec2(335,0),ImGuiCond_Always);
    if(ImGui::Begin("Character palette",&palette_editor,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("%s",characters[s.character]);
        ImGui::TextWrapped("Changes are visible to everyone as you edit, including in special stages.");
        auto &p=palette_styles[s.character];
        const char *names[]={"Original","Blue","Red","Gold","Green","Purple","Pink","Cyan","Silver","Custom"};
        const unsigned colors[]={0,0x285aef,0xe73242,0xf0b52d,0x24ce70,0x9c55e8,0xef70b5,0x30ccdf,0xaab9cd};
        int preset=p.mask?9:0;
        if(p.mask==1)for(int i=1;i<9;i++)if(rgb_value(p.body)==colors[i])preset=i;
        if(ImGui::Combo("Preset",&preset,names,10)) {
            if(!preset)p.mask=0;else if(preset<9){p.mask|=1;rgb_set(p.body,colors[preset]);}
            palette_changed();
        }
        const char *labels[]={"Body","Shoes","Gloves"};float *values[]={p.body,p.shoes,p.gloves};
        for(unsigned i=0;i<3;i++) {
            ImGui::PushID(int(i));bool active=(p.mask&(1u<<i))!=0;
            if(ImGui::Checkbox("##enabled",&active)){p.mask=(p.mask&~(1u<<i))|(active?(1u<<i):0);palette_changed();}
            ImGui::SameLine();
            if(ImGui::ColorEdit3(labels[i],values[i],ImGuiColorEditFlags_NoInputs)){p.mask|=1u<<i;palette_changed();}
            ImGui::PopID();
        }
        if(ImGui::Button("Use original palette")){p.mask=0;preset=0;palette_changed();}
        ImGui::TextWrapped("The game keeps running while this is open.");
    }
    ImGui::End();
}

bool in_game() { return s.phase==INTRO||s.phase==PLAY||s.phase==FAILED||s.phase==RECONNECTING; }
unsigned player_count() { unsigned n=0;for(auto &p:s.players) if(p.present)n++;return n; }
unsigned arrived_count() { unsigned n=0;for(auto &p:s.players) if(p.present&&p.arrival_epoch==s.epoch)n++;return n; }
bool has_character(int c) { for(auto &p:s.players) if(p.present&&p.character==c)return true;return false; }
bool all_ready() {
    if(player_count()<2)return false;
    for(auto &p:s.players)if(p.present&&(!p.ready||!p.connected))return false;
    return true;
}
void emerald_notice(unsigned player,unsigned count) {
    auto &p=s.players[player];
    if(count<=p.emeralds_announced)return;
    p.emeralds_announced=count;
    s.notices[s.notice_next++%MAX_PLAYERS]={player,SDL_GetTicks(),true};
    s.notice_count++;s.last_emerald_player=int(player);
    fprintf(stderr,"[coop] Player %u (%s) got a chaos emerald!\n",player+1,characters[p.character]);
}
unsigned active_notices() {
    unsigned n=0,now=SDL_GetTicks();
    for(auto &notice:s.notices)if(notice.active&&int32_t(now-notice.started)<6000)n++;
    return n;
}
bool peer_arrived() { for(unsigned i=0;i<MAX_PLAYERS;i++)if(int(i)!=s.local_id&&s.players[i].present&&s.players[i].arrival_epoch==s.epoch)return true;return false; }
bool peer_side_trip() { for(unsigned i=0;i<MAX_PLAYERS;i++)if(int(i)!=s.local_id&&s.players[i].state_valid&&(s.players[i].state[15]&2))return true;return false; }
bool side_trip() {
    unsigned mode=r8(0xf600),stage=r16(0xee4e);
    return mode==0x34||mode==0x48||r8(0xfe48)!=0||stage==0x1701||
           (r8(0xee4e)>=0x13&&r8(0xee4e)<=0x15);
}
// The parent survives until every title-card child has left the screen.
// Level_started is too early; it allows sprites to cover the exiting text.
bool title_card_active() {
    for(unsigned obj=0xb0de;obj<0xcae2;obj+=0x4a)if(r32(obj)==0x02d690)return true;
    return false;
}
struct LevelMap {
    SDL_Texture *texture=nullptr,*local_texture=nullptr;
    std::array<unsigned char,STATE_BYTES> local_state{};
    std::vector<uint32_t> pixels;
    unsigned width=0,height=0,stage=~0u,world_stage=~0u,epoch=~0u,updated=0,solid=0;
    unsigned local_x=0,local_y=0,markers=0,shrink=4,world_width=0,world_height=0,art_pixels=0,sprite_markers=0;
    bool open=false,overview=false;
    float zoom=1,center_x=.5f,center_y=.5f,view_width=1600,view_height=960;
    uint16_t previous_pad=0;
} level_map;
const char *zone_names[]={"Angel Island","Hydrocity","Marble Garden","Carnival Night","Flying Battery","IceCap","Launch Base","Mushroom Hill","Sandopolis","Lava Reef","Sky Sanctuary","Death Egg","Doomsday"};
void snapshot(unsigned char *p);
void upload_player_texture(SDL_Renderer *renderer,SDL_Texture *&texture,const unsigned char *state,unsigned flip=0) {
    if(!texture) {
        texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,SIDE,SIDE);
        if(!texture)return;
        SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture,SDL_ScaleModeNearest);
    }
    std::array<uint32_t,SPRITE_BYTES> pixels;
    for(unsigned i=0;i<SPRITE_BYTES;i++){unsigned x=i%SIDE,y=i/SIDE;
        unsigned at=((flip&2)?SIDE-1-y:y)*SIDE+((flip&1)?SIDE-1-x:x);
        unsigned c=state[SPRITE_OFFSET+at];pixels[i]=c?get32(state+STATE_HEADER+c*4):0;}
    SDL_UpdateTexture(texture,nullptr,pixels.data(),SIDE*4);
}
void update_level_map(SDL_Renderer *renderer) {
    if(s.phase!=PLAY||s.start_pending||r8(0xf600)!=0x0c||side_trip()||title_card_active()||!r8(0xf711))return;
    auto &m=level_map;unsigned now=SDL_GetTicks(),stage=r16(0xee4e),world=r16(0xfe10);
    m.local_x=r16(0xb010);m.local_y=r16(0xb014);
    if(m.open){snapshot(m.local_state.data());upload_player_texture(renderer,m.local_texture,m.local_state.data());}
    if(m.texture&&m.stage==stage&&m.world_stage==world&&m.epoch==s.epoch&&(!m.open||int32_t(now-m.updated)<1000))return;
    unsigned cols=r16(0x8000),rows=r16(0x8004);
    if(!cols||cols>512||!rows||rows>32)return;
    SDL_RendererInfo info{};SDL_GetRendererInfo(renderer,&info);
    unsigned shrink=4;
    while((info.max_texture_width>0&&cols*128/shrink>unsigned(info.max_texture_width))||
          (info.max_texture_height>0&&rows*128/shrink>unsigned(info.max_texture_height)))shrink*=2;
    if(shrink>16)return;
    unsigned cell=16/shrink,width=cols*128/shrink,height=rows*128/shrink;
    // Reconstruct the foreground from the game's live 128px chunks, 16px
    // block definitions and 4bpp VRAM tiles. Downsample each native block once,
    // retaining its real palette, transparency and both tile/block flips.
    const unsigned char *vram=sonikk_vram();
    std::array<uint32_t,64> palette;for(unsigned i=0;i<64;i++)palette[i]=sonikk_cram_argb(i);
    std::vector<uint32_t> art(0x300*cell*cell);
    for(unsigned block=0;block<0x300;block++)for(unsigned y=0;y<cell;y++)for(unsigned x=0;x<cell;x++) {
        unsigned red=0,green=0,blue=0,count=0;
        for(unsigned yy=0;yy<shrink;yy++)for(unsigned xx=0;xx<shrink;xx++) {
            unsigned sx=x*shrink+xx,sy=y*shrink+yy;
            unsigned attr=r16(0x9000+block*8+(sy/8)*4+(sx/8)*2);
            unsigned tx=sx&7,ty=sy&7;if(attr&0x800)tx=7-tx;if(attr&0x1000)ty=7-ty;
            unsigned offset=((attr&0x7ff)*32+ty*4+tx/2)&65535;
            unsigned color=(vram[offset]>>((tx&1)?0:4))&15;if(!color)continue;
            unsigned rgb=palette[((attr>>13)&3)*16+color];
            red+=(rgb>>16)&255;green+=(rgb>>8)&255;blue+=rgb&255;count++;
        }
        if(count)art[block*cell*cell+y*cell+x]=((count*255/(shrink*shrink))<<24)|((red/count)<<16)|((green/count)<<8)|(blue/count);
    }
    m.pixels.assign(size_t(width)*height,0xff101b2b);m.solid=0;m.art_pixels=0;
    for(unsigned by=0;by<rows*8;by++) {
        unsigned row=r16(0x8008+(by/8)*4);if(row<0x8000||row+cols>0x9000)continue;
        for(unsigned bx=0;bx<cols*8;bx++) {
            unsigned chunk=r8(row+bx/8),block=r16(chunk*128+(by%8)*16+(bx%8)*2),id=block&0x3ff;
            if(block&0xf000)m.solid++;if(id>=0x300)continue;
            for(unsigned y=0;y<cell;y++)for(unsigned x=0;x<cell;x++) {
                unsigned sx=(block&0x400)?cell-1-x:x,sy=(block&0x800)?cell-1-y:y;
                unsigned rgb=art[id*cell*cell+sy*cell+sx],alpha=rgb>>24;
                if(!alpha)continue;
                unsigned r=(((rgb>>16)&255)*alpha+16*(255-alpha))/255;
                unsigned g=(((rgb>>8)&255)*alpha+27*(255-alpha))/255;
                unsigned b=((rgb&255)*alpha+43*(255-alpha))/255;
                m.pixels[(by*cell+y)*width+bx*cell+x]=0xff000000|(r<<16)|(g<<8)|b;m.art_pixels++;
            }
        }
    }
    if(m.width!=width||m.height!=height||!m.texture) {
        if(m.texture)SDL_DestroyTexture(m.texture);
        m.texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,int(width),int(height));
        if(m.texture)SDL_SetTextureScaleMode(m.texture,SDL_ScaleModeLinear);
    }
    m.width=width;m.height=height;m.shrink=shrink;m.world_width=cols*128;m.world_height=rows*128;
    m.stage=stage;m.world_stage=world;m.epoch=s.epoch;m.updated=now;
    if(m.texture)SDL_UpdateTexture(m.texture,nullptr,m.pixels.data(),int(width*4));
}
void toggle_level_map() {
    if(s.phase!=PLAY&&s.phase!=FAILED)return;
    level_map.open=!level_map.open;level_map.previous_pad=0;
    if(level_map.open){
        palette_editor=false;auto &m=level_map;m.zoom=1;m.overview=false;
        m.center_x=m.world_width?float(m.local_x)/m.world_width:.5f;
        m.center_y=m.world_height?float(m.local_y%m.world_height)/m.world_height:.5f;
    }
}
void map_input(uint16_t buttons) {
    auto &m=level_map;
    if((buttons&0x10)&&!(m.previous_pad&0x10)) {
        if(m.overview){m.overview=false;m.zoom=1;}else m.zoom=m.zoom>=8?1:m.zoom*2;
    }
    float step_x=m.world_width?m.view_width*.015f/m.world_width:0;
    float step_y=m.world_height?m.view_height*.015f/m.world_height:0;
    if(buttons&4)m.center_x-=step_x;if(buttons&8)m.center_x+=step_x;
    if(buttons&1)m.center_y-=step_y;if(buttons&2)m.center_y+=step_y;
    m.center_x=(std::max)(0.0f,(std::min)(1.0f,m.center_x));
    m.center_y=(std::max)(0.0f,(std::min)(1.0f,m.center_y));m.previous_pad=buttons;
}
void draw_level_map() {
    auto &m=level_map;m.markers=0;m.sprite_markers=0;if(!m.open)return;
    ImGuiViewport *vp=ImGui::GetMainViewport();
    ImVec2 size((std::max)(300.0f,vp->WorkSize.x*.88f),(std::max)(210.0f,vp->WorkSize.y*.76f));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x+vp->WorkSize.x*.5f,vp->WorkPos.y+vp->WorkSize.y*.54f),ImGuiCond_Always,ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(size,ImGuiCond_Always);
    if(ImGui::Begin("Level map",&m.open,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoMove)) {
        unsigned zone=m.stage>>8;
        if(m.texture)ImGui::Text("%s - Act %u",zone<13?zone_names[zone]:"Current area",(m.stage&255)+1);
        ImGui::TextDisabled("Jump / wheel: zoom | D-pad / right-drag: pan | Select: close");
        if(ImGui::Button("Center on me")){m.overview=false;m.zoom=1;m.center_x=m.world_width?float(m.local_x)/m.world_width:.5f;m.center_y=m.world_height?float(m.local_y%m.world_height)/m.world_height:.5f;}
        ImGui::SameLine();if(ImGui::Button("Whole level")){m.overview=true;m.zoom=1;m.center_x=m.center_y=.5f;}
        draw_team_map_tools();
        if(!m.texture)ImGui::TextWrapped("The level map will appear after the area has loaded.");
        else {
            ImVec2 start=ImGui::GetCursorScreenPos(),area=ImGui::GetContentRegionAvail();
            area.y=(std::max)(60.0f,area.y-(std::max)(80.0f,float((player_count()+1)/2)*23.0f));
            ImDrawList *draw=ImGui::GetWindowDrawList();
            ImVec2 end(start.x+area.x,start.y+area.y);
            draw->AddRectFilled(start,end,IM_COL32(10,16,27,255));draw->PushClipRect(start,end,true);
            // Default to a useful nearby view, fill both canvas dimensions,
            // and keep the game's aspect ratio. Full-level fitting is opt-in.
            float fill=(std::max)(area.x/m.width,area.y/m.height);
            float nearby=(std::max)(area.x*m.shrink/1600.0f,area.y*m.shrink/960.0f);
            float scale=(m.overview?(std::min)(area.x/m.width,area.y/m.height):(std::max)(fill,nearby))*m.zoom;
            m.view_width=area.x*m.shrink/scale;m.view_height=area.y*m.shrink/scale;
            if(!m.overview){float hx=area.x/(2*m.width*scale),hy=area.y/(2*m.height*scale);
                m.center_x=(std::max)(hx,(std::min)(1-hx,m.center_x));m.center_y=(std::max)(hy,(std::min)(1-hy,m.center_y));}
            ImVec2 origin(start.x+area.x*.5f-m.center_x*m.width*scale,start.y+area.y*.5f-m.center_y*m.height*scale);
            draw->AddImage((ImTextureID)(intptr_t)m.texture,origin,ImVec2(origin.x+m.width*scale,origin.y+m.height*scale));
            for(unsigned i=0;i<MAX_PLAYERS;i++) {
                if(!s.players[i].present||!s.players[i].connected)continue;
                bool local=int(i)==s.local_id;auto &p=s.players[i].state;
                bool visible=local?!side_trip():(s.players[i].state_valid&&get32(p.data())==m.epoch&&get16(p.data()+4)==m.stage&&state_layout(p.data())==m.world_stage&&!(p[15]&2));
                if(!visible)continue;
                auto shown=player_view(i,double(SDL_GetTicks64()));
                float x=shown.x,y=std::fmod(shown.y,float(m.world_height));
                ImVec2 point(origin.x+x*scale/m.shrink,origin.y+y*scale/m.shrink);
                bool outside=point.x<start.x+18||point.x>end.x-18||point.y<start.y+18||point.y>end.y-18;
                ImVec2 actual=point;
                point.x=(std::max)(start.x+18,(std::min)(end.x-18,point.x));
                point.y=(std::max)(start.y+18,(std::min)(end.y-18,point.y));
                SDL_Texture *sprite=local?m.local_texture:remote_textures[i];
                // A readable minimum keeps a whole-act view useful. Zooming
                // increases the genuine animated sprite up to 3/4 native size.
                float sprite_scale=(std::max)(.4f,(std::min)(.75f,scale/m.shrink));
                float half=SIDE*sprite_scale*.5f;
                if(sprite) {
                    draw->AddImage((ImTextureID)(intptr_t)sprite,ImVec2(point.x-half,point.y-half),ImVec2(point.x+half,point.y+half));
                    m.sprite_markers++;
                }
                char label[25];snprintf(label,sizeof(label),"%s",player_name(i));
                if(outside){float dx=actual.x-point.x,dy=actual.y-point.y,len=sqrtf(dx*dx+dy*dy);if(len>0){dx/=len;dy/=len;ImVec2 tip(point.x+dx*16,point.y+dy*16);draw->AddTriangleFilled(tip,ImVec2(tip.x-dx*7-dy*4,tip.y-dy*7+dx*4),ImVec2(tip.x-dx*7+dy*4,tip.y-dy*7-dx*4),IM_COL32(255,255,255,255));}}
                float label_x=(std::max)(start.x+2,(std::min)(end.x-ImGui::CalcTextSize(label).x-2,point.x+9));
                float label_y=(std::max)(start.y+2,(std::min)(end.y-14,point.y-15-float(i%3)*11));
                draw->AddText(ImVec2(label_x,label_y),IM_COL32(255,255,255,255),label);m.markers++;
            }
            draw_team_pings(draw,origin,scale,m.shrink,m.stage,m.epoch);
            ImVec2 center(start.x+area.x*.5f,start.y+area.y*.5f);draw->AddLine(ImVec2(center.x-5,center.y),ImVec2(center.x+5,center.y),IM_COL32(255,255,255,160));draw->AddLine(ImVec2(center.x,center.y-5),ImVec2(center.x,center.y+5),IM_COL32(255,255,255,160));
            draw->PopClipRect();ImGui::InvisibleButton("##mapcanvas",area,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
            if(ImGui::IsItemHovered()&&ImGui::GetIO().MouseWheel!=0){m.overview=false;m.zoom=(std::max)(1.0f,(std::min)(8.0f,m.zoom*(ImGui::GetIO().MouseWheel>0?2.0f:.5f)));}
            if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Right)){auto d=ImGui::GetIO().MouseDelta;m.center_x-=d.x/(m.width*scale);m.center_y-=d.y/(m.height*scale);}
            if(ImGui::IsItemClicked(ImGuiMouseButton_Left)) {ImVec2 mouse=ImGui::GetMousePos();float x=(mouse.x-origin.x)*m.shrink/scale,y=(mouse.y-origin.y)*m.shrink/scale;
                if(x>=0&&y>=0&&x<m.world_width&&y<m.world_height)team_map_click(unsigned(x),unsigned(y));}
            for(unsigned i=0;i<MAX_PLAYERS;i++)if(s.players[i].present) {
                bool local=int(i)==s.local_id;auto &p=s.players[i].state;
                bool special=local?side_trip():(s.players[i].state_valid&&(p[15]&2));
                char label[100];snprintf(label,sizeof(label),"%s (%s)%s%s",player_name(i),characters[s.players[i].character],local?" (you)":"",special?" - special stage":"");
                if(ImGui::Button(label)&&!special) {
                    unsigned x=local?m.local_x:get16(p.data()+6),y=local?m.local_y:get16(p.data()+8);
                    m.overview=false;m.zoom=1;m.center_x=float(x)/(m.world_width);m.center_y=float(y%(m.world_height))/(m.world_height);
                }
                if(i%2==0&&i+1<MAX_PLAYERS&&s.players[i+1].present)ImGui::SameLine();
            }
        }
    }
    ImGui::End();
}

bool would_block() {
#ifdef _WIN32
    int e=WSAGetLastError();return e==WSAEWOULDBLOCK||e==WSAEINPROGRESS;
#else
    return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINPROGRESS;
#endif
}
void close_socket(CoopSocket &fd) {
    if(fd==BAD_SOCKET)return;
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
    fd=BAD_SOCKET;
}
bool nonblocking(CoopSocket fd) {
#ifdef _WIN32
    u_long on=1;return ioctlsocket(fd,FIONBIO,&on)==0;
#else
    int f=fcntl(fd,F_GETFL,0);return f>=0&&fcntl(fd,F_SETFL,f|O_NONBLOCK)==0;
#endif
}
void disconnect_channel(unsigned index) {
    auto &c=channels[index];int who=c.peer;close_socket(c.fd);c=Channel{};
    if(s.host) {
        if(who>0&&who<int(MAX_PLAYERS)) {
            s.players[who].connected=false;team_disconnect(unsigned(who));
            if(!in_game())s.players[who]=Player{};
            roster();
        }
    } else if(in_game()&&s.phase!=FAILED) {
        if(!s.reconnecting){s.resume_phase=s.phase;s.saved_pause=native_unpause?0:r16(0xf63a);}
        s.reconnecting=true;s.connecting_socket=false;s.hello=false;s.phase=RECONNECTING;
        s.retry_at=SDL_GetTicks()+1500;w16(0xf63a,1);team_disconnect(unsigned(s.local_id));
        snprintf(s.error,sizeof(s.error),"Connection interrupted. Rejoining your slot...");
    } else if(!s.host&&s.phase!=OFF&&s.phase!=FAILED){s.phase=FAILED;s.hello=false;snprintf(s.error,sizeof(s.error),"Disconnected from the lobby. Return to data select and join again.");}
}
void fail(const char *reason) {
    fprintf(stderr,"[coop] failure phase=%d player=%d epoch=%u arrived=%u/%u: %s\n",int(s.phase),s.local_id,s.epoch,arrived_count(),player_count(),reason);
    snprintf(s.error,sizeof(s.error),"%s",reason);s.phase=FAILED;
    close_socket(s.listener);
    for(auto &c:channels){close_socket(c.fd);c.tx_size=c.rx_size=0;}
    for(auto &p:s.players)p.state_valid=false;
}
void flush(unsigned index) {
    auto &c=channels[index];
    if(c.fd==BAD_SOCKET||s.phase==CONNECTING)return;
    while(c.tx_size) {
#ifdef MSG_NOSIGNAL
        int flags=MSG_NOSIGNAL;
#else
        int flags=0;
#endif
        int n=send(c.fd,reinterpret_cast<const char*>(c.tx.data()),int(c.tx_size),flags);
        if(n<0&&would_block())return;
        if(n<=0){disconnect_channel(index);return;}
        c.tx_size-=n;memmove(c.tx.data(),c.tx.data()+n,c.tx_size);
    }
}
void send_packet(unsigned index,unsigned kind,const unsigned char *data,unsigned length) {
    auto &c=channels[index];
    if(c.fd==BAD_SOCKET||s.phase==FAILED)return;
    if(length>MAX_PACKET||c.tx_size+length+8>CAPACITY){disconnect_channel(index);return;}
    unsigned char *p=c.tx.data()+c.tx_size;
    put16(p,0x5333);p[2]=VERSION;p[3]=uint8_t(kind);put32(p+4,length);
    if(length)memcpy(p+8,data,length);
    c.tx_size+=length+8;s.sent++;c.last_send=SDL_GetTicks();flush(index);
}
void broadcast(unsigned kind,const unsigned char *data,unsigned length,int except=-1) {
    for(unsigned i=1;i<MAX_CHANNELS;i++)if(channels[i].hello&&channels[i].peer!=except)send_packet(i,kind,data,length);
}
void send_to_player(unsigned who,unsigned kind,const unsigned char *data,unsigned length) {
    if(!s.host){if(who==0)send_packet(0,kind,data,length);return;}
    for(unsigned i=1;i<MAX_CHANNELS;i++)if(channels[i].hello&&channels[i].peer==int(who)){send_packet(i,kind,data,length);return;}
}
void send_value(unsigned kind,unsigned value) { unsigned char p[4];put32(p,value);send_packet(0,kind,p,4); }
void roster() {
    if(!s.host)return;
    unsigned char p[4+MAX_PLAYERS*32]{};
    for(unsigned i=0;i<MAX_PLAYERS;i++) { auto &v=s.players[i];unsigned char *q=p+4+i*32;
        q[0]=v.present;q[1]=uint8_t(v.character);q[2]=v.ready;q[3]=v.connected;
        memcpy(q+4,v.name,24);q[28]=uint8_t(v.chaos_mask);q[29]=uint8_t(v.super_mask);
    }
    for(unsigned i=1;i<MAX_CHANNELS;i++)if(channels[i].hello&&channels[i].peer>0){put32(p,unsigned(channels[i].peer));send_packet(i,ROSTER,p,sizeof(p));}
}
void connected(unsigned index) {
    auto &c=channels[index];if(!in_game()&&!s.reconnecting)s.phase=LOBBY;c.last_recv=c.last_send=SDL_GetTicks();
    int yes=1;setsockopt(c.fd,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char*>(&yes),sizeof(yes));
    unsigned char p[52]{};put32(p,rom_crc);put32(p+4,s.character);put32(p+8,s.host?0:1);
    put64(p+12,s.room);put64(p+20,s.host?0:s.token);memcpy(p+28,local_name,24);
    send_packet(index,HELLO,p,sizeof(p));
}
void leave(bool menu) {
    bool was_game=in_game();progress_clear();team_reset();
    if(((s.reconnecting&&!s.saved_pause)||(menu&&was_game))&&r16(0xf63a))native_unpause=true;
    if(menu){ticket=ResumeTicket{};remove("sonic3k_resume.ini");}
    level_map.open=false;palette_editor=false;save_palettes();
    level_map.stage=level_map.epoch=~0u;close_socket(s.listener);
    for(auto &c:channels){close_socket(c.fd);c=Channel{};}
    int character=s.character;s=Session{};s.character=character;remote_drawn=0;
    if(menu&&was_game)w8(0xf600,0x4c);
}
bool begin(bool host,const char *ip,int port) {
    if(r8(0xf600)!=0x4c)return false;
    leave(false);s.host=host;s.token=new_token();s.room=host?new_token():0;lobby_open=true;
    if(port<1||port>65535){fail("Choose a port between 1 and 65535.");return false;}
    port_number=port;
#ifdef _WIN32
    if(!socket_ready){WSADATA d;if(WSAStartup(MAKEWORD(2,2),&d)){fail("Networking could not start.");return false;}socket_ready=true;}
#endif
    CoopSocket fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(fd==BAD_SOCKET||!nonblocking(fd)){close_socket(fd);fail("Could not open a network socket.");return false;}
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(uint16_t(port));
    if(host) {
        a.sin_addr.s_addr=htonl(INADDR_ANY);
        if(bind(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))||listen(fd,MAX_PLAYERS-1)){
            close_socket(fd);fail("That port is unavailable. Choose another host port.");return false;
        }
        s.listener=fd;s.phase=LISTENING;s.local_id=0;
        s.players[0].present=s.players[0].connected=true;s.players[0].character=s.character;s.players[0].token=s.token;snprintf(s.players[0].name,25,"%s",local_name);
    } else {
        if(!strcmp(ip,"localhost"))ip="127.0.0.1";
        if(inet_pton(AF_INET,ip,&a.sin_addr)!=1){close_socket(fd);fail("Enter the host's IPv4 address.");return false;}
        snprintf(peer_address,sizeof(peer_address),"%s",ip);channels[0].peer=0;channels[0].fd=fd;s.phase=CONNECTING;s.connect_at=SDL_GetTicks();
        int result=connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a));
        if(result==0)connected(0);
        else if(!would_block()){fail("Could not connect to that host and port.");return false;}
    }
    return true;
}
void choose(int c) {
    if(c<0||c>2||in_game())return;
    s.character=c;
    if(s.local_id>=0){auto &p=s.players[s.local_id];p.character=c;p.ready=false;}
    if(s.host)roster();else if(s.hello)send_value(CHOICE,c);
}
void ready(bool value) {
    if(s.phase!=LOBBY||!s.hello||s.local_id<0)return;
    s.players[s.local_id].ready=value;
    if(s.host)roster();else send_value(READY,value?1:0);
}
void begin_game() {
    s.shared_intro=has_character(0);s.phase=s.shared_intro?INTRO:PLAY;
    s.start_pending=true;s.start_frames=0;s.epoch=s.shared_intro?0:1;
    s.released=s.waiting=s.intro_finished=s.handoff=s.hit=false;
    for(auto &p:s.players){p.arrival_epoch=~0u;p.state_valid=false;}
    s.knux_captured=false;s.frames=0;s.chaos_observed=r8(0xffb0);lobby_open=false;
    w8(0xef4b,0);w16(0xef4c,s.shared_intro?0:s.character+1);
}
void start() {
    if(s.host&&s.phase==LOBBY&&all_ready()&&r8(0xf600)==0x4c) {
        // Pending handshakes cannot be silently omitted from the roster.
        for(unsigned i=1;i<MAX_CHANNELS;i++)if(channels[i].fd!=BAD_SOCKET&&!channels[i].hello)return;
        if(!progress_restart(false))return;
        unsigned char p[4]{};broadcast(START,p,sizeof(p));if(s.phase!=FAILED)begin_game();
    }
}
void maybe_release() {
    // Release authority is the confirmed recomp-net state, for every seat.
    if(!s.waiting||!progress.active())return;
    if(progress.committed().epoch>s.epoch){s.last_release=s.epoch;s.released=true;}
}
bool barrier(unsigned stage) {
    if(s.phase==FAILED||s.local_id<0)return false;
    auto &local=s.players[s.local_id];
    if(!s.waiting){local.arrival_epoch=s.epoch;local.stage=stage;s.waiting=true;s.barrier_stage=stage;}
    maybe_release();if(!s.released)return false;
    s.epoch++;s.released=s.waiting=false;return true;
}
// Sprite canvases are mostly transparent. Run-length encoding keeps an
// eight-player host from relaying seven full 96x96 canvases to every client.
// Raw fallback bounds the worst case; malformed runs fail closed.
unsigned encode_state(unsigned who,const unsigned char *state,unsigned char *out) {
    put32(out,who);out[4]=1;memcpy(out+5,state,SPRITE_OFFSET);
    unsigned n=SPRITE_OFFSET+5;
    for(unsigned at=SPRITE_OFFSET;at<STATE_BYTES;) {
        unsigned run=1;
        while(at+run<STATE_BYTES&&run<255&&state[at+run]==state[at])run++;
        if(n+2>=STATE_BYTES+5){out[4]=0;memcpy(out+5,state,STATE_BYTES);return STATE_BYTES+5;}
        out[n++]=uint8_t(run);out[n++]=state[at];at+=run;
    }
    return n;
}
bool decode_state(const unsigned char *p,unsigned n,unsigned char *out) {
    if(n<SPRITE_OFFSET+5||p[4]>1)return false;
    if(!p[4]){if(n!=STATE_BYTES+5)return false;memcpy(out,p+5,STATE_BYTES);}
    else {
        memcpy(out,p+5,SPRITE_OFFSET);unsigned at=SPRITE_OFFSET;
        for(unsigned pos=SPRITE_OFFSET+5;pos<n;pos+=2) {
            if(pos+1>=n||!p[pos]||p[pos+1]>63||at+p[pos]>STATE_BYTES)return false;
            memset(out+at,p[pos+1],p[pos]);at+=p[pos];
        }
        if(at!=STATE_BYTES)return false;
    }
    for(unsigned i=SPRITE_OFFSET;i<STATE_BYTES;i++)if(out[i]>63)return false;
    return true;
}
bool handle(unsigned index,unsigned kind,const unsigned char *p,unsigned n) {
    auto &c=channels[index];
    if(kind==HELLO) {
        if(n!=52||c.hello||get32(p)!=rom_crc||get32(p+4)>2||get32(p+8)!=(s.host?1u:0u))return false;
        if(s.host) {
            uint64_t room=get64(p+12),token=get64(p+20);if(!token)return false;
            int who=-1;
            if(room) {
                if(room!=s.room||!in_game())return false;
                for(unsigned i=1;i<MAX_PLAYERS;i++)if(s.players[i].present&&s.players[i].token==token&&!s.players[i].connected){who=int(i);break;}
            } else if(!in_game())for(unsigned i=1;i<MAX_PLAYERS;i++)if(!s.players[i].present){who=int(i);break;}
            if(who<1)return false;
            c.peer=who;auto &v=s.players[who];v.present=true;v.connected=!room;v.token=token;
            if(!room){v.character=int(get32(p+4));clean_name(v.name,p+28,24);}
            c.hello=true;s.hello=true;if(room)team_reset_peer(unsigned(who));roster();if(room)send_resume(index);
        } else {
            if(!get64(p+12)||(s.room&&s.room!=get64(p+12)))return false;
            s.room=get64(p+12);c.peer=0;c.hello=true;s.hello=true;
        }
        return true;
    }
    if(!c.hello)return false;
    switch(kind) {
    case ROSTER: {
        if(s.host||n!=4+MAX_PLAYERS*32||get32(p)==0||get32(p)>=MAX_PLAYERS)return false;
        unsigned local=get32(p);if(!p[4]||!p[4+local*32])return false;
        for(unsigned i=0;i<MAX_PLAYERS;i++) {
            const unsigned char *q=p+4+i*32;if(q[0]>1||q[1]>2||q[2]>1||q[3]>1)return false;
            auto &v=s.players[i];v.present=q[0]!=0;v.character=q[1];v.ready=q[2]!=0;v.connected=q[3]!=0;
            clean_name(v.name,q+4,24);v.chaos_mask=q[28]&127;v.super_mask=q[29]&127;
        }
        s.local_id=int(local);s.character=s.players[local].character;save_ticket();return true;
    }
    case CHOICE:
        if(!s.host||n!=4||s.phase!=LOBBY||get32(p)>2)return false;
        s.players[c.peer].character=int(get32(p));s.players[c.peer].ready=false;roster();return true;
    case READY:
        if(!s.host||n!=4||s.phase!=LOBBY||get32(p)>1)return false;
        s.players[c.peer].ready=get32(p)!=0;roster();return true;
    case START:
        if(n!=4||get32(p)!=0||s.host||s.phase!=LOBBY||s.local_id<0||!all_ready()||r8(0xf600)!=0x4c)return false;
        begin_game();return true;
    case PROGRESS_CONFIG:return progress_handle(p,n);
    case ARRIVE:case RELEASE:return false; // Shared driver owns progression.
    case STATE: {
        if(n<SPRITE_OFFSET+5||!in_game())return false;
        unsigned who=get32(p);
        if(who>=MAX_PLAYERS||!s.players[who].present||int(who)==s.local_id||(s.host&&int(who)!=c.peer))return false;
        auto *state=s.players[who].state.data();
        if(!decode_state(p,n,state)||state[10]!=s.players[who].character||state[11]>1||(state[15]&~3)||state_layout(state)>0x1701)return false;
        s.players[who].state_valid=true;
        PlayerMotion::Frame shown;shown.tick=get32(state+16);shown.epoch=get32(state);shown.stage=(state_layout(state)<<16)|get16(state+4);
        shown.y_period=float(state_y_period(state));
        shown.warp=get32(state+28);shown.visible=state[11]!=0;
        shown.x=float(get16(state+6))+float(get16(state+24))/65536;
        shown.y=float(get16(state+8))+float(get16(state+26))/65536;
        memcpy(shown.image.data(),state,STATE_BYTES);remote_motion[who].push(std::move(shown),double(SDL_GetTicks64()));
        if(s.host)for(unsigned i=1;i<MAX_CHANNELS;i++)
            if(i!=index&&channels[i].hello&&channels[i].tx_size==0)send_packet(i,STATE,p,n);
        return true;
    }
    case RINGS:return false; // Never import progress from the presentation channel.
    case EMERALD:return false; // Replaced by authoritative inventory awards in protocol 5.
    case PING:return n==0;
    default:return team_handle(index,kind,p,n);
    }
}
unsigned crc32() {
    unsigned crc=~0u;
    for(unsigned i=0;i<sizeof(g_rom);i++){crc^=g_rom[i];for(int b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&unsigned(-int(crc&1)));}
    return ~crc;
}
// Called only at the audited campaign/NPC/boss CMP consumers in game.toml.
// The native avatar stays Knuckles. In particular, do not change Player_mode
// globally: that would break his palettes, Super/Hyper and special-stage art.
int campaign_route_hook() {
    if((s.phase==PLAY||s.phase==FAILED||s.phase==RECONNECTING)&&s.character==2) {
        // Both CMP.W #3,Player_mode and CMP.B #2,Player_1.character_id use
        // the Sonic/Tails branch with N=1 Z=0 V=0 C=1; CMP preserves X.
        g_cpu.SR=uint16_t((g_cpu.SR&~0x0fu)|0x09u);s.route_hits++;
    }
    return 0;
}
int icecap_spawn_hook() {
    // Both sites are inside ICZ1-only native loader branches. Equality chooses
    // Tails' camera/start coordinates and entrance object for every character.
    // Preserve X, as CMP does; never change the actual avatar or Player_mode.
    if(s.phase==PLAY||s.phase==FAILED||s.phase==RECONNECTING)g_cpu.SR=uint16_t((g_cpu.SR&~0x0fu)|0x04u);
    return 0;
}
void shared_release_result_signal(unsigned);
int finish_hook() {
    if(s.phase==OFF||s.phase==LOBBY||s.phase==LISTENING||s.phase==CONNECTING) return 0;
    if(s.phase!=PLAY) return 1;
    // Special/bonus results are private. Even if a native route reuses this
    // routine, they must never count as clearing the normal act.
    if(r8(0xf600)!=0x0c||side_trip()) return 0;
    // Native results normally leave the underwater arena before air expires.
    // A party can wait here indefinitely; drowning would delete this player's
    // results controller and leave the confirmed clear barrier stranded.
    w8(0xb02c,30);
    unsigned cleared_epoch=s.epoch;
    if(!barrier(r16(0xee4e)))return 1;
    shared_release_result_signal(cleared_epoch);return 0;
}
int knux_hook() {
    if(s.phase==INTRO&&has_character(2)) {
        unsigned obj=g_cpu.A[0]&65535;
        s.knux_x=r16(obj+0x10);s.knux_y=r16(obj+0x14);s.knux_captured=true;
        // loc_61F10 takes its native cleanup branch without walking offscreen.
        w8(obj+4,r8(obj+4)&0x7f);
    }
    return 0;
}
int hit_hook() {
    if(s.phase==INTRO) {
        s.hit=true;
        // Release the AIZ-specific hidden CPU state into the game's ordinary
        // catch-up flight. Native code positions him above Sonic and descends.
        if(has_character(1)) w16(0xf708,2);
    }
    return 0;
}
void handoff() {
    unsigned x=r16(0xb010),y=r16(0xb014);
    if(s.character==1) { x=r16(0xb05a);y=r16(0xb05e); }
    if(s.character==2) { x=s.knux_x;y=s.knux_y; }
    w16(0xff08,s.character+1);w16(0xff0a,s.character+1);
    if(s.character!=0) {
        if(s.character==1) {
            // Transfer the real, fully descended Tails object. Its animation,
            // collision state and native flight state survive the handoff.
            unsigned char tails[0x4a];memcpy(tails,g_ram+0xb04a,sizeof(tails));
            for(unsigned i=0;i<sizeof(tails);i++) w8(0xb000+i,tails[i]);
            w8(0xb02e,0);w16(0xcc0a+0x30,0xb000);
            w16(0xf708,6);
        } else {
            for(unsigned i=0;i<0x4a;i++) w8(0xb000+i,0);
            w32(0xb000,0x016444);w16(0xb010,x);w16(0xb014,y);
            // Native Knuckles Init will populate mappings, DPLC and physics.
            // Pal_Knuckles ($0A8AFC), also used by the native level loader.
            // The cutscene's second palette line has already been restored by
            // AfterBoss_Cleanup, so it cannot supply the playable palette.
            w8(0xf65f,0);
            for(unsigned i=0;i<32;i++) {
                w8(0xfc00+i,g_rom[0x0a8afc+i]);
                w8(0xfc80+i,g_rom[0x0a8afc+i]);
            }
        }
        w32(0xcce8,0); // remove Sonic's insta-shield object for other characters
    }
    for(unsigned i=0;i<0x4a;i++) w8(0xb04a+i,0);
    if(s.character!=1) { w32(0xcc0a,0);w32(0xcc9e,0); }
    w8(0xf766,0xff);w8(0xf7de,0xff);w8(0xf7df,0xff);
    w8(0xf7ca,0);w16(0xf602,0);w16(0xf606,0);
    w16(0xfe2e,x);w16(0xfe30,y);w32(0xfe22,0);
    s.hud_pending=s.character!=0;s.handoff=true;s.phase=PLAY;
}

void publish_rings(unsigned zone,uint32_t mask) {
    if(zone<14)progress_wanted_rings[zone]|=mask;
}
void sync_rings() {
    if(s.phase!=PLAY||s.start_pending||r8(0xf600)!=0x0c||side_trip()||!r8(0xf711))return;
    unsigned zone=r8(0xee4e);if(zone>=s.used_rings.size())return;
    uint32_t native=r32(0xff92);
    for(unsigned obj=0xb0de;obj<0xcae2;obj+=0x4a) {
        // Obj_WaitOffscreen replaces Obj_SSEntryRing with its continuation.
        if(r32(obj)!=0x061682||r8(obj+5)!=4)continue;
        uint32_t bit=uint32_t(1)<<(r8(obj+0x2c)&31);
        s.entering_rings[zone]|=bit;publish_rings(zone,bit);
    }
    // Covers the 50-ring reward when all emeralds are already owned.
    if(s.observed_ring_zone==int(zone))publish_rings(zone,native&~s.observed_rings);
    s.observed_ring_zone=int(zone);s.entering_rings[zone]&=~native;
    uint32_t remote_used=s.used_rings[zone]&~s.entering_rings[zone];
    uint32_t merged=native|remote_used;
    if(merged!=native)w32(0xff92,merged);
    s.observed_rings=merged;
    for(unsigned obj=0xb0de;obj<0xcae2;obj+=0x4a) {
        uint32_t bit=uint32_t(1)<<(r8(obj+0x2c)&31),code=r32(obj);
        if(!(remote_used&bit))continue;
        if(code==0x061682) {
            // Native cleanup restores explosion art and releases the VRAM
            // tracking slot. A bare delete would leak that slot.
            w32(obj,r8(obj+5)?0x06196a:0x06166a);
        } else if(code==0x085ad2&&r32(obj+0x34)==0x061682) {
            // An offscreen ring has not allocated its graphics slot yet.
            w32(obj,0x06166a);
        }
    }
}
void snapshot(unsigned char *p) {
    memset(p,0,STATE_BYTES);
    put32(p,s.epoch);put16(p+4,r16(0xee4e));put16(p+6,r16(0xb010));put16(p+8,r16(0xb014));
    p[10]=uint8_t(s.character);p[11]=(s.phase==PLAY&&!s.start_pending&&r8(0xf600)==0x0c&&!title_card_active()&&!side_trip())?1:0;
    put16(p+12,r16(0xfe20));p[14]=r8(0xfe12);p[15]=(s.waiting?1:0)|(side_trip()?2:0);
    unsigned period=local_y_period();
    put32(p+16,s.frames);p[20]=(r8(0xb004)&3)|(r8(0xfe10)<<2)|(period?128:0);p[21]=r8(0xb022);p[22]=r8(0xb020);p[23]=r8(0xfe11)|(period==4096?128:0);
    put16(p+24,r16(0xb012));put16(p+26,r16(0xb016));put32(p+28,motion_warp);
    for(unsigned i=0;i<64;i++) {
        uint32_t color=sonikk_cram_argb(i);
        if(s.phase==PLAY&&(i/16)==((r16(0xb00a)>>13)&3))color=palette_color(i,color,false);
        put32(p+STATE_HEADER+i*4,color);
    }
    if(!p[11]) return;
    // Use the actual native sprite table, after DPLC uploads. Capture only
    // the selected player's tile bank, retaining native flips and animation.
    const unsigned char *vram=sonikk_vram();
    int px=int(r16(0xb010))-int(r16(0xee80))+128;
    int py=int(adventure::wrapped_delta(float(int(r16(0xb014))-int(r16(0xee84))),float(period)))+128;
    unsigned link=0;
    for(unsigned i=0;i<80;i++) {
        unsigned a=0xf800+link*8, attr=r16(a+4), tile=attr&0x7ff;
        link=r8(a+3)&127;
        if(!own_character_sprite(attr)) { if(!link||link>=80) break; else continue; }
        int sx=int(r16(a+6)&511)-px+int(SIDE/2);
        int sy=int(r16(a)&511)-py+int(SIDE/2);
        unsigned size=r8(a+2),w=(((size>>2)&3)+1)*8,h=((size&3)+1)*8;
        for(unsigned yy=0;yy<h;yy++) for(unsigned xx=0;xx<w;xx++) {
            int dx=sx+int(xx),dy=sy+int(yy);
            if(dx<0||dy<0||dx>=int(SIDE)||dy>=int(SIDE)) continue;
            unsigned tx=(attr&0x800)?w-1-xx:xx,ty=(attr&0x1000)?h-1-yy:yy;
            unsigned nt=tile+(tx/8)*(h/8)+(ty/8),offset=(nt*32+(ty%8)*4+(tx%8)/2)&65535;
            unsigned col=(vram[offset]>>((tx&1)?0:4))&15;
            if(col) p[SPRITE_OFFSET+dy*SIDE+dx]=uint8_t(col+((attr>>13)&3)*16);
        }
        if(!link||link>=80) break;
    }
}
#include "sonic3k_coop_emeralds.inc"
#include "sonic3k_coop_progress.inc"
#include "sonic3k_coop_team.inc"
PlayerMotion::View player_view(unsigned who,double now) {
    if(who>=MAX_PLAYERS)return {};
    auto native=[](){return PlayerMotion::View{float(r16(0xb010)),float(r16(0xb014)),nullptr,false};};
    auto raw=[&](unsigned id){auto v=int(id)==s.local_id?native():remote_motion[id].sample(now);v.y=adventure::wrapped_position(v.y,v.frame?v.frame->y_period:float(local_y_period()));return v;};
    auto view=raw(who);int carrier=team.carrier[who];
    if(carrier>=0&&active_pose(unsigned(carrier))&&s.players[carrier].pose.stage==s.players[who].pose.stage&&s.players[carrier].pose.actual==s.players[who].pose.actual) {
        auto anchor=raw(unsigned(carrier));
        if(int(carrier)==s.local_id||anchor.frame){unsigned flags=int(carrier)==s.local_id?r8(0xb004):anchor.frame->image[20];
            auto grip=carry_grip(who,flags);view.x=anchor.x+grip.x;view.y=anchor.y+grip.y;}
    } else if(s.local_id>=0&&team.cargo[who]==s.local_id&&team.carry_owned&&!team.carry_blocked) {
        // The passenger's native world/camera uses this same delayed carrier.
        // Keep their on-screen attachment exact between simulation and rendering.
        auto anchor=native();auto grip=carry_grip(unsigned(s.local_id),r8(0xb004));view.x=anchor.x-grip.x;view.y=anchor.y-grip.y;
    }
    view.y=adventure::wrapped_position(view.y,view.frame?view.frame->y_period:float(local_y_period()));return view;
}
unsigned carry_sprite_flip(unsigned who,const unsigned char *state,double now) {
    int carrier=who<MAX_PLAYERS?team.carrier[who]:-1;if(carrier<0||!active_pose(unsigned(carrier)))return 0;
    auto anchor=player_view(unsigned(carrier),now);
    unsigned flags=int(carrier)==s.local_id?r8(0xb004):(anchor.frame?anchor.frame->image[20]:state[20]);
    return (state[20]^flags)&3;
}
#if OA_TEST_CONTROL
struct MotionDraw {uint64_t frame=0;unsigned who=0;float x=0,y=0,cx=0,cy=0;int carrier=-1;};
std::deque<MotionDraw> motion_draws;
#endif
}

extern "C" void s3k_coop_pre(uint64_t){team_pre_frame();}
extern "C" int s3k_coop_world_instruction(uint32_t pc){return shared_instruction(pc);}
extern "C" void s3k_coop_init() {
    load_identity();load_palettes();machine_set_sprite_tint(tint_character);
    rom_crc=crc32();g_recomp_hooks[11]=finish_hook;g_recomp_hooks[12]=knux_hook;g_recomp_hooks[13]=hit_hook;g_recomp_hooks[14]=campaign_route_hook;g_recomp_hooks[15]=icecap_spawn_hook;g_recomp_hooks[16]=boss_a1_hook;g_recomp_hooks[18]=switch_hook;
    g_recomp_hooks[19]=boss_hpz_hook;g_recomp_hooks[20]=boss_fbz_hook;g_recomp_hooks[21]=boss_lrz_hook;g_recomp_hooks[22]=boss_dez_hook;g_recomp_hooks[23]=boss_ddz_hook;g_recomp_hooks[24]=carry_collision_hook;g_recomp_hooks[25]=boss_cnz_hook;g_recomp_hooks[26]=resume_pause_hook;
}
extern "C" int s3k_coop_active(){return s.phase!=OFF;}
extern "C" int s3k_coop_event(const union SDL_Event *event) {
    if(!event||(s.phase!=PLAY&&s.phase!=FAILED&&s.phase!=RECONNECTING))return 0;
    if(team_event(event))return 1;
    if((event->type==SDL_CONTROLLERBUTTONDOWN&&event->cbutton.button==SDL_CONTROLLER_BUTTON_BACK)||
       (event->type==SDL_KEYDOWN&&!event->key.repeat&&event->key.keysym.sym==SDLK_BACKSPACE)) {
        toggle_level_map();return 1;
    }
    return 0;
}
extern "C" int s3k_coop_ui_visible(){return r8(0xf600)==0x4c||s.phase!=OFF;}
extern "C" void s3k_coop_poll() {
    progress_poll();
    unsigned now=SDL_GetTicks();
    if(s.listener!=BAD_SOCKET) {
        CoopSocket fd=accept(s.listener,nullptr,nullptr);
        if(fd!=BAD_SOCKET) {
            unsigned i=1;while(i<MAX_CHANNELS&&channels[i].fd!=BAD_SOCKET)i++;
            if(i==MAX_CHANNELS)close_socket(fd);
            else if(!nonblocking(fd)){close_socket(fd);fail("Could not configure player socket.");}
            else {channels[i].fd=fd;connected(i);}
        } else if(!would_block())fail("Could not accept the incoming connection.");
    }
    if(s.phase==RECONNECTING&&channels[0].fd==BAD_SOCKET&&!s.connecting_socket&&int32_t(now-s.retry_at)>=0)reconnect_now();
    if(s.phase==CONNECTING||s.connecting_socket) {
        auto fd=channels[0].fd;
        fd_set writes,errors;FD_ZERO(&writes);FD_ZERO(&errors);FD_SET(fd,&writes);FD_SET(fd,&errors);timeval timeout{};
        int result=select(int(fd)+1,nullptr,&writes,&errors,&timeout);
        if(result<0){if(s.reconnecting)disconnect_channel(0);else fail("Connection check failed.");}
        else if(result>0) {
            int error=0;
#ifdef _WIN32
            int len=sizeof(error);
#else
            socklen_t len=sizeof(error);
#endif
            if(getsockopt(fd,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&len)||error){if(s.reconnecting)disconnect_channel(0);else fail("Could not connect. Check the address, port and host firewall.");}
            else {s.connecting_socket=false;connected(0);}
        } else if(int32_t(now-s.connect_at)>10000){if(s.reconnecting)disconnect_channel(0);else fail("Connection timed out. Check the address and port.");}
    }
    if(s.phase==CONNECTING||s.connecting_socket||s.phase==FAILED)return;
    for(unsigned index=0;index<MAX_CHANNELS;index++) {
        auto &c=channels[index];if(c.fd==BAD_SOCKET)continue;
        flush(index);
        // Bounded work per channel prevents one peer starving input/rendering.
        for(unsigned budget=0;budget<4&&c.fd!=BAD_SOCKET;budget++) {
            int n=recv(c.fd,reinterpret_cast<char*>(c.rx.data()+c.rx_size),int(CAPACITY-c.rx_size),0);
            if(n<0&&would_block())break;
            if(n<=0){disconnect_channel(index);break;}
            c.rx_size+=n;c.last_recv=now;
            while(c.rx_size>=8) {
                auto *p=c.rx.data();unsigned length=get32(p+4);
                if(get16(p)!=0x5333||p[2]!=VERSION||length>MAX_PACKET){if(s.host&&in_game())disconnect_channel(index);else fail("Incompatible or invalid multiplayer packet.");return;}
                if(c.rx_size<length+8)break;
                if(!handle(index,p[3],p+8,length)){
                    fprintf(stderr,"[coop] rejected type=%u channel=%u length=%u first=%u second=%u third=%u waiting=%d stage=%u\n",unsigned(p[3]),index,length,length>=4?get32(p+8):0,length>=8?get32(p+12):0,length>=12?get32(p+16):0,s.waiting,s.barrier_stage);
                    if(s.host&&in_game())disconnect_channel(index);else fail("Session mismatch. All players need the same build and ROM.");return;
                }
                if(s.phase==FAILED)return;if(c.fd==BAD_SOCKET)break;
                s.received++;c.rx_size-=length+8;memmove(c.rx.data(),c.rx.data()+length+8,c.rx_size);
            }
        }
        if(s.phase==FAILED)return;
        if(c.fd==BAD_SOCKET)continue;
        if(int32_t(now-c.last_recv)>15000){disconnect_channel(index);continue;}
        if(int32_t(now-c.last_send)>1000)send_packet(index,PING,nullptr,0);
    }
}
extern "C" uint16_t s3k_coop_pad(int port,uint16_t buttons) {
    if(s.phase==OFF) {
        if(lobby_open&&r8(0xf600)==0x4c) return 0;
        return buttons;
    }
    if(port) return 0;
    buttons&=~0x0800u;
    buttons=team_pad(buttons);
    if(level_map.open){map_input(buttons);return 0;}
    if(palette_editor)return 0;
    if(s.start_pending) return (s.start_frames%16)<4?0x80:0;
    if(s.phase!=PLAY||s.waiting) return 0;
    return buttons;
}
extern "C" void s3k_coop_frame(uint64_t frame) {
    if(!in_game()) return;
    s.frames++;
    if(s.phase==RECONNECTING)return;
    emerald_frame();team_frame(frame);
#if OA_TEST_CONTROL
    test_fill_check("frame",0);
#endif
    sync_rings();
    if(s.hud_pending) {
        // Append only the native life-icon PLC entry to the native queue.
        // Wait for a free slot rather than evicting level graphics.
        unsigned plc=s.character==2?5:((r8(0xffd8)&0x80)?7:0x52);
        unsigned table=0x09238c;
        unsigned entry=table+int16_t(get16(g_rom+table+plc*2))+2;
        for(unsigned q=0xf680;q<0xf6e0;q+=6) if(!r32(q)) {
            w32(q,get32(g_rom+entry));w16(q+4,get16(g_rom+entry+4));
            w8(0xfe1c,1);s.hud_pending=false;break;
        }
    }
    if(s.start_pending) {
        w8(0xef4b,0);w16(0xef4c,s.shared_intro?0:s.character+1);
        if(r8(0xf600)!=0x4c) s.start_pending=false;
        else if(++s.start_frames>600) fail("Game did not leave data select. Return to data select and retry.");
    }
    if(s.phase==INTRO&&s.hit&&r8(0xf711)&&!r8(0xf7ca)) {
        // Native Tails AI descends when Sonic is hit. Wait until he has landed;
        // do not replace the flight with teleportation or a timer.
        bool tails_landed=!has_character(1)||(r32(0xb04a)&&!(r8(0xb074)&2)&&!r8(0xb078));
        if(tails_landed&&(!has_character(2)||s.knux_captured)) s.intro_finished=true;
        if(s.intro_finished) {
            w16(0xf602,0);w16(0xf606,0);
            if(barrier(0xffffffffu)) handoff();
        }
    }
    if(s.phase==PLAY&&!s.start_pending&&r8(0xf600)==0x4c) fail("Player returned to data select. End this session to reconnect.");
    if(s.phase!=FAILED&&s.hello&&s.local_id>=0) {
        std::array<unsigned char,STATE_BYTES> state;snapshot(state.data());
        std::array<unsigned char,MAX_PACKET> p;unsigned length=encode_state(unsigned(s.local_id),state.data(),p.data());
        if(s.host) {
            for(unsigned i=1;i<MAX_CHANNELS;i++)if(channels[i].hello&&channels[i].tx_size==0)send_packet(i,STATE,p.data(),length);
        } else if(channels[0].tx_size==0)send_packet(0,STATE,p.data(),length);
    }
}

extern "C" void s3k_coop_draw_player(SDL_Renderer *renderer) {
    emerald_textures(renderer);team_video_textures(renderer);update_level_map(renderer);
    const double render_time=double(SDL_GetTicks64());
    // Keep the map's other players animated even while its viewer is on a
    // private special-stage visit and ordinary world drawing is suppressed.
    if(level_map.open)for(unsigned who=0;who<MAX_PLAYERS;who++) {
        auto &v=s.players[who];auto &p=v.state;
        if(int(who)!=s.local_id&&v.present&&v.connected&&v.state_valid&&p[11]&&get32(p.data())==level_map.epoch&&get16(p.data()+4)==level_map.stage&&state_layout(p.data())==level_map.world_stage)
            {auto shown=player_view(who,render_time);auto data=shown.frame?shown.frame->image.data():p.data();upload_player_texture(renderer,remote_textures[who],data,carry_sprite_flip(who,data,render_time));}
    }
    remote_drawn=0;
    if(s.phase!=PLAY||s.start_pending||r8(0xf600)!=0x0c||title_card_active()||side_trip()){team_capture(renderer);return;}
    for(unsigned who=0;who<MAX_PLAYERS;who++) {
        auto &v=s.players[who];auto &p=v.state;
        if(int(who)==s.local_id||!v.present||!v.connected||!v.state_valid||!p[11]||get32(p.data())!=s.epoch||get16(p.data()+4)!=r16(0xee4e)||state_layout(p.data())!=r16(0xfe10))continue;
        auto &texture=remote_textures[who];
        auto shown=player_view(who,render_time);
        if(!level_map.open){auto data=shown.frame?shown.frame->image.data():p.data();upload_player_texture(renderer,texture,data,carry_sprite_flip(who,data,render_time));}if(!texture)continue;
        SDL_Rect dest{int(std::lround(shown.x))-online_view_left-int(SIDE/2),int(std::lround(adventure::wrapped_delta(shown.y-online_view_top,float(local_y_period()))))-int(SIDE/2),int(SIDE),int(SIDE)};
        if(SDL_RenderCopy(renderer,texture,nullptr,&dest)==0)remote_drawn++;
        team_nameplate(renderer,who,dest.x+SIDE/2,dest.y+SIDE/2);
#if OA_TEST_CONTROL
        int carrier=team.carrier[who];auto anchor=carrier>=0?player_view(unsigned(carrier),render_time):PlayerMotion::View{};
        motion_draws.push_back({s.frames,who,shown.x,shown.y,anchor.x,anchor.y,carrier});
        while(motion_draws.size()>2048)motion_draws.pop_front();
#endif
    }
    if(s.local_id>=0)team_nameplate(renderer,unsigned(s.local_id),int(r16(0xb010))-online_view_left,int(adventure::wrapped_delta(float(int(r16(0xb014))-online_view_top),float(local_y_period()))));
#if OA_TEST_CONTROL
    if(s.local_id>=0){int carrier=team.carrier[s.local_id];auto anchor=carrier>=0?player_view(unsigned(carrier),render_time):PlayerMotion::View{};
        motion_draws.push_back({s.frames,unsigned(s.local_id),float(r16(0xb010)),float(r16(0xb014)),anchor.x,anchor.y,carrier});
        while(motion_draws.size()>2048)motion_draws.pop_front();}
#endif
    team_capture(renderer);
}
extern "C" void s3k_coop_draw_ui() {
    bool menu=r8(0xf600)==0x4c;
    if(s.phase==OFF&&!menu){lobby_open=false;return;}
    // Keyboard/gamepad navigation belongs to menus a player opened on purpose:
    // the lobby, the palette editor, the team menu, or the spectator view while
    // waiting. During normal play the overlay never takes focus, so jump and
    // the arrow keys always reach the game instead of activating a button.
    {auto &io=ImGui::GetIO();const ImGuiConfigFlags navigation=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
     if(!in_game()||palette_editor||team.open||(team.watch>=0&&s.waiting))io.ConfigFlags|=navigation;
     else{io.ConfigFlags&=~navigation;ImGui::SetWindowFocus(nullptr);}}
    ImGuiViewport *vp=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x+vp->WorkSize.x-12,vp->WorkPos.y+12),ImGuiCond_Always,ImVec2(1,0));
    ImGui::SetNextWindowBgAlpha(.94f);
    ImGui::SetNextWindowSize(ImVec2(in_game()?280.0f:350.0f,0),ImGuiCond_Always);
    // In play the status panel is mouse-only: it never takes focus or navigation.
    ImGuiWindowFlags panel=ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings;
    if(in_game())panel|=ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoBringToFrontOnFocus;
    if(ImGui::Begin("Online multiplayer",nullptr,panel)) {
        if(s.phase==OFF&&!lobby_open) {
            if(ImGui::Button("Connect with players"))lobby_open=true;
        } else if(!in_game()) {
            ImGui::TextWrapped("Up to 8 players, each with their own camera. Everyone clears the act before moving on.");
            ImGui::TextUnformatted("Your name");ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-60);if(ImGui::InputText("##player-name",local_name,sizeof(local_name),ImGuiInputTextFlags_EnterReturnsTrue))send_name();ImGui::SameLine();if(ImGui::Button("Apply"))send_name();
            int c=s.character;if(ImGui::Combo("Character",&c,characters,3))choose(c);
            if(ImGui::Button("Customize palette"))palette_editor=!palette_editor;
            if(s.phase==OFF) {
                ImGui::InputText("Host IP",peer_address,sizeof(peer_address));ImGui::InputInt("Port",&port_number);
                if(ticket.room&&ImGui::Button("Rejoin previous session"))resume_ticket();
                if(ImGui::Button("Host game"))begin(true,nullptr,port_number);
                ImGui::SameLine();if(ImGui::Button("Join game"))begin(false,peer_address,port_number);
                ImGui::TextWrapped("Internet: host forwards this port for both TCP and UDP, or everyone uses the same VPN. LAN: use the host's local IP.");
                if(ImGui::Button("Back to solo"))lobby_open=false;
            } else if(s.phase==LISTENING)ImGui::Text("Waiting for players on port %d...",port_number);
            else if(s.phase==CONNECTING||!s.hello||s.local_id<0)ImGui::TextUnformatted("Connecting...");
            else {
                ImGui::Text("Lobby: %u / %u players",player_count(),MAX_PLAYERS);
                for(unsigned i=0;i<MAX_PLAYERS;i++)if(s.players[i].present)
                    ImGui::Text("%u. %s (%s)%s %s",i+1,player_name(i),characters[s.players[i].character],int(i)==s.local_id?" (you)":"",s.players[i].ready?"Ready":"Not ready");
                if(ImGui::Button(s.players[s.local_id].ready?"Not ready":"Ready"))ready(!s.players[s.local_id].ready);
                if(s.host){ImGui::SameLine();ImGui::BeginDisabled(!all_ready());if(ImGui::Button("Start together"))start();ImGui::EndDisabled();}
                else ImGui::TextWrapped("The host starts when everyone is ready.");
                ImGui::TextWrapped("Starts a new No Save adventure in Angel Island.");
            }
            if(s.phase!=OFF&&ImGui::Button("Disconnect"))leave(false);
        } else if(s.phase==FAILED) {
            ImGui::TextWrapped("%s",s.error);
            if(ImGui::Button("Return to data select")){leave(true);lobby_open=true;}
        } else {
            if(s.phase==RECONNECTING)ImGui::TextWrapped("Reconnecting to the host. Your place is reserved...");
            else if(s.phase==INTRO)ImGui::TextWrapped(s.waiting?"Waiting for everyone to finish the opening...":"Watching the opening together...");
            else if(s.waiting){ImGui::Text(shared_phase_waiting?"Next area: %u / %u":"Act cleared: %u / %u",arrived_count(),player_count());ImGui::TextWrapped(shared_phase_waiting?"Waiting for everyone to reach the next area...":peer_side_trip()?"Some players are in special stages. Waiting for everyone to clear the act...":"Waiting for everyone to clear the act...");}
            else if(side_trip())ImGui::TextWrapped(peer_arrived()?"Others cleared the act. Return and clear it to continue together.":"Special stage. The other players can keep playing the act.");
            else if(peer_arrived())ImGui::TextWrapped(shared_fire_epoch==s.epoch?"The fire encounter is complete. Join the others to enter the next area.":"Other players cleared the act and are waiting.");
            else if(peer_side_trip())ImGui::TextWrapped("A player is in a special stage. Keep playing!");
            else ImGui::Text("%s | %u players",characters[s.character],player_count());
            if(ImGui::Button("Map (Select)"))toggle_level_map();
            ImGui::SameLine();if(ImGui::Button("Palette")){palette_editor=!palette_editor;level_map.open=false;}
            if(ImGui::Button("Team (F2 / Y)"))team_toggle();
            if(ImGui::Button("Leave session")){leave(true);lobby_open=true;}
        }
    }
    ImGui::End();
    draw_level_map();draw_palette_editor();draw_team_ui();
    // Newest notice anchors at the bottom right; simultaneous awards stack up.
    float bottom=vp->WorkPos.y+vp->WorkSize.y-12;
    unsigned now=SDL_GetTicks();
    for(unsigned i=0;i<(std::min)(s.notice_next,MAX_PLAYERS);i++) {
        auto &notice=s.notices[(s.notice_next-1-i)%MAX_PLAYERS];
        if(!notice.active||int32_t(now-notice.started)>=6000)continue;
        char title[32];snprintf(title,sizeof(title),"##emerald_notice_%u",i);
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x+vp->WorkSize.x-12,bottom),ImGuiCond_Always,ImVec2(1,1));
        ImGui::SetNextWindowSize(ImVec2(300,0),ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(.95f);
        ImGui::Begin(title,nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|
                     ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1.0f,.88f,.30f,1.0f));
        ImGui::TextWrapped("%s got a chaos emerald!",player_name(notice.player));
        ImGui::PopStyleColor();
        bottom-=ImGui::GetWindowHeight()+8;
        ImGui::End();
    }
}
extern "C" void s3k_coop_shutdown() {
    save_identity();save_palettes();machine_set_sprite_tint(nullptr);
    if(level_map.texture){SDL_DestroyTexture(level_map.texture);level_map.texture=nullptr;}
    if(level_map.local_texture){SDL_DestroyTexture(level_map.local_texture);level_map.local_texture=nullptr;}
    leave(false);for(auto &texture:remote_textures)if(texture){SDL_DestroyTexture(texture);texture=nullptr;}
#ifdef _WIN32
    if(socket_ready){WSACleanup();socket_ready=false;}
#endif
}
extern "C" void s3k_coop_after_ui(SDL_Renderer *renderer) {
    if(!capture_path[0]) return;
    int logical_w=0,logical_h=0;SDL_RenderGetLogicalSize(renderer,&logical_w,&logical_h);
    SDL_RenderSetLogicalSize(renderer,0,0);
    int w=0,h=0;SDL_GetRendererOutputSize(renderer,&w,&h);
    if(w>0&&h>0&&w<=8192&&h<=8192) {
        auto *pixels=static_cast<uint32_t*>(malloc(size_t(w)*h*4));
        if(pixels) {
            if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,pixels,w*4)==0)
                png_write_argb(capture_path,pixels,w,h,w);
            free(pixels);
        }
    }
    SDL_RenderSetLogicalSize(renderer,logical_w,logical_h);
    capture_path[0]=0;
}
// The existing local debug server drives the same public actions as the UI.
// It never bypasses the ready handshake or directly satisfies a barrier.
extern "C" void s3k_coop_debug(int id,const char *json) {
    char action[32]{},ip[64]="127.0.0.1";int value=0;
    auto string_value=[&](const char *key,char *out,unsigned cap) {
        char needle[48];snprintf(needle,sizeof(needle),"\"%s\"",key);const char *p=strstr(json,needle);if(!p)return;
        p=strchr(p+strlen(needle),':');if(!p)return;p=strchr(p,'"');if(!p)return;p++;
        unsigned n=0;while(p[n]&&p[n]!='"'&&n+1<cap) {out[n]=p[n];n++;}out[n]=0;
    };
    string_value("action",action,sizeof(action));string_value("host",ip,sizeof(ip));
    const char *v=strstr(json,"\"value\"");if(v&&(v=strchr(v,':'))) value=atoi(v+1);
#if OA_TEST_CONTROL
    if(!strcmp(action,"test_contact")) {
        auto number=[&](const char *name,int fallback){char needle[40];snprintf(needle,sizeof needle,"\"%s\"",name);const char *p=strstr(json,needle);return p&&(p=strchr(p,':'))?atoi(p+1):fallback;};
        char key[32]{};string_value("key",key,sizeof key);
        unsigned obj=key[0]?local_entity_address(strtoull(key,nullptr,10)):unsigned(number("object",0));
        if(!native_slot(obj)||!r32(obj)||(number("expected",0)&&r32(obj)!=unsigned(number("expected",0)))){cmd_send_err(id,"Contact target is not loaded");return;}
        bool attack=number("attack",1)!=0,ground=number("ground",0)!=0;int vx=number("vx",0),vy=number("vy",ground?0:0x500);
        w8(0xb005,2);w32(0xb010,uint32_t(uint16_t(r16(obj+16)+number("dx",0)))<<16);w32(0xb014,uint32_t(uint16_t(r16(obj+20)+number("dy",-20)))<<16);
        w16(0xb018,uint16_t(vx));w16(0xb01a,uint16_t(vy));w16(0xb01c,uint16_t(vx));
        w8(0xb01e,attack?14:r8(0xb044));w8(0xb01f,attack?7:r8(0xb045));w8(0xb020,attack?2:0);w8(0xb021,0xff);w8(0xb02a,(attack?4:0)|(ground?0:2));w8(0xb02b,0);
        w8(0xb02e,uint8_t(number("control",attack?1:0)));w8(0xb02f,0);w8(0xb034,attack?0:180);w8(0xb03d,0);w8(0xb040,attack?1:0);w16(0xfe20,50);
        char out[100];snprintf(out,sizeof out,"{\"id\":%d,\"ok\":true,\"object\":%u}",id,obj);cmd_send_response(out);return;
    }
    if(!strcmp(action,"test_start_level")) {
        // Make the native StartNewLevel call a level's own ending code makes
        // (e.g. the escape ship's loc_803D6), at the next object-loop start.
        if(value<0||value>0x1701){cmd_send_err(id,"Invalid stage");return;}
        test_start_level=unsigned(value);char out[64];snprintf(out,sizeof out,"{\"id\":%d,\"ok\":true}",id);cmd_send_response(out);return;
    }
    if(!strcmp(action,"test_warp")) {
        const char *v=strstr(json,"\"y\"");unsigned y=v&&(v=strchr(v,':'))?unsigned(atoi(v+1)):0;
        bool ok=warp_local(r16(0xee4e),unsigned(value),y,true);
        char out[100];snprintf(out,sizeof out,"{\"id\":%d,\"ok\":%s}",id,ok?"true":"false");cmd_send_response(out);return;
    }
    if(!strcmp(action,"test_entities")) {
        std::string out="{\"id\":"+std::to_string(id)+",\"ok\":true,\"entities\":[";bool first=true;
        for(const auto &entry:shared_world.entities()){
            const auto &e=entry.second;if(e.key.epoch!=s.epoch||e.key.stage!=shared_stage_id())continue;
            char row[300];snprintf(row,sizeof row,"%s{\"key\":\"%llu\",\"initial\":%u,\"code\":%u,\"obj\":%u,\"hp\":%u,\"removed\":%u,\"owner\":%d,\"x\":%u,\"y\":%u}",first?"":",",(unsigned long long)e.key.id,get32(e.image.initial.data()),get32(e.image.bytes.data()),local_entity_address(e.key.id),e.image.bytes[0x29],e.removed,e.owner,get16(e.image.bytes.data()+16),get16(e.image.bytes.data()+20));first=false;out+=row;
        }
        out+="]}";cmd_send_response(out.c_str());return;
    }
    if(!strcmp(action,"motion")) {
        std::string result="{\"id\":"+std::to_string(id)+",\"ok\":true,\"draws\":[";
        bool first=true;char row[256];for(const auto &d:motion_draws){snprintf(row,sizeof row,"%s[%llu,%u,%.3f,%.3f,%d,%.3f,%.3f]",first?"":",",(unsigned long long)d.frame,d.who,d.x,d.y,d.carrier,d.cx,d.cy);first=false;result+=row;}
        result+="]}";cmd_send_response(result.c_str());motion_draws.clear();return;
    }
#endif
    if(!strcmp(action,"progress")) {
        auto stats=progress.stats();char result[384];
        snprintf(result,sizeof(result),"{\"id\":%d,\"ok\":true,\"generation\":%u,\"tick\":%u,\"confirmed\":%u,\"episodes\":%u,\"replayed\":%llu,\"desyncs\":%u,\"linked\":%d,\"epoch\":%u}",id,progress_generation,stats.tick,stats.confirmed,stats.episodes,(unsigned long long)stats.replayed,stats.desyncs,stats.linked,progress.committed().epoch);
        cmd_send_response(result);return;
    }
    if(!strcmp(action,"palette_sample")) {
        if(value<0||value>15){cmd_send_err(id,"Palette index must be 0..15");return;}
        bool special=r8(0xf600)==0x34;
        unsigned index=(special?(s.character==1?1:0):((r16(0xb00a)>>13)&3))*16+unsigned(value);
        unsigned native=sonikk_cram_argb(index),painted=palette_color(index,native,special),peer=0;
        for(unsigned i=0;i<MAX_PLAYERS;i++)if(int(i)!=s.local_id&&s.players[i].present&&s.players[i].state_valid) {
            peer=get32(s.players[i].state.data()+STATE_HEADER+unsigned(value)*4);break;
        }
        char result[160];snprintf(result,sizeof(result),"{\"id\":%d,\"ok\":true,\"native\":%u,\"painted\":%u,\"peer\":%u}",
            id,native&0xffffff,painted&0xffffff,peer&0xffffff);cmd_send_response(result);return;
    }
    if(!strcmp(action,"host")) begin(true,nullptr,value?value:7777);
    else if(!strcmp(action,"join")) begin(false,ip,value?value:7777);
    else if(!strcmp(action,"character")) choose(value);
    else if(!strcmp(action,"ready")) ready(value!=0);
    else if(!strcmp(action,"start")) start();
    else if(!strcmp(action,"leave")) leave(true);
    else if(!strcmp(action,"palette")){if(value<0||unsigned(value)>0xffffff){cmd_send_err(id,"Palette RGB must be 0..16777215");return;}auto &p=palette_styles[s.character];p.mask|=1;rgb_set(p.body,unsigned(value));palette_changed();}
    else if(!strcmp(action,"palette_reset")){palette_styles[s.character].mask=0;palette_changed();}
    else if(!strcmp(action,"palette_editor"))palette_editor=!palette_editor;
    else if(!strcmp(action,"map")){SDL_Event event{};event.type=SDL_CONTROLLERBUTTONDOWN;event.cbutton.button=SDL_CONTROLLER_BUTTON_BACK;s3k_coop_event(&event);}
    else if(!strcmp(action,"capture")) string_value("path",capture_path,sizeof(capture_path));
    else if(team_debug(id,action,json))return;
    else if(action[0]&&strcmp(action,"status")) { cmd_send_err(id,"Unknown co-op action");return; }

    unsigned visible=0,remote_count=0,tx=0,rx=0;
    int first_peer=-1;unsigned peer_body=0;
    for(unsigned i=0;i<MAX_PLAYERS;i++) {
        tx+=channels[i].tx_size;rx+=channels[i].rx_size;
        if(int(i)==s.local_id||!s.players[i].present)continue;
        if(first_peer<0){first_peer=s.players[i].character;peer_body=get32(s.players[i].state.data()+STATE_HEADER+(first_peer==1?8:3)*4)&0xffffff;}
        unsigned count=0;for(unsigned j=SPRITE_OFFSET;j<STATE_BYTES;j++)if(s.players[i].state[j])count++;
        visible+=count;if(count&&s.players[i].state_valid)remote_count++;
    }
    char out[2048];snprintf(out,sizeof(out),
        "{\"id\":%d,\"ok\":true,\"phase\":%d,\"host\":%d,\"hello\":%d,\"local_id\":%d,\"players\":%u,\"character\":%d,\"peer_character\":%d,\"ready\":%d,\"peer_ready\":%d,\"epoch\":%u,\"waiting\":%d,\"arrived\":%u,\"peer_arrived\":%d,\"hit\":%d,\"knux_captured\":%d,\"knux_x\":%u,\"knux_y\":%u,\"intro_finished\":%d,\"handoff\":%d,\"title_card\":%d,\"remote_drawn\":%u,\"side_trip\":%d,\"peer_side_trip\":%d,\"used_rings\":%u,\"sonic_tails_route\":%d,\"route_hits\":%u,\"map_open\":%d,\"map_width\":%u,\"map_height\":%u,\"map_solid\":%u,\"map_markers\":%u,\"map_art_pixels\":%u,\"map_sprite_markers\":%u,\"palette_mask\":%u,\"palette_body\":%u,\"peer_body\":%u,\"palette_tint_hits\":%u,\"special_palette_hits\":%u,\"map_zoom\":%.1f,\"emerald_notices\":%u,\"emerald_popups\":%u,\"last_emerald_player\":%d,\"remote_pixels\":%u,\"remote_count\":%u,\"sent\":%u,\"received\":%u,\"tx_bytes\":%u,\"rx_bytes\":%u,\"error\":\"%s\"}",
        id,int(s.phase),s.host,s.hello,s.local_id,player_count(),s.character,first_peer,s.local_id>=0&&s.players[s.local_id].ready,all_ready(),s.epoch,s.waiting,arrived_count(),peer_arrived(),s.hit,s.knux_captured,s.knux_x,s.knux_y,s.intro_finished,s.handoff,title_card_active(),remote_drawn,side_trip(),peer_side_trip(),r8(0xee4e)<14?s.used_rings[r8(0xee4e)]:0,in_game(),s.route_hits,level_map.open,level_map.width,level_map.height,level_map.solid,level_map.markers,level_map.art_pixels,level_map.sprite_markers,palette_styles[s.character].mask,rgb_value(palette_styles[s.character].body),peer_body,palette_tint_hits,special_palette_hits,level_map.zoom,s.notice_count,active_notices(),s.last_emerald_player,visible,remote_count,s.sent,s.received,tx,rx,s.error);
    cmd_send_response(out);
}
