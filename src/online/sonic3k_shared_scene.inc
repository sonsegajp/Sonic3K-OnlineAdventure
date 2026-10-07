// These native boss layouts use monotonic stage-wide phase bits, outside
// object RAM. Sharing a child's PC alone loses its one-time writes to them.
// Pointers, camera positions, player controls and repeating counters stay local.
// Packed bits: FAB8 in bits 8-15, FA88 in bits 0-7, and the Death Egg final
// boss's hand-destroyed flags FA82/FA83 (st when all three fingers of that
// hand are gone; the root waits for both) as bits 16/17. A hand writes its
// flag only on the client that owns it at that instant, while the root may be
// owned elsewhere by then.
struct SharedScene {unsigned epoch=0,stage=0,bits=0;};
std::map<std::pair<unsigned,unsigned>,SharedScene> shared_scenes;
unsigned scene_mask(unsigned stage){
    switch(stage>>16){
    case 0xb01:return 0x0300;
    case 0x1600:return 0x0303;
    case 0x1700:return 0x33f00;
    case 0xc00:return 0xff00;
    default:return 0;
    }
}
void scene_reset(){shared_scenes.clear();}
void scene_packet(const SharedScene &scene,int channel=-1){
    unsigned char p[24]{};put32(p,s.host?14:13);put32(p+4,scene.epoch);put32(p+8,scene.stage);put32(p+12,unsigned(s.local_id));put32(p+16,scene.bits);
    if(channel>=0)send_packet(unsigned(channel),WORLD,p,sizeof p);
    else if(s.host)broadcast(WORLD,p,sizeof p);else send_packet(0,WORLD,p,sizeof p);
}
void scene_snapshot(unsigned channel){for(const auto &entry:shared_scenes)scene_packet(entry.second,int(channel));}
void scene_pre(){
    unsigned stage=shared_stage_id(),mask=scene_mask(stage);if(!mask)return;
    auto it=shared_scenes.find({s.epoch,stage});if(it==shared_scenes.end())return;
    w8(0xfab8,r8(0xfab8)|uint8_t(it->second.bits>>8));w8(0xfa88,r8(0xfa88)|uint8_t(it->second.bits));
    if(it->second.bits&0x10000)w8(0xfa82,0xff);
    if(it->second.bits&0x20000)w8(0xfa83,0xff);
}
void scene_frame(){
    if(!world_play())return;unsigned stage=shared_stage_id(),mask=scene_mask(stage);if(!mask)return;
    auto &scene=shared_scenes[{s.epoch,stage}];scene.epoch=s.epoch;scene.stage=stage;
    unsigned bits=((r8(0xfab8)<<8)|r8(0xfa88)|(r8(0xfa82)?0x10000u:0)|(r8(0xfa83)?0x20000u:0))&mask;
    if(bits&~scene.bits){scene.bits|=bits;scene_packet(scene);}
}
bool scene_handle(unsigned channel,const unsigned char *p,unsigned n){
    if(n!=24)return false;unsigned type=get32(p),epoch=get32(p+4),stage=get32(p+8),who=get32(p+12),bits=get32(p+16);
    unsigned mask=scene_mask(stage);if(!mask||(bits&~mask)||!valid_shared_stage(stage))return false;
    if(epoch!=s.epoch)return true;
    if(s.host){
        if(type!=13||who>=MAX_PLAYERS||channel>=MAX_CHANNELS||channels[channel].peer!=int(who)||!world_pose_matches(who,epoch,stage))return false;
    }else if(type!=14||channel!=0)return false;
    auto &scene=shared_scenes[{epoch,stage}];scene.epoch=epoch;scene.stage=stage;
    bool changed=(bits&~scene.bits)!=0;scene.bits|=bits;if(changed&&s.host)scene_packet(scene);
    return true;
}
