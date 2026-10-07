void shared_reset();void shared_pre();void shared_frame();void shared_snapshot(unsigned);bool shared_handle(unsigned,const unsigned char*,unsigned);
bool shared_debug(int,const char*);
uint64_t shared_entity_identity(unsigned);bool shared_entity_owned(unsigned);
unsigned shared_stage_id();bool valid_shared_stage(unsigned);
void shared_commit_boss_damage(unsigned,uint64_t,unsigned,unsigned);
bool world_pose_matches(unsigned who,unsigned epoch,unsigned stage){
    if(who>=MAX_PLAYERS)return false;const auto &v=s.players[who];
    return v.pose_valid&&v.pose.epoch==epoch&&v.pose.stage==(stage&65535)&&v.pose.actual==(stage>>16)&&!(v.pose.flags&1);
}
// Shared native triggers and boss health. Never exchange RAM addresses or player progress.
struct WorldArea {
    unsigned epoch=~0u,stage=0,revision=0;
    std::array<unsigned char,16> value{},latched{},momentary{},known{},buttons{};
    std::array<std::array<unsigned char,16>,MAX_PLAYERS> pressure{};
};
struct SharedBoss {
    uint64_t key=0;
    unsigned epoch=0,stage=0,hp=0,kind=0,revision=0,hit_at=0;
};
struct LocalBoss {uint64_t key=0;unsigned revision=0,code=0;bool replay=false,pending=false;unsigned pending_hp=0;};
struct WorldState {
    std::vector<WorldArea> areas;
    std::vector<SharedBoss> bosses;
    std::array<LocalBoss,93> local{};
    std::array<unsigned char,16> pressure{},latched{},known{},momentary{},applied{};
    std::array<unsigned,MAX_PLAYERS> sequences{};
    unsigned stage=~0u,epoch=~0u,sequence=0,sent_at=0,hits=0,buttons=0;
    bool loaded=false;
} world;
void world_reset(){world=WorldState{};shared_reset();}
// Restart_level_flag means this frame finishes on a layout that is about to be
// replaced. StartNewLevel has already written the next zone/act, so stage-
// scoped reports from the rest of the frame would file the old level's
// objects, terrain and phase bits (e.g. Death Egg act 2's FAB8) under the new
// one. Shared world state resumes once the new layout is running.
bool world_play(){return s.phase==PLAY&&!s.start_pending&&!s.restore_pending&&!side_trip()&&r8(0xf600)==0x0c&&r8(0xf711)&&!r16(0xfe02)&&!title_card_active();}
WorldArea *world_area(unsigned epoch,unsigned stage,bool create=true) {
    for(auto &a:world.areas)if(a.epoch==epoch&&a.stage==stage)return &a;
    if(!create||world.areas.size()>=64)return nullptr;
    world.areas.push_back(WorldArea{});auto &a=world.areas.back();a.epoch=epoch;a.stage=stage;return &a;
}
SharedBoss *world_boss(unsigned epoch,unsigned stage,uint64_t key,bool create=false) {
    for(auto &b:world.bosses)if(b.epoch==epoch&&b.stage==stage&&b.key==key)return &b;
    if(!create||!key||world.bosses.size()>=128)return nullptr;
    world.bosses.push_back(SharedBoss{});auto &b=world.bosses.back();b.epoch=epoch;b.stage=stage;b.key=key;return &b;
}
void world_send_area(const WorldArea &a,int channel=-1) {
    unsigned char p[64]{};put32(p,1);put32(p+4,a.epoch);put32(p+8,a.stage);put32(p+12,a.revision);
    memcpy(p+16,a.value.data(),16);memcpy(p+32,a.known.data(),16);memcpy(p+48,a.momentary.data(),16);
    if(channel<0)broadcast(WORLD,p,sizeof(p));else send_packet(unsigned(channel),WORLD,p,sizeof(p));
}
void world_send_boss(const SharedBoss &b,int channel=-1) {
    unsigned char p[36]{};put32(p,3);put32(p+4,b.epoch);put32(p+8,b.stage);put64(p+12,b.key);
    put32(p+20,b.hp);put32(p+24,b.kind);put32(p+28,b.revision);
    if(channel<0)broadcast(WORLD,p,sizeof(p));else send_packet(unsigned(channel),WORLD,p,sizeof(p));
}
void world_send_snapshot(unsigned channel) {
    shared_snapshot(channel);
    for(auto &a:world.areas)world_send_area(a,int(channel));
    for(auto &b:world.bosses)world_send_boss(b,int(channel));
}
void world_merge(WorldArea &a) {
    bool changed=false;
    for(unsigned i=0;i<16;i++) {
        unsigned pressure=0;for(unsigned p=0;p<MAX_PLAYERS;p++)if(active_pose(p)&&world_pose_matches(p,a.epoch,a.stage)&&SDL_GetTicks()-s.players[p].pose_time<1500)pressure|=a.pressure[p][i];
        unsigned value=(a.value[i]&~a.known[i])|a.latched[i]|(pressure&a.momentary[i]);
        if(a.value[i]!=value){a.value[i]=uint8_t(value);changed=true;}
    }
    if(changed){a.revision++;world_send_area(a);}
}
uint64_t boss_key(unsigned obj) {
    unsigned map=r32(obj+0xc);
    if(obj<0xb0de||obj>0xca98||(obj-0xb000)%0x4a||!r32(obj)||map<0x100||map>=sizeof(g_rom))return 0;
    // Each client runs its own Hidden Palace Knuckles (see hpz_story_code);
    // their health is one ledger. 642C6 is the hit-replay entry.
    {unsigned code=r32(obj);if(r16(0xfe10)==0x1601&&((code>=0x63cfc&&code<0x63de2)||code==0x642c6))return 0x2000000000001601ull;}
    // Dynamic VRAM tiles differ between clients that loaded different objects.
    // Health and the physical boss must use the very same world identity.
    return shared_entity_identity(obj);
}
void world_select() {
    unsigned stage=shared_stage_id();
    if(world.stage==stage&&world.epoch==s.epoch)return;
    world.stage=stage;world.epoch=s.epoch;world.local.fill(LocalBoss{});
    world.pressure.fill(0);world.latched.fill(0);world.known.fill(0);world.momentary.fill(0);world.applied.fill(0);world.loaded=false;
}
int switch_hook() {
    if(!world_play())return 0;world_select();unsigned obj=g_cpu.A[0]&65535;
    if(obj<0xb0de||obj>0xca98)return 0;
    unsigned subtype=r8(obj+0x2c),index=subtype&15,mask=(subtype&0x40)?0x80:1;
    world.known[index]|=mask;if(!(subtype&0x10))world.momentary[index]|=mask;
    // Native collision has already updated this button's frame. Never infer a
    // local press from the merged trigger array, which includes remote players.
    bool held=r8(obj+0x22)==1&&(r8(obj+4)&0x80)&&abs(int(r16(obj+0x10))-int(r16(0xb010)))<80&&abs(int(r16(obj+0x14))-int(r16(0xb014)))<80;
    if(held){if(subtype&0x10)world.latched[index]|=mask;else world.pressure[index]|=mask;world.buttons++;}
    auto *a=world_area(s.epoch,world.stage,false);
    if(a){if(a->value[index]&mask)w8(obj+0x22,1);w8(0xf7e0+index,(r8(0xf7e0+index)&~a->known[index])|(a->value[index]&a->known[index]));}
    return 0;
}
void world_boss_hit(unsigned obj,unsigned kind,bool before_instruction=false) {
    if(!world_play())return;world_select();uint64_t key=boss_key(obj);if(!key)return;
    auto &local=world.local[(obj-0xb000)/0x4a];
    if(local.replay){w32(obj,local.code);local.replay=false;return;}
    // The replay entry is never a native routine. Reaching it without a
    // pending local replay means a stale image carried it: not a contact.
    static const unsigned replay_entry[]={0,0x642c6,0x6f9a2,0x79f66,0x7fb9e,0x82de8,0};
    if(kind&&kind<6&&r32(obj)==replay_entry[kind]){if(kind==1)w32(obj,0x63de0);return;}
    // Ordinary badniks have no health counter. The standard hook is reached
    // only through Touch_Enemy's boss path; custom boss sites are audited too.
    unsigned after=(r8(obj+0x29)-(before_instruction?1:0))&255,before=(after+1)&255;
    if(!before&&kind!=5)return;
    if(local.key!=key)local=LocalBoss{key};
    if(!kind){local.pending=true;local.pending_hp=after;}
    sample_pose();if(!s.host){unsigned char pose[80];encode_pose(unsigned(s.local_id),pose);send_packet(0,POSE,pose,sizeof pose);}
    unsigned char p[44]{};put32(p,2);put32(p+4,s.epoch);put32(p+8,shared_stage_id());put64(p+12,key);
    put32(p+20,before);put32(p+24,kind);put32(p+28,++world.sequence);put32(p+32,unsigned(s.local_id));put32(p+36,after);
    local.key=key;world.hits++;
    if(s.host)world_handle(MAX_CHANNELS,p,sizeof(p));else send_packet(0,WORLD,p,sizeof(p));
}
int boss_cnz_hook(){world_boss_hit(g_cpu.A[1]&65535,0,true);return 0;}
int boss_a1_hook(){world_boss_hit(g_cpu.A[1]&65535,0);return 0;}
int boss_hpz_hook(){world_boss_hit(g_cpu.A[0]&65535,1);return 0;}
int boss_fbz_hook(){world_boss_hit(g_cpu.A[0]&65535,2);return 0;}
int boss_lrz_hook(){world_boss_hit(g_cpu.A[0]&65535,3);return 0;}
int boss_dez_hook(){world_boss_hit(g_cpu.A[0]&65535,4);return 0;}
int boss_ddz_hook(){world_boss_hit(g_cpu.A[0]&65535,5);return 0;}
// A Doomsday homing missile damaging the ship's health child (FAA4) writes it
// directly. Missiles fly far from their launcher and keep their own owner, so
// that write landed on a replica (lost) or was replayed every frame by a
// replica missile. Route it through the shared health ledger instead; a
// replica missile's native decrement is neutralised.
int shared_ddz_missile_hit(){
    if(!world_play()||r16(0xfe10)!=0xc00)return 0;
    unsigned missile=g_cpu.A[0]&65535,head=g_cpu.A[1]&65535;
    uint64_t key=shared_entity_identity(missile);
    if(key&&!shared_entity_owned(missile)){w8(head+0x29,(r8(head+0x29)+1)&255);return 0;}
    world_boss_hit(head,6,true);return 0;
}
void world_apply_bosses() {
    for(unsigned obj=0xb0de;obj<=0xca98;obj+=0x4a) {
        uint64_t key=boss_key(obj);if(!key){world.local[(obj-0xb000)/0x4a]=LocalBoss{};continue;}auto *b=world_boss(s.epoch,world.stage,key);if(!b)continue;
        auto &l=world.local[(obj-0xb000)/0x4a];if(l.key!=key)l=LocalBoss{key};
        if(l.replay||(b->kind&&l.revision==b->revision))continue;
        // Wait until the native object has initialized its health. Once dead,
        // do not re-enter the explosion/escape routine on another revision.
        if(r8(obj+0x2a)&0x80){l.revision=b->revision;continue;}
        if(!r8(obj+0x29)&&b->kind!=5)continue;
        unsigned target=b->kind==5?(b->hp?b->hp-1:255):b->hp;
        unsigned current=r8(obj+0x29);l.revision=b->revision;
        if(current==target||(!b->kind&&current<target))continue;
        if(b->kind==6) {
            // Doomsday missile hit (loc_82B62): health down, hit flag set.
            if(current<target)continue;w8(obj+0x29,target);w8(obj+0x28,0xff);
        } else if(b->kind) {
            static const unsigned entry[]={0,0x642c6,0x6f9a2,0x79f66,0x7fb9e,0x82de8};
            l.code=r32(obj);l.replay=true;w8(obj+0x29,(target+1)&255);w32(obj,entry[b->kind]);
            if(b->kind==1){w16(obj+0x18,0x300);w16(obj+0x1a,uint16_t(-0x300));}
        } else {
            if(r8(obj+0x28))w8(obj+0x25,r8(obj+0x28));
            w8(obj+0x28,0);w8(obj+0x1c,0);w8(obj+0x29,target);
            if(!target)w8(obj+0x2a,r8(obj+0x2a)|0x80);
        }
    }
}
void world_pre_frame() {
    if(!world_play()||r16(0xf63a))return;world_select();world.pressure.fill(0);
    auto *a=world_area(s.epoch,world.stage,false);
    if(a)for(unsigned i=0;i<16;i++)w8(0xf7e0+i,(r8(0xf7e0+i)&~a->known[i])|(a->value[i]&a->known[i]));
    for(unsigned i=0;i<16;i++)world.applied[i]=r8(0xf7e0+i);
    shared_pre();world_apply_bosses();
}
void world_frame() {
    shared_frame();
    if(!world_play())return;world_select();
    // Button reports are complete pressure snapshots: leaving/offscreen,
    // dying, and disconnecting release momentary switches; latches persist.
    if(s.host)for(auto &a:world.areas)world_merge(a);
    // Other native levers, timed switches and level mechanisms use the same
    // trigger array. Share their changed bits without echoing merged state.
    unsigned char changed[52]{};put32(changed,4);put32(changed+4,s.epoch);put32(changed+8,world.stage);put32(changed+12,unsigned(s.local_id));bool any=false;
    for(unsigned i=0;i<16;i++){changed[20+i]=(r8(0xf7e0+i)^world.applied[i])&~world.known[i];changed[36+i]=r8(0xf7e0+i);any|=changed[20+i]!=0;}
    if(any){put32(changed+16,++world.sequence);if(s.host)world_handle(MAX_CHANNELS,changed,sizeof(changed));else send_packet(0,WORLD,changed,sizeof(changed));}
    if(SDL_GetTicks()-world.sent_at<50)return;world.sent_at=SDL_GetTicks();
    unsigned char p[84]{};put32(p,0);put32(p+4,s.epoch);put32(p+8,world.stage);put32(p+12,unsigned(s.local_id));put32(p+16,++world.sequence);
    memcpy(p+20,world.known.data(),16);memcpy(p+36,world.momentary.data(),16);memcpy(p+52,world.pressure.data(),16);memcpy(p+68,world.latched.data(),16);
    if(s.host)world_handle(MAX_CHANNELS,p,sizeof(p));else send_packet(0,WORLD,p,sizeof(p));
}
void world_idle_frame() {
    // Special stages and menus do not run the level's object loop. Maintain
    // network bookkeeping there without sampling a suspended level routine.
    if(!world_play())world_frame();
}
bool world_handle(unsigned channel,const unsigned char *p,unsigned n) {
    if(n<12||!in_game())return false;
    unsigned type=get32(p),epoch=get32(p+4),stage=get32(p+8);
    if(epoch>s.epoch+1)return false;
    if(type>=5)return shared_handle(channel,p,n);
    if(!valid_shared_stage(stage))return false;
    if(type==0) {
        if(!s.host||n!=84)return false;unsigned who=get32(p+12),seq=get32(p+16);
        int sender=channel<MAX_CHANNELS?channels[channel].peer:s.local_id;
        if(who>=MAX_PLAYERS||int(who)!=sender)return false;
        if(seq<=world.sequences[who]||epoch!=s.epoch||!world_pose_matches(who,epoch,stage))return true;world.sequences[who]=seq;
        auto *a=world_area(epoch,stage);if(!a)return true;
        for(unsigned i=0;i<16;i++){a->known[i]|=p[20+i];a->buttons[i]|=p[20+i];a->momentary[i]|=p[36+i];a->pressure[who][i]=p[52+i]&p[36+i];a->latched[i]|=p[68+i]&p[20+i]&~p[36+i];}
        world_merge(*a);return true;
    }
    if(type==4) {
        if(!s.host||n!=52)return false;unsigned who=get32(p+12),seq=get32(p+16);int sender=channel<MAX_CHANNELS?channels[channel].peer:s.local_id;
        if(who>=MAX_PLAYERS||int(who)!=sender)return false;
        if(seq<=world.sequences[who]||epoch!=s.epoch||!world_pose_matches(who,epoch,stage))return true;world.sequences[who]=seq;
        auto *a=world_area(epoch,stage);if(!a)return true;
        for(unsigned i=0;i<16;i++){unsigned mask=p[20+i]&~a->buttons[i];a->known[i]|=mask;a->latched[i]=(a->latched[i]&~mask)|(p[36+i]&mask);}
        world_merge(*a);return true;
    }
    if(type==1) {
        if(s.host||n!=64)return false;auto *a=world_area(epoch,stage);if(!a)return true;
        if(get32(p+12)<a->revision)return true;a->revision=get32(p+12);
        memcpy(a->value.data(),p+16,16);memcpy(a->known.data(),p+32,16);memcpy(a->momentary.data(),p+48,16);return true;
    }
    if(type==2) {
        if(!s.host||n!=44||get32(p+24)>6||get32(p+20)>255||get32(p+36)>255)return false;
        unsigned who=get32(p+32),seq=get32(p+28);int sender=channel<MAX_CHANNELS?channels[channel].peer:s.local_id;
        if(who>=MAX_PLAYERS||int(who)!=sender)return false;
        if(seq<=world.sequences[who]||epoch!=s.epoch||!world_pose_matches(who,epoch,stage))return true;world.sequences[who]=seq;
        auto *b=world_boss(epoch,stage,get64(p+12),true);if(!b)return true;
        unsigned kind=get32(p+24),before=get32(p+20),after=get32(p+36);
        if(((before-1)&255)!=after)return false;
        if(!b->revision){b->kind=kind;b->hp=before+(kind==5?1:0);}
        // Simultaneous contacts that saw the same vulnerable health value
        // contend for one native hit, rather than bypassing the shared cooldown.
        if((!kind||kind==1||kind==6)&&before>b->hp){world_send_boss(*b);return true;}
        // Hidden Palace Knuckles is protected between hits only by his native
        // knockback routine (at least ~28 frames). A replica reverted to the
        // authority's image loses that routine and re-detects the same contact.
        if(kind==1&&b->revision&&SDL_GetTicks()-b->hit_at<400){world_send_boss(*b);return true;}
        if(b->kind!=kind)return true;if(b->hp)b->hp--;b->revision++;b->hit_at=SDL_GetTicks();world_send_boss(*b);
        if(!kind)shared_commit_boss_damage(stage,b->key,who,b->hp);
        return true;
    }
    if(type==3) {
        if(s.host||n!=36||get32(p+20)>256||get32(p+24)>6)return false;
        auto *b=world_boss(epoch,stage,get64(p+12),true);if(!b)return true;
        if(get32(p+28)<b->revision)return true;b->hp=get32(p+20);b->kind=get32(p+24);b->revision=get32(p+28);return true;
    }
    return false;
}

bool world_debug(int id,const char *action) {
    if(shared_debug(id,action))return true;
    if(strcmp(action,"world_status"))return false;
    char out[8000];unsigned n=unsigned(snprintf(out,sizeof(out),"{\"id\":%d,\"ok\":true,\"hits\":%u,\"buttons\":%u,\"known\":[",id,world.hits,world.buttons));
    for(unsigned i=0;i<16;i++)n+=unsigned(snprintf(out+n,sizeof(out)-n,"%s%u",i?",":"",world.known[i]));
    n+=unsigned(snprintf(out+n,sizeof(out)-n,"],\"bosses\":["));bool first=true;
    for(auto &b:world.bosses){n+=unsigned(snprintf(out+n,sizeof(out)-n,"%s{\"key\":%llu,\"hp\":%u,\"kind\":%u,\"revision\":%u}",first?"":",",(unsigned long long)b.key,b.hp,b.kind,b.revision));first=false;if(n>7400)break;}
    snprintf(out+n,sizeof(out)-n,"]}");cmd_send_response(out);return true;
}

#include "sonic3k_shared_world.inc"
