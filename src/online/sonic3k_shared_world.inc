// Host-committed entity state. Each loaded entity has one simulation authority;
// other clients retain native collision/presentation, reconciling to that state.
// Layout identities survive different local object allocation orders.
// Internal acts can change without an act-clear (for example AIZ burning).
// Keep both layout identity and the visible act in the world namespace.
unsigned shared_stage_id(){return (r16(0xfe10)<<16)|r16(0xee4e);}
// $1700 is the Death Egg final-boss layout entered by native StartNewLevel.
// $1701 (Hidden Palace emerald shrine) is a private side trip, never shared.
bool valid_shared_stage(unsigned stage){return (stage&65535)<=0x1700&&(stage>>16)<=0x1700;}
#include "sonic3k_shared_terrain.inc"
#include "sonic3k_shared_phase.inc"
#include "sonic3k_shared_scene.inc"
adventure::SharedWorld shared_world;
struct NativeEntity {
    adventure::EntityKey key{};
    unsigned initial_code=0,children=0,revision=0,last_report=0,generation=0;
    bool initialized=false,classified=false,seed_ready=false,personal=false,touching=false;
    adventure::ObjectImage seed{};
};
struct PickupClaim {adventure::EntityKey key{};unsigned object=0,kind=0;bool applied=false;unsigned last_attempt=0,layout=0;};
std::array<NativeEntity,93> native_entities;
std::map<std::pair<unsigned,uint64_t>,unsigned> root_allocations;
std::vector<PickupClaim> pickup_claims;
// Coalesce continuous updates before they reach the reliable socket. A late
// join cannot overflow the channel by queuing the entire level in one frame.
std::array<std::map<adventure::EntityKey,adventure::WorldEntity>,MAX_CHANNELS> entity_outbox;
std::array<adventure::EntityKey,MAX_CHANNELS> entity_cursor;
using ObjectMotion=adventure::RemoteMotion<adventure::ObjectImage>;
struct ObjectTimeline {unsigned generation=0;ObjectMotion motion;};
std::map<adventure::EntityKey,ObjectTimeline> object_timelines;
unsigned shared_stage=~0u,shared_epoch=~0u,shared_sequence=0,shared_claims=0;
unsigned shared_personal_birth=0;
bool applying_pickup=false;
std::map<adventure::EntityKey,bool> local_goal_finished;
std::map<adventure::EntityKey,bool> local_chase_finished;
std::map<adventure::EntityKey,unsigned> local_lrz_scene;
std::map<adventure::EntityKey,bool> local_camera_seen;
// Replica copies that native code deleted here, awaiting the owner's removal.
std::map<adventure::EntityKey,unsigned> local_deleted;
std::map<adventure::EntityKey,bool> local_dez_exit;
std::map<adventure::EntityKey,unsigned> local_dez3_scene;
struct LocalEnding {unsigned state=0,frames=0;};
std::map<uint64_t,unsigned> dez3_finger_bits;
std::map<adventure::EntityKey,LocalEnding> local_dez3_ending;
unsigned ring_timer_object=0,ring_timer_saved=0;
// A replica still runs its native routine for local collision/presentation,
// but its state is reverted to the authority's every frame. One-shot writes
// it makes through a link into an object this client owns (e.g. a Death Egg
// mine exploding decrements its boss's live-mine count at $39) therefore
// repeat each frame and accumulate in the owned object. Replica writes into
// owned linked objects are discarded; their owner performs the real ones.
struct ReplicaGuard {unsigned obj=0,count=0;std::array<unsigned,6> slots{};std::array<std::array<uint8_t,74>,6> bytes{};} replica_guard;
constexpr unsigned ENTITY_REPORT=5,ENTITY_COMMIT=6,ENTITY_RETIRE=7,ENTITY_CLAIM=8;
constexpr unsigned ENTITY_BYTES=456;
bool shared_handle(unsigned,const unsigned char*,unsigned);
bool native_slot(unsigned address){return address>=0xb0de&&address<=0xca98&&(address-0xb000)%0x4a==0;}
bool ssz_entry_presentation(unsigned code){return code>=0x57b6a&&code<0x57e96;}
// Bosses allocate invisible helpers whose whole effect is this client's own
// presentation: Obj_IncLevEndXGradual/Obj_DecLevStartXGradual widen the camera
// towards Camera_stored_max/min_X_pos, Obj_Song_Fade_Transition changes music,
// the Death Egg final boss's loc_810A0 clamps the camera, loc_8642E shakes
// the screen and loc_85E64 fades the palette to white. As children of a shared boss they were created only by its
// authority, so other players kept boss music and stayed walled in the arena.
// Each nearby client now runs a private copy of every live shared helper.
bool local_camera_code(unsigned code){return code==0x84a48||code==0x84a76;}
// Song fades (Obj_Song_Fade_Transition 85B30, Obj_Song_Fade_ToLevelMusic 85B0A) change only
// the local music; every nearby client runs its own copy.
bool local_effect_code(unsigned code){return local_camera_code(code)||code==0x85b30||code==0x85b0a||code==0x810a0||code==0x8642e||code==0x85e64;}
unsigned slot_address(unsigned i){return 0xb000+i*0x4a;}
// Slotted objects (SetUp_ObjAttributesSlotted: Rhinobot, Clamer, Penguinator,
// Butterdroid, Fireworm, the end sign, ...) claim a bit of Slotted_object_bits
// ($FA9A..$FAA1) for a VRAM slot, recording it at ros_bit ($3B)/ros_addr ($3C)
// with status bit 2 set. Replicas are created, overwritten by images and
// removed outside the native paths that release those bits, so bits leaked
// until slotted badniks, or the act's end sign sharing their array, could no
// longer spawn on that client. Each client keeps its own holder fields, and
// the bits are rebuilt every frame from the objects actually holding them.
bool slot_holder(unsigned obj){unsigned a=r16(obj+0x3c);return r32(obj)&&(r8(obj+0x2a)&4)&&a>=0xfa9a&&a<0xfaa2&&r8(obj+0x3b)<8;}
void slot_rebuild(){
    unsigned char bits[8]{};
    for(unsigned i=3;i<93;i++){unsigned obj=slot_address(i);if(slot_holder(obj))bits[r16(obj+0x3c)-0xfa9a]|=uint8_t(1u<<r8(obj+0x3b));}
    for(unsigned k=0;k<8;k++)w8(0xfa9a+k,bits[k]);
}
uint64_t entity_hash(uint64_t a,uint64_t b){uint64_t h=a^0xcbf29ce484222325ull;for(unsigned i=0;i<8;i++){h^=(b>>(i*8))&255;h*=0x100000001b3ull;}return (h&0x3fffffffffffffffull)|0x4000000000000000ull;}
void shared_reset(){shared_phase_reset();terrain_reset();scene_reset();shared_world.clear();local_goal_finished.clear();local_chase_finished.clear();local_lrz_scene.clear();local_camera_seen.clear();local_deleted.clear();local_dez_exit.clear();local_dez3_scene.clear();local_dez3_ending.clear();dez3_finger_bits.clear();root_allocations.clear();native_entities={};pickup_claims.clear();object_timelines.clear();for(auto &q:entity_outbox)q.clear();shared_stage=shared_epoch=~0u;shared_sequence=shared_claims=0;}
void shared_select(){if(shared_stage!=shared_stage_id()||shared_epoch!=s.epoch){
    if(shared_epoch!=s.epoch){shared_world.discard_before(s.epoch);root_allocations.clear();object_timelines.clear();for(auto &q:entity_outbox)q.clear();}
    native_entities={};pickup_claims.clear();shared_stage=shared_stage_id();shared_epoch=s.epoch;}}
uint32_t world_connected_mask(){unsigned mask=0;for(unsigned i=0;i<MAX_PLAYERS;i++)if(s.players[i].present&&s.players[i].connected)mask|=1u<<i;return mask;}
unsigned entity_distance(unsigned who,const adventure::ObjectImage &image){auto &p=s.players[who].pose;
    int dx=int16_t(get16(image.bytes.data()+0x10)-p.x),dy=int(adventure::wrapped_delta(float(int16_t(get16(image.bytes.data()+0x14)-p.y)),float(p.wrap_y)));
    return unsigned((std::min)(int64_t(0xffffffffu),int64_t(dx)*dx+int64_t(dy)*dy));}
// Hidden Palace's story sequence (Sonic/Tails route, act 2) is played by every
// client with its own Player_1: Obj_CutsceneKnuckles/CutsceneKnux_HPZ, which
// copies itself into fixed slot 45 without an allocation and fights whoever is
// Player_1; the Master Emerald he reads through FABA (set only by its
// initializer); the teleporter whose helper scripts Player_1 and the camera and
// which beams Knuckles through FAA4; and everything these create. Only the
// fight's health is shared, through one boss ledger (boss_key).
bool hpz_story_code(unsigned code){
    return r16(0xfe10)==0x1601&&(code==0x61d4c||(code>=0x63cfc&&code<0x656da)||(code>=0x906a0&&code<0x90774)||(code>=0x45528&&code<0x45dae));
}
// Identity zero means this object belongs to a player's presentation/equipment.
adventure::EntityKey identify_entity(unsigned obj) {
    if(!native_slot(obj)||!r32(obj))return {};
    auto &local=native_entities[(obj-0xb000)/0x4a];
    if(local.personal)return {};
    if(hpz_story_code(r32(obj))){local=NativeEntity{};local.personal=true;local.initial_code=0x63d1a;return {};}
    // A boss can become a generic end-sign controller in the same slot.
    // Reclassifying that established shared allocation as personal loses its
    // identity and causes shared_spawn_nearby to resurrect its original boss.
    if(local.classified&&local.key.id)return local.key;
    unsigned code=r32(obj),respawn=r16(obj+0x48);uint64_t key=0;
    // Big rings already have a shared, persistent consumption ledger, while
    // their entry animation transports only the player who entered.
    bool personal=(code>=0x6166a&&code<=0x61910)||(code>=0x1d7ba&&code<0x1db40);
    // A shared world does not advance somebody else's checkpoint, result
    // screen, or finish-line barrier. These controllers write player progress.
    personal|=(code>=0x2d690&&code<0x2e600)||(code>=0x85ba8&&code<0x85c36);
    // SSZ's entrance sequencers directly move the local player/camera. A
    // replicated countdown would repeatedly subtract camera Y on a peer.
    // The spawned Knuckles NPC and physical level objects still use the world.
    personal|=ssz_entry_presentation(code);
    // This sequencer waits for the local player to land, stops the repeating
    // camera and runs that player out of the Mushroom Hill chase. Its shared
    // boss triggers a separate native sequencer for each participant below.
    personal|=code>=0x768b6&&code<0x76926;
    // Death Egg's fixed teleport pads hold per-player transit counters and
    // directly move Player_1. Gravity changes still use shared terrain state.
    personal|=code==0x48c16;
    // After the Death Egg end boss, loc_7F2DC/loc_7F2FE follow the local
    // camera and Player_1 to the act-3 StartNewLevel, then delete themselves.
    // Each client runs its own copy (see shared_dez_exit_pre).
    personal|=code>=0x7f2dc&&code<0x7f336;
    // Doomsday's flight controller (loc_81492, created by DDZ_ScreenInit on
    // every client) is each Sonic player's Super flight: it drives Player_1,
    // the camera and the scroll speed every frame.
    personal|=code>=0x81492&&code<0x817da;
    // These controllers operate directly on the local player's controls,
    // landing motion and camera. Sharing their program counter/timer can
    // re-lock somebody who has already finished their individual entrance.
    personal|=(code>=0x396e4&&code<0x39c50) || // Ice Cap / ground launch
              (code>=0x41fee&&code<0x4224e) || // Sandopolis fall / jump out
              (code>=0x44a0c&&code<0x44a68) || // scripted entrance run
              (code>=0x53a34&&code<0x53ad4);   // personal snow-pile jump out
    if(native_slot(respawn)&&native_entities[(respawn-0xb000)/74].personal&&ssz_entry_presentation(native_entities[(respawn-0xb000)/74].initial_code))personal=true;
    // The starpost itself is shared; its bonus-stage portal is a personal trip.
    personal|=code==0x2cfa8&&r8(obj+5)==8;
    // Native child creation copies mappings before SetUp_ObjAttributes3 sets
    // world-coordinate rendering. Classifying that uninitialized child as
    // screen-space permanently detached physical boss parts from the world.
    // Collision-only children (such as Tunnelbot's spikes) never render at
    // all; their explicit shared parent still makes them world objects.
    personal|=local.initialized&&!local.seed.parent&&r32(obj+0xc)!=0&&!(r8(obj+4)&4);
    // Classification belongs to an allocation, not its current function.
    // Native objects temporarily use generic wait/draw routines during init.
    if(personal){local=NativeEntity{};local.personal=true;local.initial_code=code;return {};}
    // The layout loader calls AllocateObject before filling respawn_addr.
    // Replace that provisional allocation identity with the layout identity
    // before first execution. Otherwise different load orders create several
    // copies of one boss, despite all copies pointing at the same respawn byte.
    bool layout=!local.initialized&&!local.seed.parent&&respawn>=0xeb00&&respawn<0xee00;
    if(local.key.id&&(!layout||local.key.id==(0x1000000000000000ull|(respawn-0xeb00+1)))){if(!local.initial_code){local.initial_code=code;memcpy(local.seed.initial.data(),g_ram+obj,74);}return local.key;}
    if(layout)key=0x1000000000000000ull|(respawn-0xeb00+1);
    else if((code==0x1a64a||code==0x1a662)&&r8(obj+5)==0)
        key=entity_hash(0x52494e4700000000ull|unsigned(s.local_id),++shared_personal_birth);
    else if(respawn>=0xeb00&&respawn<0xee00)key=0x1000000000000000ull|(respawn-0xeb00+1);
    else {
        // Monitor contents award the collector locally. Their effect is not a
        // second collectible and must never give a reward on another machine.
        if(code==0x1d7ba||code==0x1d7ce||code==0x1d81a||code==0x1db2e)return {};
        uint64_t parent=0;
        for(unsigned offset:{0x42u,0x46u,0x48u}){unsigned address=r16(obj+offset);if(native_slot(address)&&address!=obj){parent=native_entities[(address-0xb000)/0x4a].key.id;if(parent)break;}}
        if(parent)key=entity_hash(parent,(uint64_t(code)<<32)|(uint64_t(r16(obj+0x10))<<16)|r16(obj+0x14));
        else key=entity_hash(uint64_t(code)<<16|r8(obj+0x2c),(uint64_t(r16(obj+0x10))<<16)|r16(obj+0x14));
    }
    if((key>>60)==1)for(unsigned i=3;i<93;i++)if(slot_address(i)!=obj&&native_entities[i].key.id==key&&r32(slot_address(i))){
        // A layout load can race an incoming remote spawn. Keep one instance.
        for(unsigned j=0;j<74;j++)w8(obj+j,0);local=NativeEntity{};return {};
    }
    local.key={s.epoch,shared_stage_id(),key};local.initial_code=code;memcpy(local.seed.initial.data(),g_ram+obj,74);return local.key;
}
adventure::ObjectImage capture_entity(unsigned obj) {
    auto &local=native_entities[(obj-0xb000)/74];
    if(!local.seed_ready){
        local.seed_ready=true;
        for(unsigned i=0;i<14;i++){unsigned offset=0x2e+i*2,address=get16(local.seed.initial.data()+offset);
            if(native_slot(address)&&native_entities[(address-0xb000)/74].key.id){local.seed.initial_reference_mask|=uint16_t(1u<<i);local.seed.initial_references[i]=native_entities[(address-0xb000)/74].key.id;put16(local.seed.initial.data()+offset,0);}}
    }
    adventure::ObjectImage image=local.seed;memcpy(image.bytes.data(),g_ram+obj,74);
    // A pending custom boss-hit replay points the routine at a mid-function
    // entry with health one above its target. Publish the routine it returns
    // to and the resulting health; a replica must never run that entry as if
    // it were a fresh native contact.
    {auto &boss=world.local[(obj-0xb000)/74];
     if(boss.replay&&boss.key==local.key.id){put32(image.bytes.data(),boss.code);image.bytes[0x29]=uint8_t(image.bytes[0x29]-1);}}
    // A native object can reuse a link field as a timer after a phase change.
    // Recompute current links instead of retaining the remote spawn's mask.
    image.reference_mask=0;image.references={};
    image.children=native_entities[(obj-0xb000)/0x4a].children;
    for(unsigned i=0;i<14;i++) {unsigned offset=0x2e+i*2,address=get16(image.bytes.data()+offset);
        if(native_slot(address)&&r32(address)) {image.reference_mask|=uint16_t(1u<<i);image.references[i]=native_entities[(address-0xb000)/0x4a].key.id;put16(image.bytes.data()+offset,0);}
    }
    return image;
}
unsigned local_entity_address(uint64_t key){if(!key)return 0;for(unsigned i=3;i<93;i++)if(native_entities[i].key.id==key&&r32(slot_address(i)))return slot_address(i);return 0;}
bool entity_links_ready(const adventure::ObjectImage &image){
    for(unsigned i=0;i<14;i++)if((image.reference_mask&(1u<<i))&&image.references[i]&&!local_entity_address(image.references[i]))return false;
    return true;
}
void remember_object_motion(const adventure::WorldEntity &entity){
    if(entity.owner==s.local_id||entity.removed){object_timelines.erase(entity.key);return;}
    auto &timeline=object_timelines[entity.key];if(timeline.generation!=entity.generation){timeline.motion.clear();timeline.generation=entity.generation;}
    ObjectMotion::Frame f;f.tick=entity.tick;f.epoch=entity.key.epoch;f.stage=entity.key.stage;f.warp=entity.generation;f.visible=true;f.image=entity.image;
    f.y_period=entity.owner>=0?float(s.players[entity.owner].pose.wrap_y):0;
    f.x=float(int16_t(get16(f.image.bytes.data()+16)))+float(get16(f.image.bytes.data()+18))/65536;
    f.y=float(int16_t(get16(f.image.bytes.data()+20)))+float(get16(f.image.bytes.data()+22))/65536;
    timeline.motion.push(std::move(f),double(SDL_GetTicks64()));
}
void apply_entity(unsigned obj,const adventure::WorldEntity &entity) {
    auto &local=native_entities[(obj-0xb000)/0x4a];
    if(entity.removed){unsigned respawn=r16(obj+0x48);for(unsigned i=0;i<74;i++)w8(obj+i,0);local=NativeEntity{};if(respawn>=0xeb00&&respawn<0xee00)w8(respawn,r8(respawn)|0x80);return;}
    // Let the native init routine load its art before importing a running state.
    if(!local.initialized||!entity_links_ready(entity.image))return;
    auto &boss_local=world.local[(obj-0xb000)/74];
    if(boss_local.key==entity.key.id&&boss_local.pending){
        // Local collision has already disabled the boss's hitbox. Do not
        // replace that response with a pre-hit snapshot while its grant travels.
        if(entity.image.bytes[0x29]>boss_local.pending_hp)return;
        boss_local.pending=false;
    }
    auto image=entity.image;ObjectMotion::View view{};
    auto timeline=object_timelines.find(entity.key);
    if(!entity.consumed&&entity.owner!=s.local_id&&timeline!=object_timelines.end()){
        // Interpolate position only. Delaying logical state would rewind
        // damage, invulnerability, switches and destruction with every pose.
        view=timeline->second.motion.sample(double(SDL_GetTicks64()));
    }
    unsigned visibility=r8(obj+4)&0x80,respawn=r16(obj+0x48),tile=r16(obj+0xa)&0x7ff,contact=r8(obj+0x2a)&0x78;
    bool holder=slot_holder(obj);
    for(unsigned i=0;i<74;i++){
        if(i>=0x2e&&i<0x4a&&(image.reference_mask&(1u<<((i-0x2e)/2))))continue;
        if(holder&&i>=0x3b&&i<0x3e)continue;
        w8(obj+i,image.bytes[i]);
    }
    if(holder)w8(obj+0x2a,r8(obj+0x2a)|4);
    w8(obj+4,(r8(obj+4)&0x7f)|visibility);
    // Dynamic art slots and solid-object contact bits are local bookkeeping.
    // Copying them would use another client's VRAM allocation or standing bits.
    w16(obj+0xa,(r16(obj+0xa)&0xf800)|tile);
    w8(obj+0x2a,(r8(obj+0x2a)&~0x78)|contact);
    if(respawn>=0xeb00&&respawn<0xee00)w16(obj+0x48,respawn);
    for(unsigned i=0;i<14;i++)if(image.reference_mask&(1u<<i)){
        unsigned address=local_entity_address(image.references[i]);if(address)w16(obj+0x2e+i*2,address);
    }
    if(view.frame){w32(obj+16,uint32_t(int64_t(std::llround(double(view.x)*65536))));w32(obj+20,uint32_t(int64_t(std::llround(double(adventure::wrapped_position(view.y,float(local_y_period())))*65536))));}
    local.children=entity.image.children;local.revision=entity.revision;local.generation=entity.generation;
}
bool decode_entity_image(const unsigned char *p,unsigned n,adventure::ObjectImage &image){
    if(!adventure::decode_world_image(p,n,image))return false;
    for(const auto *bytes:{image.bytes.data(),image.initial.data()}){
        unsigned code=get32(bytes),mapping=get32(bytes+12);
        if((code&1)||code>=sizeof(g_rom)||(mapping&&mapping>=sizeof(g_rom)&&mapping<0xff0000))return false;
    }
    return true;
}
unsigned write_entity_packet(const adventure::WorldEntity &entity,unsigned char *p){
    memset(p,0,ENTITY_BYTES);put32(p,ENTITY_COMMIT);put32(p+4,entity.key.epoch);put32(p+8,entity.key.stage);put64(p+12,entity.key.id);
    put32(p+20,entity.revision);put32(p+24,entity.generation);put32(p+28,entity.tick);put32(p+32,unsigned(entity.owner));put32(p+36,unsigned(entity.collector));
    put32(p+40,(entity.consumed?1u:0)|(entity.removed?2u:0)|(entity.goal_open?4u:0));put16(p+44,entity.goal_x);put16(p+46,entity.goal_y);return 48+unsigned(adventure::encode_world_image(p+48,entity.image));
}
void commit_entity(const adventure::WorldEntity &entity,int channel=-1,bool authority_change=false){
    for(unsigned i=1;i<MAX_CHANNELS;i++)if(channels[i].hello&&(channel<0||unsigned(channel)==i)){
        int peer=channels[i].peer;
        // Persistent consumption reaches everyone. Continuous motion is needed
        // only near the recipient, allowing players to cover separate areas.
        if(channel<0&&!authority_change&&!entity.consumed&&!entity.removed&&!entity.goal_open&&peer>=0&&(entity.image.bytes[4]&4)&&get32(entity.image.bytes.data()+12)){auto &pose=s.players[peer].pose;
            if(pose.stage!=(entity.key.stage&65535)||entity_distance(unsigned(peer),entity.image)>1024u*1024u)continue;}
        entity_outbox[i][entity.key]=entity;
    }
}
void flush_entities(){
    if(!s.host)return;
    for(unsigned i=1;i<MAX_CHANNELS;i++){
        auto &queue=entity_outbox[i];if(!channels[i].hello){queue.clear();continue;}
        for(unsigned budget=0;budget<24&&!queue.empty()&&channels[i].tx_size<8192;budget++){
            auto it=queue.upper_bound(entity_cursor[i]);if(it==queue.end())it=queue.begin();entity_cursor[i]=it->first;
            unsigned char p[ENTITY_BYTES];unsigned n=write_entity_packet(it->second,p);send_packet(i,WORLD,p,n);queue.erase(it);
        }
    }
}
void shared_snapshot(unsigned channel){if(shared_fire_epoch==s.epoch)shared_phase_packet(int(channel));terrain_snapshot(channel);scene_snapshot(channel);for(const auto &entry:shared_world.entities())commit_entity(entry.second,int(channel));}
void send_entity_report(unsigned obj,bool immediate=false){
    auto key=identify_entity(obj);if(!key.id||s.local_id<0)return;auto &local=native_entities[(obj-0xb000)/0x4a];
    // Hurt can allocate the spill after the object loop has already run. Its
    // routine-zero image is a command to empty the local player's pockets,
    // not an individual ring. Publish only after the native spill initializer.
    if((r32(obj)==0x1a64a||r32(obj)==0x1a662)&&r8(obj+5)==0)return;
    auto *canonical=shared_world.find(key);unsigned now=SDL_GetTicks();
    bool owner=!canonical||canonical->owner==s.local_id;
    bool touching=local.initialized&&(r8(obj+0x2a)&0x28)&&entity_distance(unsigned(s.local_id),capture_entity(obj))<128u*128u;
    if(touching!=local.touching){local.touching=touching;immediate=true;}
    if(!immediate&&now-local.last_report<(owner?30u:200u))return;
    if(!s.host&&channels[0].tx_size>8192)return;
    local.last_report=now;
    unsigned char p[ENTITY_BYTES]{};put32(p,ENTITY_REPORT);put32(p+4,key.epoch);put32(p+8,key.stage);put64(p+12,key.id);
    put32(p+20,unsigned(s.local_id));put32(p+24,canonical?canonical->generation:0);put32(p+28,s.frames);put32(p+32,++shared_sequence);
    put32(p+40,touching?1:0);
    auto image=capture_entity(obj);
    // Routine 4 is a local TouchResponse request, not a shared broken state.
    // Publishing it before the claim lets a replica mistake that request for
    // its own collision and steal the reward. The host's consumed commit is
    // the only event that publishes the broken monitor to other players.
    if(get32(image.bytes.data())==0x1d566&&image.bytes[5]==4&&(!canonical||!canonical->consumed))image.bytes[5]=2;
    // The widening target is the reporter's stored camera bound (unused $3A).
    if(local_camera_code(get32(image.bytes.data()))){unsigned code=get32(image.bytes.data());put16(image.bytes.data()+0x3a,r16(code==0x84a48?0xfa92:0xfa94));image.bytes[0x3c]=1;}
    put32(p+36,entity_distance(unsigned(s.local_id),image));unsigned n=48+unsigned(adventure::encode_world_image(p+48,image));
    if(s.host)shared_handle(MAX_CHANNELS,p,n);else send_packet(0,WORLD,p,n);
}
int request_pickup(unsigned obj,unsigned kind) {
    if(!world_play()||s.local_id<0||applying_pickup)return 0;
    auto key=identify_entity(obj);if(!key.id)return 0;
    unsigned now=SDL_GetTicks();PickupClaim *pending=nullptr;
    for(auto &claim:pickup_claims)if(claim.key.id==key.id){pending=&claim;break;}
    if(pending){
        auto *entity=shared_world.find(key);
        if(pending->applied||(entity&&entity->consumed)||now-pending->last_attempt<100)return 1;
        pending->last_attempt=now;
    }else {pickup_claims.push_back({key,obj,kind,false,now});shared_claims++;}
    // Contact runs inside the native frame, before the regular pose broadcast.
    // Keep the current location ahead of its claim on the reliable channel.
    sample_pose();
    if(!s.host){unsigned char pose[80];encode_pose(unsigned(s.local_id),pose);send_packet(0,POSE,pose,sizeof pose);}
    send_entity_report(obj,true);
    unsigned char p[32]{};put32(p,ENTITY_CLAIM);put32(p+4,key.epoch);put32(p+8,key.stage);put64(p+12,key.id);put32(p+20,unsigned(s.local_id));put32(p+24,kind);
    if(s.host)shared_handle(MAX_CHANNELS,p,sizeof p);else send_packet(0,WORLD,p,sizeof p);
    return 1;
}
int shared_monitor_hook(){return request_pickup(g_cpu.A[0]&65535,1);}
int shared_badnik_hook(){return request_pickup(g_cpu.A[1]&65535,2);}
int shared_object_ring_hook(unsigned kind){return request_pickup(g_cpu.A[0]&65535,kind);}
void shared_ring_window(){
    if(s.phase!=PLAY||side_trip()||r8(0xf600)!=0x0c)return;
    unsigned stage=r16(0xfe10);if(stage>0x1700||r8(0xfe10)==0x14)return;
    unsigned table=0x1e3e58+(stage>>8)*8+(stage&1)*4;
    unsigned base=get32(g_rom+table);if(base+4>=sizeof(g_rom))return;
    unsigned end=base;
    for(unsigned i=0;i<512&&end+4<sizeof(g_rom)&&get16(g_rom+end)!=65535;i++)end+=4;
    if(end+4>=sizeof(g_rom)||get16(g_rom+end)!=65535)return;
    unsigned first=r32(0xee42),last=r32(0xee46);
    if(first>=base&&first<=last&&last<=end&&(first-base)%4==0&&(last-base)%4==0&&r16(0xee4a)==0xe700+(first-base)/2)return;
    // A seamless layout swap can leave the old end beside the new start.
    // Both collision and drawing require a coherent window in this layout.
    int camera=int16_t(r16(0xee78));unsigned lo=camera>8?unsigned(camera-8):1,hi=lo+0x150;
    first=base;while(first<end&&get16(g_rom+first)<lo)first+=4;
    last=first;while(last<end&&get16(g_rom+last)<hi)last+=4;
    w32(0xee42,first);w32(0xee46,last);w16(0xee4a,0xe700+(first-base)/2);
}
// A granted hit makes the attacker the struck part's authority. Hand over its
// whole creator assembly in the same commit: parts report to their parents by
// writing into them (a finger's death sets its hand's bit, a hand releases the
// robot's phase), and those writes are lost while parent and part belong to
// different owners until the next regrouping pass.
void shared_transfer_assembly(const adventure::EntityKey &key,int who){
    auto root_of=[&](const adventure::WorldEntity *e){
        for(unsigned depth=0;depth<93&&e->image.parent;depth++){auto *parent=shared_world.find({e->key.epoch,e->key.stage,e->image.parent});if(!parent||parent->removed)break;e=parent;}
        return e->key.id;};
    auto *hit=shared_world.find(key);if(!hit)return;uint64_t root=root_of(hit);
    std::vector<adventure::EntityKey> members;
    for(const auto &entry:shared_world.entities()){const auto &e=entry.second;
        if(e.key.epoch==key.epoch&&e.key.stage==key.stage&&!e.removed&&e.owner>=0&&e.owner!=who&&root_of(&e)==root)members.push_back(e.key);}
    for(const auto &member:members){auto *e=shared_world.find(member);e->owner=who;e->generation++;e->revision++;e->tick=0;commit_entity(*e,-1,true);}
}
void shared_commit_boss_damage(unsigned stage,uint64_t id,unsigned who,unsigned hp){
    auto *e=shared_world.find({s.epoch,stage,id});if(!e||e->removed)return;
    shared_transfer_assembly({s.epoch,stage,id},int(who));e=shared_world.find({s.epoch,stage,id});
    auto &b=e->image.bytes;
    if(b[0x28])b[0x25]=b[0x28];b[0x28]=0;b[0x1c]=0;b[0x29]=uint8_t(hp);
    if(!hp)b[0x2a]|=0x80;
    // The hitting player's native response continues the authoritative boss.
    // Publish health and ownership atomically with the contact decision.
    e->owner=int(who);e->generation++;e->revision++;e->tick=0;
    commit_entity(*e);
}
int shared_ring_draw_guard(){
    if(s.phase!=PLAY||side_trip())return 0;
    unsigned destination=g_cpu.A[6]&65535;
    if(int16_t(g_cpu.D[7])<0||destination<0xf800||destination+8>0xfa80){online_host->tail_call(0xebec);return 1;}
    return 0;
}
int shared_ring_hook(bool attraction){
    if(!world_play()||applying_pickup||s.local_id<0)return 0;
    unsigned address=g_cpu.A[4]&65535,layout=g_cpu.A[1]&0xffffff;
    if(address<0xe700||address>=0xeb00||(address&1)||layout+4>sizeof(g_rom))return 0;
    adventure::EntityKey key{s.epoch,shared_stage_id(),0x2000000000000000ull|((address-0xe700)/2+1)};
    bool pending=false;for(const auto &claim:pickup_claims)if(claim.key.id==key.id)pending=true;
    if(!pending){
        pickup_claims.push_back({key,address,attraction?4u:3u,false,SDL_GetTicks(),layout});shared_claims++;
        sample_pose();if(!s.host){unsigned char pose[80];encode_pose(unsigned(s.local_id),pose);send_packet(0,POSE,pose,sizeof pose);}
        adventure::ObjectImage image;put16(image.bytes.data()+16,get16(g_rom+layout));put16(image.bytes.data()+20,get16(g_rom+layout+2));
        unsigned char report[ENTITY_BYTES]{};put32(report,ENTITY_REPORT);put32(report+4,key.epoch);put32(report+8,key.stage);put64(report+12,key.id);put32(report+20,unsigned(s.local_id));put32(report+28,s.frames);put32(report+36,entity_distance(unsigned(s.local_id),image));
        unsigned size=48+unsigned(adventure::encode_world_image(report+48,image));
        if(s.host)shared_handle(MAX_CHANNELS,report,size);else send_packet(0,WORLD,report,size);
        unsigned char claim[32]{};put32(claim,ENTITY_CLAIM);put32(claim+4,key.epoch);put32(claim+8,key.stage);put64(claim+12,key.id);put32(claim+20,unsigned(s.local_id));put32(claim+24,attraction?4:3);
        if(s.host)shared_handle(MAX_CHANNELS,claim,sizeof claim);else send_packet(0,WORLD,claim,sizeof claim);
    }
    // The native reward is deferred until the host grants this exact ring.
    // Continue its collision scan without altering the loop's registers.
    if(!attraction)online_host->tail_call(0xeada);
    return 1;
}
void shared_rings_pre(){
    for(auto &claim:pickup_claims)if(!claim.applied&&(claim.kind==3||claim.kind==4)){
        auto *entity=shared_world.find(claim.key);if(!entity||!entity->consumed||entity->collector!=s.local_id)continue;
        claim.applied=true;M68KState saved=g_cpu;applying_pickup=true;
        if(claim.kind==4){g_cpu.A[0]=0xffb000;g_cpu.A[1]=claim.layout;g_cpu.A[4]=0xff0000|claim.object;recomp_call_addr(0xeaf0);}
        else {
            w16(claim.object,0x604);recomp_call_addr(0xeae6);
            for(unsigned a=0xef82;a<0xf000;a+=2)if(!r16(a)){w16(a,claim.object);w16(0xef80,r16(0xef80)+1);break;}
        }
        applying_pickup=false;g_cpu=saved;
    }
    for(const auto &entry:shared_world.entities()){
        const auto &e=entry.second;if(e.key.epoch!=s.epoch||e.key.stage!=shared_stage||(e.key.id>>60)!=2||!e.consumed)continue;
        unsigned index=unsigned(e.key.id&0xffff);if(!index||index>512)continue;
        unsigned address=0xe700+(index-1)*2;
        if(!r16(address)){w16(address,0xffff);if(r16(0xff04))w16(0xff04,r16(0xff04)-1);}
    }
}
void shared_spawn_nearby(){
    int cx=int16_t(r16(0xee80)),cy=int16_t(r16(0xee84));
    // Linked native objects can reference both the preceding and following
    // child. Reserve the whole visible graph before resolving its pointers;
    // requiring each neighbour to exist first deadlocks an absent arm chain.
    std::map<uint64_t,std::pair<const adventure::WorldEntity*,unsigned>> pending;
    std::vector<unsigned> free_slots;
    for(unsigned i=3;i<93;i++)if(!r32(slot_address(i)))free_slots.push_back(slot_address(i));
    for(const auto &entry:shared_world.entities()){
        const auto &e=entry.second;
        if(e.key.epoch!=s.epoch||e.key.stage!=shared_stage||e.removed||e.owner<0||local_entity_address(e.key.id)||!get32(e.image.initial.data())||local_effect_code(get32(e.image.initial.data())))continue;
        // A replica that just finished natively (e.g. the Death Egg robot's
        // root deleting itself after its fall) must not be re-created from its
        // initializer while the owner's removal is in flight: rerunning that
        // init re-scripts the player and respawns the encounter's intro.
        if(auto gone=local_deleted.find(e.key);gone!=local_deleted.end()&&SDL_GetTicks()-gone->second<5000)continue;
        int x=get16(e.image.bytes.data()+16),y=get16(e.image.bytes.data()+20);
        int dy=int(adventure::wrapped_delta(float(y-cy),float(local_y_period())));
        if(x<cx-128||x>cx+448||dy< -128||dy>352)continue;
        if(pending.size()==free_slots.size())break;
        pending.emplace(e.key.id,std::make_pair(&e,free_slots[pending.size()]));
    }
    bool pruned=true;
    while(pruned){pruned=false;for(auto it=pending.begin();it!=pending.end();){
        const auto &image=it->second.first->image;bool resolved=true;
        for(unsigned i=0;i<14;i++)if(image.initial_reference_mask&(1u<<i)){
            auto id=image.initial_references[i];
            if(!local_entity_address(id)&&!pending.count(id)){resolved=false;break;}
        }
        if(!resolved){it=pending.erase(it);pruned=true;}else ++it;
    }}
    for(const auto &entry:pending){
        const auto &e=*entry.second.first;unsigned obj=entry.second.second;
        int x=cx+int16_t(get16(e.image.bytes.data()+16)-cx),y=int16_t(get16(e.image.bytes.data()+20));
        std::array<unsigned,14> refs{};
        for(unsigned i=0;i<14;i++)if(e.image.initial_reference_mask&(1u<<i)){
            auto id=e.image.initial_references[i];refs[i]=local_entity_address(id);
            if(!refs[i])refs[i]=pending.at(id).second;
        }
        const auto *seed=&e.image.initial;
        unsigned initial_code=get32(seed->data());
        if((initial_code==0x1a64a||initial_code==0x1a662)&&(*seed)[5]==0){
            if(e.image.bytes[5]==0)continue;
            seed=&e.image.bytes;
        }
        for(unsigned i=0;i<74;i++)w8(obj+i,(*seed)[i]);
        for(unsigned i=0;i<14;i++)if(e.image.initial_reference_mask&(1u<<i))w16(obj+0x2e+i*2,refs[i]);
        w16(obj+16,x);w16(obj+20,y);
        if((e.key.id>>60)==1){unsigned respawn=0xeb00+unsigned((e.key.id&0xffff)-1);if(respawn<0xee00){w16(obj+0x48,respawn);w8(respawn,r8(respawn)|0x80);}}
        auto &local=native_entities[(obj-0xb000)/74];local=NativeEntity{};local.key=e.key;local.initial_code=get32(e.image.initial.data());local.seed=e.image;local.seed_ready=true;
    }
}
uint64_t shared_entity_identity(unsigned obj){return identify_entity(obj).id;}
bool shared_entity_owned(unsigned obj){auto key=identify_entity(obj);if(!key.id)return true;auto *e=shared_world.find(key);return !e||e->owner==s.local_id;}
bool shared_goal(const adventure::WorldEntity &entity){unsigned code=get32(entity.image.initial.data());return code==0x837b2||code==0x86540;}
int shared_results_hook(){
    if(!world_play())return 0;
    for(const auto &entry:shared_world.entities()){
        const auto &entity=entry.second;if(entity.key.epoch!=s.epoch||entity.key.stage!=shared_stage||!shared_goal(entity)||entity_distance(unsigned(s.local_id),entity.image)>512u*512u)continue;
        if(local_goal_finished[entity.key])continue;local_goal_finished[entity.key]=true;
        sample_pose();if(!s.host){unsigned char pose[80];encode_pose(unsigned(s.local_id),pose);send_packet(0,POSE,pose,sizeof pose);}
        unsigned char p[32]{};put32(p,11);put32(p+4,entity.key.epoch);put32(p+8,entity.key.stage);put64(p+12,entity.key.id);put32(p+20,unsigned(s.local_id));
        if(s.host)shared_handle(MAX_CHANNELS,p,sizeof p);else send_packet(0,WORLD,p,sizeof p);
    }
    return 0;
}
void shared_goal_pre(){
    if(s.waiting||r8(0xb005)!=2||(r8(0xb02a)&2))return;
    for(const auto &entry:shared_world.entities()){
        const auto &e=entry.second;if(e.key.epoch!=s.epoch||e.key.stage!=shared_stage||!e.goal_open||local_goal_finished[e.key])continue;
        // A flying capsule (vertically flipped, e.g. after Lava Reef's boss)
        // hovers above the platforms the party stands on; its own sub_868F8
        // grants results to any grounded player without a distance check.
        bool flying=(e.image.bytes[4]&2)!=0;int range_x=flying?320:160,range_y=flying?400:192;
        if(abs(int(r16(0xb010))-e.goal_x)>range_x||std::abs(adventure::wrapped_delta(float(int(r16(0xb014))-e.goal_y),float(local_y_period())))>range_y)continue;
        unsigned goal=local_entity_address(e.key.id);if(!goal)continue;
        unsigned obj=0;for(unsigned i=3;i<93;i++)if(!r32(slot_address(i))){obj=slot_address(i);break;}if(!obj)return;
        local_goal_finished[e.key]=true;M68KState saved=g_cpu;g_cpu.A[0]=0xff0000|goal;g_cpu.A[1]=0xffb000;recomp_call_addr(0x869c6);g_cpu=saved;
        native_entities[(obj-0xb000)/74]=NativeEntity{};w32(obj,0x2dad0);
    }
}
void shared_chase_pre(){
    if(r16(0xfe10)!=0x701||r16(0xb010)<0x3c90)return;
    for(const auto &entry:shared_world.entities()){
        const auto &e=entry.second;
        if(e.key.epoch!=s.epoch||e.key.stage!=shared_stage||get32(e.image.initial.data())!=0x75f50||e.image.bytes[0x29]!=1||local_chase_finished[e.key])continue;
        unsigned free=0;bool exists=false;
        for(unsigned i=3;i<93;i++){
            unsigned obj=slot_address(i),code=r32(obj);
            if(code>=0x768b6&&code<0x76926)exists=true;
            if(!code&&!free)free=obj;
        }
        if(!exists){
            if(!free)return;
            for(unsigned j=0;j<74;j++)w8(free+j,0);
            native_entities[(free-0xb000)/74]=NativeEntity{};
            w32(free,0x768b6);
        }
        local_chase_finished[e.key]=true;
    }
}
void shared_lrz_scene_pre(){
    if(r16(0xfe10)!=0x1600)return;
    for(const auto &entry:shared_world.entities()){
        const auto &e=entry.second;
        if(e.key.epoch!=s.epoch||e.key.stage!=shared_stage)continue;
        if(get32(e.image.initial.data())==0x793e2&&e.removed){
            auto &seen=local_lrz_scene[e.key];
            if(!(seen&1)){seen|=1;w8(0xfab8,r8(0xfab8)|1);if(r16(0xeec0)==4)w8(0xeec4,0xff);}
        }
        if(get32(e.image.initial.data())!=0x78f56)continue;
        unsigned code=get32(e.image.bytes.data());auto &seen=local_lrz_scene[e.key];
        // Object snapshots cannot carry the native routine's writes to stage
        // event RAM. Replay those latched signals once, letting each client's
        // original screen event load its art and start its own camera scroll.
        if(code>=0x79002&&code<=0x790a6){
            if(!(seen&1)){
                seen|=1;w8(0xfab8,r8(0xfab8)|1);
                if(r16(0xeec0)==4)w8(0xeec4,0xff);
            }
            if((e.image.bytes[5]>=2||code==0x79072)&&!(seen&2)){seen|=2;w8(0xfab8,r8(0xfab8)|2);}
            if(code==0x79072&&!(seen&4)){
                seen|=4;if(r16(0xeec0)==8)w8(0xeec4,0xff);
            }
        }
    }
}
bool local_effect_near(const adventure::WorldEntity &e){
    // A helper belongs to the encounter of its creator. A player elsewhere in
    // the act (perhaps inside another arena) must not receive its effects.
    if(!e.image.parent)return true;
    auto *creator=shared_world.find({e.key.epoch,e.key.stage,e.image.parent});if(!creator)return true;
    int dx=int16_t(get16(creator->image.bytes.data()+16)-r16(0xb010));
    float dy=adventure::wrapped_delta(float(int16_t(get16(creator->image.bytes.data()+20)-r16(0xb014))),float(local_y_period()));
    return abs(dx)<0x400&&std::abs(dy)<0x300;
}
void shared_effect_pre(){
    for(const auto &entry:shared_world.entities()){
        const auto &e=entry.second;unsigned code=get32(e.image.initial.data());
        if(e.key.epoch!=s.epoch||e.key.stage!=shared_stage||e.removed||e.owner<0||!local_effect_code(code))continue;
        auto &seen=local_camera_seen[e.key];if(seen)continue;
        if(local_entity_address(e.key.id)){seen=true;continue;}
        if(!local_effect_near(e))continue;
        // The arena clamp already ran here (the background target left $6C0).
        if(code==0x810a0&&r16(0xeed2)!=0x6c0){seen=true;continue;}
        unsigned obj=0;for(unsigned i=3;i<93;i++)if(!r32(slot_address(i))){obj=slot_address(i);break;}if(!obj)return;
        seen=true;
        if(local_camera_code(code)&&e.image.bytes[0x3c])w16(code==0x84a48?0xfa92:0xfa94,get16(e.image.bytes.data()+0x3a));
        // The creator's initializer image (subtype selects the music); links are
        // already cleared in the seed and none of these helpers follows one.
        for(unsigned j=0;j<74;j++)w8(obj+j,e.image.initial[j]);
        w16(obj+0x48,0);
        auto &local=native_entities[(obj-0xb000)/74];local=NativeEntity{};local.personal=true;local.initial_code=code;
    }
}
// The Death Egg act 2 defeat (loc_7FBD6) sets FAB8 bit 0, which only the arena's
// teleporter capsules read: it stops the single player at the boss from
// leaving the exit floor. In co-op another player can be inside an upper or
// lower capsule when someone else lands the final hit and would be shut in
// for good. After the defeat, keep the capsules disabled only while this
// client's player stands on the exit floor (the middle capsule's level).
unsigned dez_capsule_epoch=~0u;
void shared_dez_capsule_pre(){
    if(r16(0xfe10)!=0xb01)return;
    if(!(r8(0xfab8)&1)&&dez_capsule_epoch!=s.epoch)return;
    dez_capsule_epoch=s.epoch;
    unsigned y=r16(0xb014);bool exit_floor=y>=0x300&&y<0x360;
    w8(0xfab8,exit_floor?(r8(0xfab8)|1):(r8(0xfab8)&~1u));
}
void shared_dez_exit_pre(){
    // The Death Egg end boss finishes its countdown at loc_7F2B4 with local
    // effects (Boss_flag, music, Camera_stored_max_X_pos and the camera
    // widening), then waits at loc_7F2DC/loc_7F2FE for the local camera and
    // Player_1 before StartNewLevel($1700) and deleting itself. Shared, only
    // the authority reached that exit and its deletion removed everyone
    // else's. The shared boss now rests there; each client runs its own exit.
    if(r16(0xfe10)!=0xb01)return;
    for(const auto &entry:shared_world.entities()){
        const auto &e=entry.second;unsigned code=get32(e.image.bytes.data());
        if(e.key.epoch!=s.epoch||e.key.stage!=shared_stage||e.removed||(code!=0x7f2dc&&code!=0x7f2fe)||local_dez_exit[e.key])continue;
        unsigned obj=0;for(unsigned i=3;i<93;i++)if(!r32(slot_address(i))){obj=slot_address(i);break;}if(!obj)return;
        local_dez_exit[e.key]=true;
        if(r8(0xf7aa)){
            // This client's replica never ran the countdown's final frame.
            w8(0xf7aa,0);w16(0xfa92,0x3620);
            M68KState saved=g_cpu;g_cpu.D[0]=0x17;recomp_call_addr(0x1358);g_cpu=saved;
        }
        for(unsigned j=0;j<74;j++)w8(obj+j,0);
        auto &local=native_entities[(obj-0xb000)/74];local=NativeEntity{};local.personal=true;local.initial_code=0x7f2dc;
        w32(obj,0x7f2dc);
    }
}
// Replaying an art load must respect the native queues: Queue_Kos_Module and
// Load_PLC_Raw scan for a free entry without a bound, so a full queue (four
// KosM entries at FF64, sixteen Nemesis entries at F680) would overwrite the
// decompressors' own state that follows it and corrupt all of RAM. A replica
// often queues the same art natively too; skip anything already queued.
bool art_queued(unsigned base,unsigned entries,unsigned art){for(unsigned i=0;i<entries;i++)if(r32(base+i*6)==art)return true;return false;}
unsigned art_free(unsigned base,unsigned entries){unsigned n=0;for(unsigned i=0;i<entries;i++)if(!r32(base+i*6))n++;return n;}
void replay_kos_module(unsigned art,unsigned vram){
    if(art_queued(0xff64,4,art)||!art_free(0xff64,4))return;
    M68KState saved=g_cpu;g_cpu.A[1]=art;g_cpu.D[2]=(g_cpu.D[2]&0xffff0000u)|vram;recomp_call_addr(0x1ad2);g_cpu=saved;
}
void replay_plc(unsigned list){
    unsigned header=get16(g_rom+list);if(header&0x8000)return;unsigned pieces=header+1;bool missing=false;
    for(unsigned i=0;i<pieces;i++)if(!art_queued(0xf680,16,get32(g_rom+list+2+i*6)))missing=true;
    if(!missing||art_free(0xf680,16)<pieces)return;
    M68KState saved=g_cpu;g_cpu.A[1]=list;recomp_call_addr(0x172c);g_cpu=saved;
}
#if OA_TEST_CONTROL
// Private diagnostics: report the first frame/object after which RAM looks
// block-filled, with the preceding background-event state of the frame ring.
bool test_ram_filled(){unsigned v=r16(0x5000);return v&&v==r16(0x8000)&&v==r16(0x9800)&&v==r16(0xc000)&&v==r16(0xd000);}
bool test_fill_logged=false;unsigned test_object_code=0;
struct TestFrame{unsigned frame,eec0,eec2,eec6,eec8,eeca,eecc,eed2,eede,eee2,code,routine,kosleft,kos0,nem0,bg2,bg4,camybg,plane,rooty,mode;};
std::array<TestFrame,240> test_ring{};unsigned test_ring_at=0;
void test_fill_check(const char *where,unsigned obj){
    if(test_fill_logged||!test_ram_filled())return;test_fill_logged=true;
    fprintf(stderr,"[fill] detected %s obj=%04x code_before=%06x frame=%u fe10=%04x\n",where,obj,test_object_code,s.frames,r16(0xfe10));
    for(unsigned i=0;i<test_ring.size();i++){const auto &f=test_ring[(test_ring_at+i)%test_ring.size()];if(!f.frame)continue;
        fprintf(stderr,"[fill] f=%u fg=%04x bg=%04x fg5=%04x dpos=%04x drow=%04x shake=%04x bg0=%04x bgC=%04x bg10=%04x root=%06x/%02x y=%04x kos=%02x/%06x nem=%06x bg2=%04x bg4=%04x camybg=%04x plane=%04x mode=%02x\n",f.frame,f.eec0,f.eec2,f.eec6,f.eec8,f.eeca,f.eecc,f.eed2,f.eede,f.eee2,f.code,f.routine,f.rooty,f.kosleft,f.kos0,f.nem0,f.bg2,f.bg4,f.camybg,f.plane,f.mode);}
    fflush(stderr);
}
void test_ring_record(){
    unsigned code=0,routine=0,y=0;for(unsigned i=3;i<93;i++){unsigned obj=slot_address(i),c=r32(obj);if(native_entities[i].initial_code==0x7fd68&&c){code=c;routine=r8(obj+5);y=r16(obj+0x14);}}
    test_ring[test_ring_at++%test_ring.size()]={s.frames,r16(0xeec0),r16(0xeec2),r16(0xeec6),r16(0xeec8),r16(0xeeca),r16(0xeecc),r16(0xeed2),r16(0xeede),r16(0xeee2),code,routine,
        r8(0xff60),r32(0xff64)&0xffffff,r32(0xf680)&0xffffff,r16(0xeed4),r16(0xeed6),r16(0xee90),r16(0xef74),y,r8(0xf600)};
}
#define TEST_BRIDGE(step) fprintf(stderr,"[dez3] frame=%u step=%s replica=%d\n",s.frames,step,int(replica))
#else
#define TEST_BRIDGE(step) ((void)0)
#endif
void shared_dez3_scene_pre(){
    // The final boss root scripts every Player_1 into the arena: run at +6
    // per frame (routines 2/4, executed natively by each copy), then at its
    // phase changes clear Scroll_lock (2->4), Stop_Object the player and set
    // its stand pose (4->6), and return control once FAB8 bit 1 arrives
    // (6->8). Those are one-time writes inside the authority's transition
    // frame; a replica reverted to the next routine never ran them and stayed
    // locked. Replay each step once per client when the shared root passes it.
    if(r16(0xfe10)!=0x1700)return;
    for(const auto &entry:shared_world.entities()){
        const auto &e=entry.second;
        if(e.key.epoch!=s.epoch||e.key.stage!=shared_stage||get32(e.image.initial.data())!=0x7fd68)continue;
        unsigned routine=e.removed?255:e.image.bytes[5];auto &seen=local_dez3_scene[e.key];
        // Art loads repeat harmlessly but occupy the KosM queue; the owner ran them.
        bool replica=e.owner!=s.local_id;
        auto queue_art=[&](unsigned art,unsigned vram){if(replica)replay_kos_module(art,vram);};
        if(routine>=4&&!(seen&1)){seen|=1;TEST_BRIDGE("run_done");w8(0xee0a,0);}
        if(routine>=6&&!(seen&2)){seen|=2;TEST_BRIDGE("stop");w16(0xb018,0);w16(0xb01a,0);w16(0xb01c,0);w8(0xb020,5);}
        if(routine>=8&&!(seen&4)){seen|=4;TEST_BRIDGE("control");w8(0xb02e,0);}
        // The bridge piece (loc_80DD0) sets FAB8 bit 1 together with
        // Events_fg_5, which the background event's first step waits for. The
        // bit arrives through the scene journal; the event flag did not, so a
        // replica's background fell behind and later ran its steps out of order
        // (a runaway plane draw overwrote RAM). Supply it while step 0 waits.
        if((r8(0xfab8)&2)&&!(seen&512)){seen|=512;if(r16(0xeec2)==0){TEST_BRIDGE("bridge_event");w8(0xeec6,0xff);}}
        // loc_810A0 clamps this camera to the arena and moves the background
        // event's target from $6C0 to $2C0; the background state machine waits
        // on that sequence. Its creator can finish it within the frame it was
        // created, leaving no shared record to copy, so start one here once
        // the shared FAB8 bit 2 arrives if this client never ran it.
        if((r8(0xfab8)&4)&&routine<0x12&&!(seen&256)){seen|=256;
            bool running=false;for(unsigned i=3;i<93;i++)if(r32(slot_address(i))==0x810a0)running=true;
            if(!running&&r16(0xeed2)==0x6c0){unsigned obj=0;for(unsigned i=3;i<93;i++)if(!r32(slot_address(i))){obj=slot_address(i);break;}
                if(obj){TEST_BRIDGE("arena_clamp");for(unsigned j=0;j<74;j++)w8(obj+j,0);auto &local=native_entities[(obj-0xb000)/74];local=NativeEntity{};local.personal=true;local.initial_code=0x810a0;w32(obj,0x810a0);}else seen&=~256u;}}
        // Fingers destroyed (loc_80030): freeze this camera where it stands.
        if(routine>=0x12&&!(seen&8)){seen|=8;TEST_BRIDGE("camera_lock");w16(0xee16,r16(0xee78));w16(0xee14,r16(0xee78));}
        // Chest sinks (loc_80058): the background event's scroll target.
        if(routine>=0x14&&!(seen&16)){seen|=16;TEST_BRIDGE("bg_6c0");w16(0xeed2,0x6c0);}
        // Head rises (loc_80082): lock scrolling, load the Master Emerald art.
        if(routine>=0x16&&!(seen&32)){seen|=32;TEST_BRIDGE("emerald_art");w8(0xee0a,0xff);queue_art(0x182be6,0x9a00);}
        // Escape (loc_8011E, root deletes itself): stop shaking, clear the
        // scroll target, load the crane and debris art for the escape ship.
        if(e.removed&&!(seen&64)){seen|=64;TEST_BRIDGE("removed");w16(0xeecc,0);w16(0xeed2,0);queue_art(0x1607d8,0x93a0);queue_art(0x182ed8,0x2000);}
        // Defeat (sub_80F3A, root continues at loc_80102): the escape ship's art.
        if((e.removed||get32(e.image.bytes.data())==0x80102)&&!(seen&128)){seen|=128;TEST_BRIDGE("ship_plc");
            if(replica)replay_plc(0x80f9e);}
    }
}
void shared_dez3_ending_pre(){
    // The escape ship (loc_80382) spawns a palette fade and, at loc_803D6,
    // waits for that fade's deletion before SaveGame and StartNewLevel. Its
    // link points at the creator's own fade, so replicas froze the ship and
    // never reached the party barrier. Each client runs its private fade
    // (local effect) and then performs the same native ending calls.
    if(r16(0xfe10)!=0x1700)return;
    for(const auto &entry:shared_world.entities()){
        const auto &e=entry.second;
        if(e.key.epoch!=s.epoch||e.key.stage!=shared_stage||get32(e.image.initial.data())!=0x80160)continue;
        // The creator deletes the ship as part of the same native step, so
        // latch the waiting state once seen.
        auto &ending=local_dez3_ending[e.key];if(ending.state&4)continue;
        if(get32(e.image.bytes.data())==0x803d6)ending.state|=1;
        if(!(ending.state&1))continue;
        bool fading=false;for(unsigned i=3;i<93;i++){unsigned code=r32(slot_address(i));if(code==0x85e64||code==0x85e74||code==0x85ee6)fading=true;}
        if(fading){ending.state|=2;continue;}
        // Without a visible fade (e.g. its creator was out of range), proceed
        // after the native fade's duration rather than waiting forever.
        if(!(ending.state&2)&&++ending.frames<8*4*8)continue;
        ending.state|=4;
        M68KState saved=g_cpu;w8(0xef56,0xff);recomp_call_addr(0xc434);g_cpu=saved;
        g_cpu.D[0]=(g_cpu.D[0]&0xffff0000u)|shared_party_ending();recomp_call_addr(0x85e3a);g_cpu=saved;
    }
}
// Doomsday's ending runs on each client's own boss object (loc_81CA4:
// SaveGame, StartNewLevel($D01)). Doomsday rebases level coordinates per
// client as each camera passes $7400, so a client that rebased while others
// fought on can lose sight of the shared boss and never run that ending,
// holding the whole party at the barrier. Once others wait there, perform the
// same native ending calls after a grace period.
unsigned ddz_ending_epoch=~0u,ddz_ending_frames=0;
void shared_ddz_ending_pre(){
    if(r16(0xfe10)!=0xc00||s.waiting||!arrived_count()){ddz_ending_frames=0;return;}
    if(ddz_ending_epoch==s.epoch)return;
    if(++ddz_ending_frames<8*60)return;
    ddz_ending_epoch=s.epoch;
    M68KState saved=g_cpu;w8(0xef56,0xff);recomp_call_addr(0xc434);g_cpu=saved;
    g_cpu.D[0]=(g_cpu.D[0]&0xffff0000u)|0xd01;recomp_call_addr(0x85e3a);g_cpu=saved;
}
void shared_lrz_claim_post(){
    // A Lava Reef platform claims the mine arena with a one-time global write
    // (st FAA9, loc_79D82) before switching to loc_79DC2. Replicas still run
    // native code, so a replica can claim and then have its slot reverted to
    // the authority's descending state, leaving FAA9 set with no claimant.
    // No platform would ever set FA88 bit 0. Derive the latch from the
    // reconciled slots until that bit exists: only a running claimant holds it.
    if(r16(0xfe10)!=0x1600||(r8(0xfa88)&1))return;
    bool claimed=false;
    for(unsigned i=3;i<93;i++){unsigned code=r32(slot_address(i));if(code==0x79dc2||code==0x79dea)claimed=true;}
    w8(0xfaa9,claimed?0xff:0);
}
void shared_ddz_launcher_post(){
    // The missile launcher (loc_81F94) fires a volley of three when its live
    // count $39 is negative; each missile decrements it when it ends. Those
    // decrements come from missiles that may belong to other owners, so the
    // launcher's owner recounts its live missiles instead.
    if(r16(0xfe10)!=0xc00)return;
    for(unsigned i=3;i<93;i++){unsigned launcher=slot_address(i);const auto &local=native_entities[i];
        if(!r32(launcher)||local.initial_code!=0x81f94||!local.key.id)continue;
        auto *e=shared_world.find(local.key);if(!e||e->owner!=s.local_id)continue;
        unsigned live=0;
        for(unsigned j=3;j<93;j++){unsigned m=slot_address(j);if(r32(m)&&native_entities[j].initial_code==0x81fee&&r16(m+0x46)==launcher)live++;}
        for(const auto &entry:shared_world.entities()){const auto &m=entry.second;
            if(m.key.epoch==s.epoch&&m.key.stage==shared_stage&&!m.removed&&m.owner>=0&&m.image.parent==local.key.id&&get32(m.image.initial.data())==0x81fee&&!local_entity_address(m.key.id))live++;}
        w8(launcher+0x39,live?live-1:0xff);
    }
}
void shared_dez3_hatch_post(){
    // The final boss's chest is exposed while FAA9 is negative. The head's
    // hatch child writes FAA9 only in its transition frames: 1 when created
    // and while opening (loc_80634), $80 once open (loc_8067A), 1 while
    // closing (loc_806A6), 0 when gone. Those writes land on whichever client
    // owns the hatch at that instant, so the chest's owner could read a stale
    // phase and never become vulnerable again. FAA9 is a pure function of the
    // hatch's shared state; derive it after reconciliation on every client.
    if(r16(0xfe10)!=0x1700)return;
    // The hatch need not be loaded on every client, so read its phase from
    // the shared record (a local copy, when present, is at least as fresh).
    unsigned value=0;bool hatch=false;
    auto phase=[&](unsigned code){
        if(code==0x8060c||code==0x80634||code==0x8067a||code==0x806a6)hatch=true;
        if(code==0x8067a)value=0x80;else if((code==0x8060c||code==0x80634||code==0x806a6)&&value!=0x80)value=1;};
    for(unsigned i=3;i<93;i++)phase(r32(slot_address(i)));
    for(const auto &entry:shared_world.entities()){const auto &e=entry.second;
        if(e.key.epoch!=s.epoch||e.key.stage!=shared_stage||e.removed||get32(e.image.initial.data())!=0x8060c||local_entity_address(e.key.id))continue;
        phase(get32(e.image.bytes.data()));}
    w8(0xfaa9,value);
    // Children report to their parents by writing into them: a dying finger
    // sets bit (subtype/2) of its hand's $3A (sub_810FA), the struck head sets
    // the root's $38 bit 2 and the closing hatch clears it. When the writer
    // and its parent belong to different owners at that instant (ownership
    // follows contact), the write lands on a replica and is reverted, so a
    // hand never sees its third finger and the root never leaves the finger
    // phase. Reproduce those links from shared state on the parent's owner.
    for(const auto &entry:shared_world.entities()){const auto &e=entry.second;
        if(e.key.epoch!=s.epoch||e.key.stage!=shared_stage||get32(e.image.initial.data())!=0x80ce2||!e.image.parent)continue;
        if(e.removed||get32(e.image.bytes.data())==0x80d64)dez3_finger_bits[e.image.parent]|=1u<<((e.image.initial[0x2c]>>1)&7);
    }
    for(unsigned i=3;i<93;i++){unsigned obj=slot_address(i);const auto &local=native_entities[i];if(!r32(obj)||!local.key.id)continue;
        auto *e=shared_world.find(local.key);if(!e||e->owner!=s.local_id)continue;
        unsigned initial=get32(e->image.initial.data());
        if(initial==0x80b22){auto bits=dez3_finger_bits.find(local.key.id);if(bits!=dez3_finger_bits.end())w8(obj+0x3a,r8(obj+0x3a)|bits->second);}
        if(initial==0x7fd68&&e->image.bytes[5]>=0x16)w8(obj+0x38,hatch?(r8(obj+0x38)|4):(r8(obj+0x38)&~4));
    }
}
void shared_pre() {
#if OA_TEST_CONTROL
    test_fill_check("pre",0);if(r16(0xfe10)==0x1700)test_ring_record();
#endif
    if(!world_play())return;shared_select();
    shared_transition_pre();
    terrain_pre();
    shared_phase_pre();
    scene_pre();
    shared_lrz_scene_pre();
    shared_dez3_scene_pre();
    shared_dez3_ending_pre();
    shared_ddz_ending_pre();
    shared_rings_pre();
    for(unsigned i=3;i<93;i++)if(r32(slot_address(i)))identify_entity(slot_address(i));
    slot_rebuild();
    shared_spawn_nearby();
    shared_goal_pre();
    shared_chase_pre();
    shared_effect_pre();
    shared_dez_exit_pre();
    shared_dez_capsule_pre();
    for(unsigned i=3;i<93;i++) {unsigned obj=slot_address(i);if(!r32(obj))continue;auto key=identify_entity(obj);if(!key.id)continue;
        // TouchResponse sets a monitor's break routine after that object has
        // run. Submit this local contact before a canonical update can replace
        // it on the next frame.
        if(r32(obj)==0x1d566&&r8(obj+5)==4)request_pickup(obj,1);
        if((r32(obj)==0x1a64a||r32(obj)==0x1a662)&&r8(obj+5)==4)request_pickup(obj,5);
        if(r32(obj)==0x1a51a&&r8(obj+5)==4)request_pickup(obj,6);
        auto *entity=shared_world.find(key);if(entity&&!local_effect_code(get32(entity->image.initial.data()))&&(entity->owner!=s.local_id||entity->removed||native_entities[i].generation!=entity->generation))apply_entity(obj,*entity);
        if(r32(obj)==0x2cfa8&&r8(obj+5)==4&&(r8(obj+0x2c)&127)>(r8(0xfe2a)&127)&&abs(int(r16(obj+16))-int(r16(0xb010)))<9&&std::abs(adventure::wrapped_delta(float(int(r16(obj+20))-int(r16(0xb014))),float(local_y_period())))<64){M68KState saved=g_cpu;g_cpu.A[0]=0xff0000|obj;recomp_call_addr(0x2d164);g_cpu=saved;}
    }
    shared_lrz_claim_post();
    shared_dez3_hatch_post();
    shared_ddz_launcher_post();
    for(auto &claim:pickup_claims)if(!claim.applied&&(claim.kind<3||claim.kind>=5)) {
        auto *entity=shared_world.find(claim.key);
        if(!entity||!entity->consumed||entity->collector!=s.local_id||entity->removed)continue;
        unsigned obj=local_entity_address(claim.key.id);if(!obj)continue;
        claim.applied=true;applying_pickup=true;M68KState saved=g_cpu;
        if(claim.kind==1){
            // The shared broken animation can arrive before the grant. The
            // icon still needs the original item type, stored in subtype.
            w8(obj+0x20,r8(obj+0x2c));g_cpu.A[0]=0xff0000|obj;recomp_call_addr(0x1d716);
        }
        else if(claim.kind==2){g_cpu.A[0]=0xffb000;g_cpu.A[1]=0xff0000|obj;recomp_call_addr(0x1019a);}
        else if(claim.kind==5||claim.kind==6){g_cpu.A[0]=0xff0000|obj;w8(obj+5,4);recomp_call_addr(claim.kind==5?0x1a7c2:0x1a572);}
        g_cpu=saved;applying_pickup=false;send_entity_report(obj,true);
    }
}
unsigned shared_prune_at=0;
void shared_prune(){
    // Transient children (projectiles, debris, sparks) leave a record when
    // they expire. A destroyed or orphaned child cannot return, so once
    // nobody has reported it for a while and no live record links to it,
    // forget it.
    // Without this a long fight (the Death Egg robot sheds bridge debris for
    // as long as it walks) fills the world to its capacity, after which new
    // objects are no longer shared at all. Layout objects, rings, roots and
    // consumed or goal records are permanent.
    unsigned now=SDL_GetTicks();if(now-shared_prune_at<1000)return;shared_prune_at=now;
    std::set<uint64_t> linked;
    for(const auto &entry:shared_world.entities()){const auto &e=entry.second;if(e.removed)continue;
        for(unsigned i=0;i<14;i++){if(e.image.reference_mask&(1u<<i))linked.insert(e.image.references[i]);if(e.image.initial_reference_mask&(1u<<i))linked.insert(e.image.initial_references[i]);}
        if(e.image.parent)linked.insert(e.image.parent);}
    std::vector<adventure::EntityKey> stale;
    for(const auto &entry:shared_world.entities()){const auto &e=entry.second;unsigned space=unsigned(e.key.id>>60);
        if(!e.image.parent||space==1||space==2||e.owner>=0||e.consumed||e.goal_open||now-e.touched<30000||linked.count(e.key.id))continue;
        // A merely ownerless part of a live creator may still be needed (the
        // Death Egg robot's head is never drawn and links to nothing). Forget
        // destroyed records, or records whose creator is itself gone.
        auto *creator=shared_world.find({e.key.epoch,e.key.stage,e.image.parent});
        if(!e.removed&&creator&&!creator->removed)continue;
        if(e.key.epoch==s.epoch&&e.key.stage==shared_stage&&local_entity_address(e.key.id))continue;
        stale.push_back(e.key);}
    for(const auto &key:stale){shared_world.erase(key);object_timelines.erase(key);for(auto &q:entity_outbox)q.erase(key);}
}
void shared_frame() {
    scene_frame();
    terrain_frame();
    if(s.host){shared_world.set_active(world_connected_mask());
        for(const auto &key:shared_world.handoff_all(SDL_GetTicks()))commit_entity(*shared_world.find(key),-1,true);}
    flush_entities();
    shared_prune();
    if(!world_play()){if(side_trip()||r8(0xf600)!=0x0c){native_entities={};shared_stage=~0u;}return;}shared_select();
    // Bind the full pool first so links can be translated independently of order.
    for(unsigned i=3;i<93;i++)if(r32(slot_address(i)))identify_entity(slot_address(i));
    for(unsigned i=3;i<93;i++) {unsigned obj=slot_address(i);auto &local=native_entities[i];
        if(r32(obj)){send_entity_report(obj);continue;}
        if(!local.key.id){local=NativeEntity{};continue;}auto *entity=shared_world.find(local.key);
        if(entity&&entity->owner==s.local_id){unsigned char p[32]{};put32(p,ENTITY_RETIRE);put32(p+4,local.key.epoch);put32(p+8,local.key.stage);put64(p+12,local.key.id);put32(p+20,unsigned(s.local_id));put32(p+24,entity->generation);put32(p+28,entity->consumed?1:0);
            if(s.host)shared_handle(MAX_CHANNELS,p,sizeof p);else send_packet(0,WORLD,p,sizeof p);}
        local=NativeEntity{};
    }
}
bool shared_handle(unsigned channel,const unsigned char *p,unsigned n) {
    if(n>=4&&(get32(p)==13||get32(p)==14))return scene_handle(channel,p,n);
    if(n>=4&&get32(p)==12)return shared_phase_handle(channel,p,n);
    if(n>=4&&(get32(p)==TERRAIN_REPORT||get32(p)==TERRAIN_COMMIT))return terrain_handle(channel,p,n);
    if(n<20)return false;unsigned type=get32(p);adventure::EntityKey key{get32(p+4),get32(p+8),get64(p+12)};
    if(!key.id||!valid_shared_stage(key.stage))return false;
    if(key.epoch!=s.epoch)return true; // In-flight updates during a level barrier.
    if(type==ENTITY_COMMIT){
        if(s.host||(n<84||n>ENTITY_BYTES))return false;adventure::WorldEntity entity;entity.key=key;entity.revision=get32(p+20);entity.generation=get32(p+24);entity.tick=get32(p+28);
        entity.owner=int32_t(get32(p+32));entity.collector=int32_t(get32(p+36));unsigned flags=get32(p+40);
        if(entity.owner< -1||entity.owner>=8||entity.collector< -1||entity.collector>=8||flags>7||!decode_entity_image(p+48,n-48,entity.image))return false;
        entity.consumed=(flags&1)!=0;entity.removed=(flags&2)!=0;entity.goal_open=(flags&4)!=0;entity.goal_x=uint16_t(get16(p+44));entity.goal_y=uint16_t(get16(p+46));if(shared_world.import(entity)){remember_object_motion(entity);shared_world.find(key)->touched=SDL_GetTicks();}return true;
    }
    if(!s.host||n<28)return false;unsigned who=get32(p+20);int sender=channel<MAX_CHANNELS?channels[channel].peer:s.local_id;
    if(who>=8||int(who)!=sender)return false;
    if(!s.players[who].pose_valid||s.players[who].pose.epoch!=key.epoch||s.players[who].pose.stage!=(key.stage&65535)||s.players[who].pose.actual!=(key.stage>>16)||(s.players[who].pose.flags&1))return true;
    shared_world.set_active(world_connected_mask());
    if(type==11){
        if(n!=32)return false;auto *entity=shared_world.find(key);if(!entity||!shared_goal(*entity)||entity_distance(who,entity->image)>512u*512u)return true;
        if(!entity->goal_open){entity->goal_open=true;entity->goal_x=uint16_t(get16(entity->image.bytes.data()+16));entity->goal_y=uint16_t(get16(entity->image.bytes.data()+20));entity->revision++;}
        commit_entity(*entity);return true;
    }
    if(type==ENTITY_REPORT){
        if((n<84||n>ENTITY_BYTES))return false;adventure::ObjectImage image;if(!decode_entity_image(p+48,n-48,image))return false;
        if(auto *boss=world_boss(key.epoch,key.stage,key.id))if((!boss->kind||boss->kind==6)&&image.bytes[0x29]>boss->hp){
            auto &b=image.bytes;if(b[0x28])b[0x25]=b[0x28];b[0x28]=0;b[0x29]=uint8_t(boss->hp);
            if(!boss->hp)b[0x2a]|=0x80;
        }
        auto *existing=shared_world.find(key);bool fresh=existing==nullptr;int previous_owner=existing?existing->owner:-2;
        if(fresh&&image.parent){auto parent=key;parent.id=image.parent;auto *source=shared_world.find(parent);if(source&&source->owner>=0&&source->owner!=int(who))return true;}
        if(get32(p+40)>1)return false;
        auto *entity=shared_world.observe(who,key,image,SDL_GetTicks(),get32(p+36),get32(p+40)!=0);if(!entity)return true;
        bool handed_off=false; // The frame transfers linked assemblies together.
        // Observing an ownerless (retired) entity assigns it to this reporter.
        // Publish that assignment: the reporter keeps its copy reconciled to a
        // stale image, and its reports keep the old generation, until it learns.
        if(!fresh&&previous_owner<0&&entity->owner==int(who)){commit_entity(*entity,-1,true);return true;}
        unsigned generation=get32(p+24);if(!generation&&fresh&&entity->owner==int(who))generation=entity->generation;
        if(handed_off&&entity->owner==int(who)&&get32(p+40))generation=entity->generation;
        bool changed=shared_world.update(who,key,generation,get32(p+28),image);
        if(changed||fresh||handed_off)remember_object_motion(*entity);
        if(changed||fresh||handed_off||!get32(p+24)||generation!=get32(p+24))commit_entity(*entity);
        return true;
    }
    if(type==ENTITY_CLAIM){
        if(n!=32||get32(p+24)<1||get32(p+24)>6)return false;auto *entity=shared_world.find(key);if(!entity)return true;
        if(entity_distance(who,entity->image)>128u*128u)return true;
        if(shared_world.consume(who,key)==adventure::WorldClaim::granted){
            entity->owner=int(who);entity->generation++;entity->revision++;entity->tick=0;
            if(get32(p+24)==1){put32(entity->image.bytes.data(),0x1d61e);entity->image.bytes[0x20]=10;entity->image.bytes[0x28]=0;}
            else if(get32(p+24)==2){put32(entity->image.bytes.data(),0x1e5e0);entity->image.bytes[5]=0;entity->image.bytes[0x28]=0;entity->image.bytes[0x2a]|=0x80;}
            else if(get32(p+24)>=5){entity->image.bytes[5]=6;entity->image.bytes[0x28]=0;put16(entity->image.bytes.data()+8,0x80);}
        }
        commit_entity(*entity);return true;
    }
    if(type==ENTITY_RETIRE){
        if(n!=32||get32(p+28)>1)return false;if(shared_world.retire(who,key,get32(p+24),get32(p+28)!=0)){auto *e=shared_world.find(key);e->touched=SDL_GetTicks();commit_entity(*e);}return true;
    }
    return false;
}

int shared_object_before(){
    // Host video ticks can suspend the native CPU inside an object routine.
    // Replacing its slot there mixes the old code/registers with a new state
    // (e.g. Obj_Wait then jumps through an explosion's empty callback).
    // Apply shared RAM only before the native loop starts processing players.
    if((g_cpu.A[0]&65535)==0xb000&&s.phase==PLAY){
        world_pre_frame();carry_local_frame();
    }
#if OA_TEST_CONTROL
    test_fill_check("before",g_cpu.A[0]&65535);test_object_code=r32(g_cpu.A[0]&65535);
#endif
    if((s.phase!=PLAY&&s.phase!=INTRO)||side_trip()||r8(0xf600)!=0x0c)return 0;shared_select();unsigned obj=g_cpu.A[0]&65535;
    if(!native_slot(obj)||!r32(obj))return 0;auto key=identify_entity(obj);if(!key.id)return 0;
    auto &local=native_entities[(obj-0xb000)/74];auto *entity=shared_world.find(key);
    // Each client runs a private exit controller instead (shared_dez_exit_pre).
    if(r32(obj)==0x7f2dc||r32(obj)==0x7f2fe){online_host->tail_call(0x1ab04);return 1;}
    if(entity&&local.initialized&&(entity->owner!=s.local_id||local.generation!=entity->generation)&&!entity_links_ready(entity->image)){
        // A running multipart state must never execute with an absent link.
        // Retain its last safe presentation while the graph arrives.
        M68KState saved=g_cpu;recomp_call_addr(0x1abc6);g_cpu=saved;online_host->tail_call(0x1ab04);return 1;
    }
    if(entity&&local.initialized&&entity->owner!=s.local_id&&(shared_goal(*entity)||get32(entity->image.initial.data())==0x1a84e)){
        M68KState saved=g_cpu;recomp_call_addr(0x1abc6);g_cpu=saved;online_host->tail_call(0x1ab04);return 1;
    }
    replica_guard.obj=0;replica_guard.count=0;
    if(entity&&entity->owner!=s.local_id&&s.phase==PLAY){
        for(unsigned i=0;i<14&&replica_guard.count<replica_guard.slots.size();i++){
            unsigned address=r16(obj+0x2e+i*2);if(!native_slot(address)||address==obj||!r32(address))continue;
            bool known=false;for(unsigned j=0;j<replica_guard.count;j++)known|=replica_guard.slots[j]==address;if(known)continue;
            const auto &peer=native_entities[(address-0xb000)/74];if(!peer.key.id||peer.personal)continue;
            auto *owned=shared_world.find(peer.key);if(!owned||owned->owner!=s.local_id||owned->removed)continue;
            replica_guard.slots[replica_guard.count]=address;memcpy(replica_guard.bytes[replica_guard.count].data(),g_ram+address,74);replica_guard.count++;
        }
        if(replica_guard.count)replica_guard.obj=obj;
    }
    if((r32(obj)==0x1a64a||r32(obj)==0x1a662)&&r8(obj+5)==2){
        if(r8(obj+0x41)!=0xa7){w8(obj+0x40,255);w8(obj+0x41,0xa7);}
        ring_timer_object=obj;ring_timer_saved=r8(0xfeb6);w8(0xfeb6,r8(obj+0x40));if(r8(obj+0x40))w8(obj+0x40,r8(obj+0x40)-1);
    }
    return 0;
}
void shared_object_after(){
#if OA_TEST_CONTROL
    test_fill_check("after",g_cpu.A[0]&65535);
#endif
    if(ring_timer_object){w8(0xfeb6,ring_timer_saved);ring_timer_object=0;}
    if(replica_guard.obj&&replica_guard.obj==(g_cpu.A[0]&65535)){
        for(unsigned j=0;j<replica_guard.count;j++){unsigned address=replica_guard.slots[j];
            for(unsigned i=0;i<74;i++)if(g_ram[address+i]!=replica_guard.bytes[j][i])w8(address+i,replica_guard.bytes[j][i]);}
    }
    replica_guard.obj=0;replica_guard.count=0;
    // The final slot in the complete 110-object pool, including equipment.
    // Publish completed native state, never a half-written initializer.
    if((g_cpu.A[0]&65535)==0xcf82&&s.phase==PLAY){
        carry_after_frame();world_frame();
    }
    if(!world_play())return;unsigned obj=g_cpu.A[0]&65535;
    // Obj_WaitOffscreen takes several native frames before the actual object's
    // initializer runs. Importing a running routine earlier skips DPLC setup.
    if(native_slot(obj)&&r32(obj)&&r32(obj)!=0x85ad2&&r32(obj+0xc)!=0x85b08){
        auto &local=native_entities[(obj-0xb000)/74];
        // Lava Reef's flash/countdown precedes its real physical initializer.
        // Importing a running image earlier skips local art, control unlocks
        // and the native child setup entirely.
        unsigned code=r32(obj);
        if(local.initial_code==0x78f56&&(code==0x78f82||code==0x78fae))return;
        local.initialized=true;
        if(!local.classified){identify_entity(obj);local.classified=true;}
    }
    if(native_slot(obj)&&(r32(obj)==0x1a64a||r32(obj)==0x1a662)&&r8(obj+5)==2){
        auto &local=native_entities[(obj-0xb000)/74];
        // A peer must instantiate the flying ring, not rerun the spill's
        // initializer, which subtracts the victim's personal ring inventory.
        if(local.key.id&&local.seed.initial[5]==0){memcpy(local.seed.initial.data(),g_ram+obj,74);local.seed_ready=false;}
    }
}
void shared_object_delete(){
    unsigned obj=g_cpu.A[1]&65535;if(!native_slot(obj))return;
    auto &local=native_entities[(obj-0xb000)/74];
    if(world_play()&&local.key.id){
        auto *entity=shared_world.find(local.key);
        if(entity&&entity->owner!=s.local_id&&!entity->removed)local_deleted[local.key]=SDL_GetTicks();
        if(entity&&entity->owner==s.local_id){
            unsigned char p[32]{};put32(p,ENTITY_RETIRE);put32(p+4,local.key.epoch);put32(p+8,local.key.stage);put64(p+12,local.key.id);
            unsigned initial_respawn=get16(local.seed.initial.data()+0x48);
            // Go_Delete_Sprite leaves Delete_Current_Sprite as the routine: a deliberate
            // destruction even when it happens offscreen (debris, spent shots).
            bool destroyed=entity->consumed||(r8(obj+4)&0x80)||r32(obj)==0x1abb6||(initial_respawn>=0xeb00&&initial_respawn<0xee00&&!r16(obj+0x48));
            put32(p+20,unsigned(s.local_id));put32(p+24,entity->generation);put32(p+28,destroyed?1:0);
            if(s.host)shared_handle(MAX_CHANNELS,p,sizeof p);else if(channels[0].tx_size<16384)send_packet(0,WORLD,p,sizeof p);
        }
    }
    local=NativeEntity{};
}
void shared_object_allocated(){
    if((s.phase!=PLAY&&s.phase!=INTRO)||side_trip()||r8(0xf600)!=0x0c||!(g_cpu.SR&4))return;
    unsigned obj=g_cpu.A[1]&65535,parent=g_cpu.A[0]&65535;
    if(!native_slot(obj)||r32(obj))return;
    auto &local=native_entities[(obj-0xb000)/74];local=NativeEntity{};
    if(!native_slot(parent)){
        // Scripted bosses and event objects often spawn relative to a camera.
        // Their identity must come from the native creation site, not a local
        // camera coordinate. Player-created objects have a separate namespace.
        unsigned callsite=r32(g_cpu.A[7]&65535)&0xffffff;if((callsite&1)||callsite>=sizeof(g_rom))return;
        uint64_t source=uint64_t(callsite)|(parent==0xb000||parent==0xb04a?(uint64_t(s.local_id+1)<<32):0);
        unsigned serial=++root_allocations[{shared_stage_id(),source}];
        local.key={s.epoch,shared_stage_id(),entity_hash(source,serial)};
        if(auto *existing=shared_world.find(local.key)){
            // A remote event may already have supplied this root. Native
            // allocation failure prevents a second boss/sign from replacing it.
            // When that root is absent here, keep this native allocation and
            // bind it to the shared identity instead. Event code allocates
            // once without retrying, and its initializer publishes stage-wide
            // pointers (e.g. Lava Reef's mine boss in FAA4) that a camera-
            // distance spawn of an invisible controller would never restore.
            if(existing->removed||local_entity_address(local.key.id)){
                --root_allocations[{shared_stage_id(),source}];local=NativeEntity{};
                g_cpu.SR&=~4u;g_cpu.D[0]=(g_cpu.D[0]&0xffff0000u)|0xffffu;
            }
        }
        return;
    }
    auto key=identify_entity(parent);
    // Doomsday's private flight controller creates private presentation.
    // (Other personal sequencers, e.g. Sky Sanctuary's entrance, deliberately
    // spawn shared world objects such as the Knuckles NPC.)
    if(!key.id){const auto &owner=native_entities[(parent-0xb000)/74];
        if(owner.personal&&owner.initial_code>=0x81492&&owner.initial_code<0x817da){local.personal=true;local.initial_code=0x81492;}
        // Hidden Palace's per-client story sequence creates per-client objects.
        if(owner.personal&&owner.initial_code==0x63d1a){local.personal=true;local.initial_code=0x63d1a;}
        return;}
    auto &source=native_entities[(parent-0xb000)/74];auto *authority=shared_world.find(key);
    if(source.initialized&&authority&&authority->owner>=0&&authority->owner!=s.local_id){
        // Only the authority creates continuing projectiles/children. Replicas
        // still run their first initializer locally to allocate native art.
        g_cpu.SR&=~4u;g_cpu.D[0]=(g_cpu.D[0]&0xffff0000u)|0xffffu;return;
    }
    local.key={key.epoch,key.stage,entity_hash(key.id,++source.children)};local.seed.parent=key.id;
}
// Guard the native art queues against replays. A replica re-runs a one-time
// transition every frame until the owner's next image arrives (e.g. a boss
// whose committed defeat bit precedes its new routine), queueing the same
// pieces again each time. Load_PLC/Load_PLC_Raw and Queue_Kos_Module scan for
// a free entry without a bound; overflowing into the decompressors' state
// corrupted the whole of RAM. Already-queued art and full queues are skipped.
bool plc_redundant(unsigned list){
    if(list+2>sizeof(g_rom))return false;unsigned header=get16(g_rom+list);if(header&0x8000)return false;
    unsigned pieces=header+1;if(list+2+pieces*6>sizeof(g_rom))return false;bool all=true;
    for(unsigned i=0;i<pieces;i++)if(!art_queued(0xf680,16,get32(g_rom+list+2+i*6)))all=false;
    return all||art_free(0xf680,16)<pieces;
}
int shared_plc_raw_hook(){return s.phase!=OFF&&plc_redundant(g_cpu.A[1]&0xffffff)?1:0;}
int shared_plc_hook(){
    if(s.phase==OFF)return 0;unsigned index=g_cpu.D[0]&0xffff;if(0x9238c+index*2+2>sizeof(g_rom))return 0;
    return plc_redundant(0x9238c+unsigned(int16_t(get16(g_rom+0x9238c+index*2))))?1:0;
}
int shared_kos_module_hook(){
    if(s.phase==OFF)return 0;unsigned art=g_cpu.A[1]&0xffffff;
    return art_queued(0xff64,4,art)||!art_free(0xff64,4)?1:0;
}
int shared_instruction(unsigned pc){
    switch(pc){
    case 0x1aafc:return shared_object_before();
    case 0x1ab04:shared_object_after();break;
    case 0x1abb8:shared_object_delete();break;
    case 0x1bb14:shared_object_allocated();break;
    case 0x1d716:return shared_monitor_hook();
    case 0x1019a:return shared_badnik_hook();
    case 0xeac6:return shared_ring_hook(false);
    case 0xeaf0:return shared_ring_hook(true);
    case 0xeb86:shared_ring_window();break;
    case 0xebce:return shared_ring_draw_guard();
    case 0x1a7c2:return shared_object_ring_hook(5);
    case 0x1a572:return shared_object_ring_hook(6);
    case 0x2dad0:return shared_results_hook();
    case 0x4fc0e:return shared_fire_hook();
    case 0x53886:return shared_ice_hook();
    case 0x6040:return shared_section_load_hook();
    case 0x1bf74:return shared_section_start_hook();
    case 0x2dc30:return shared_result_signal_hook();
    case 0x85e3a:return shared_transition_hook();
    case 0x16fa:return shared_plc_hook();
    case 0x172c:return shared_plc_raw_hook();
    case 0x1ad2:return shared_kos_module_hook();
    case 0x82b62:return shared_ddz_missile_hit();
    }
    return 0;
}
bool shared_debug(int id,const char *action){
    if(strcmp(action,"world_entities"))return false;
    char out[30000];unsigned n=unsigned(snprintf(out,sizeof out,"{\"id\":%d,\"ok\":true,\"count\":%u,\"claims\":%u,\"entities\":[",id,unsigned(shared_world.entities().size()),shared_claims));
    bool first=true;
    for(const auto &entry:shared_world.entities()){
        const auto &e=entry.second;if(e.key.epoch!=s.epoch||e.key.stage!=shared_stage_id())continue;
        n+=unsigned(snprintf(out+n,sizeof out-n,"%s{\"key\":\"%llu\",\"obj\":%u,\"owner\":%d,\"collector\":%d,\"consumed\":%u,\"removed\":%u,\"code\":%u,\"x\":%u,\"y\":%u,\"rev\":%u}",first?"":",",(unsigned long long)e.key.id,local_entity_address(e.key.id),e.owner,e.collector,e.consumed,e.removed,get32(e.image.bytes.data()),get16(e.image.bytes.data()+16),get16(e.image.bytes.data()+20),e.revision));first=false;if(n>29000)break;
    }
    snprintf(out+n,sizeof out-n,"]}");cmd_send_response(out);return true;
}
