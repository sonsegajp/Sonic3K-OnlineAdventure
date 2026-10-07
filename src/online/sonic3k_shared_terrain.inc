void shared_redraw_foreground(){adventure::redraw_foreground(g_ram,online_host->vram);}
adventure::SharedTerrain shared_terrain;
std::array<std::map<adventure::TerrainKey,adventure::TerrainCell>,MAX_CHANNELS> terrain_outbox;
std::array<uint16_t,0x8000> terrain_before{};
struct TerrainChange {uint16_t base=0,value=0;};
std::map<unsigned,TerrainChange> terrain_pending;
unsigned terrain_epoch=~0u,terrain_stage=~0u,terrain_changes=0;
bool terrain_ready=false;
bool terrain_loading(){return r16(0xff0e)!=0||r8(0xff60)!=0;}
constexpr unsigned TERRAIN_REPORT=9,TERRAIN_COMMIT=10;
bool terrain_handle(unsigned,const unsigned char*,unsigned);
void terrain_observe(){
    auto check=[](unsigned a){unsigned m=adventure::SharedTerrain::mask(a),value=r16(a)&m;
        if(value!=terrain_before[a/2]){
            auto found=terrain_pending.find(a);
            if(found==terrain_pending.end())terrain_pending[a]={terrain_before[a/2],uint16_t(value)};
            else found->second.value=uint16_t(value);
        }
        terrain_before[a/2]=uint16_t(value);
    };
    for(unsigned a=0;a<0xa800;a+=2)check(a);
    for(unsigned a=0xeb00;a<0xee00;a+=2)check(a);
    for(unsigned a:adventure::SharedTerrain::environment)check(a);
}
void terrain_reset(){shared_terrain.clear();for(auto &q:terrain_outbox)q.clear();terrain_pending.clear();terrain_ready=false;terrain_epoch=terrain_stage=~0u;terrain_changes=0;}
void terrain_queue(const adventure::TerrainKey &key,const adventure::TerrainCell &cell,int channel=-1){
    for(unsigned i=1;i<MAX_CHANNELS;i++)if(channels[i].hello&&(channel<0||unsigned(channel)==i))terrain_outbox[i][key]=cell;
}
void terrain_snapshot(unsigned channel){for(const auto &entry:shared_terrain.cells())terrain_queue(entry.first,entry.second,int(channel));}
void terrain_pre(){
    // Native asset decompression uses the terrain itself as its dictionary.
    // Never rewrite it or broadcast half-loaded art while a load is in flight.
    if(!world_play()||terrain_loading()){terrain_ready=false;return;}
    unsigned stage=shared_stage_id();
    if(terrain_epoch!=s.epoch||terrain_stage!=stage){
        if(terrain_epoch!=s.epoch){shared_terrain.discard_before(s.epoch);for(auto &q:terrain_outbox)q.clear();}
        terrain_pending.clear();terrain_ready=false;terrain_epoch=s.epoch;terrain_stage=stage;
    }
    // Camera events also alter geometry outside the object loop. Capture
    // those native writes before applying incoming cells at this safe boundary.
    if(terrain_ready)terrain_observe();
    bool redraw=false;
    for(const auto &entry:shared_terrain.cells()){
        const auto &key=entry.first;if(key.epoch!=s.epoch||key.stage!=stage)continue;
        auto pending=terrain_pending.find(key.address);if(pending!=terrain_pending.end())continue;
        unsigned mask=adventure::SharedTerrain::mask(key.address);
        if(key.address<0xa800&&r16(key.address)!=entry.second.value)redraw=true;
        w16(key.address,(r16(key.address)&~mask)|entry.second.value);
    }
    if(redraw){
        // Geometry collision and the visible foreground must change together,
        // even when the receiving player's camera is standing still.
        shared_redraw_foreground();
    }
    for(unsigned a=0;a<0xa800;a+=2)terrain_before[a/2]=r16(a);
    for(unsigned a=0xeb00;a<0xee00;a+=2)terrain_before[a/2]=r16(a)&0x7f7f;
    for(unsigned a:adventure::SharedTerrain::environment)terrain_before[a/2]=r16(a)&adventure::SharedTerrain::mask(a);
    terrain_ready=true;
}
void terrain_frame(){
    if(world_play()&&!terrain_loading()&&terrain_ready&&terrain_epoch==s.epoch&&terrain_stage==shared_stage_id()){
        terrain_observe();
        if(!terrain_pending.empty()&&(s.host||channels[0].tx_size<8192)){
            unsigned char packet[20+128*6]{};put32(packet,TERRAIN_REPORT);put32(packet+4,s.epoch);put32(packet+8,terrain_stage);put32(packet+12,unsigned(s.local_id));unsigned count=0;
            for(auto i=terrain_pending.begin();i!=terrain_pending.end()&&count<128;){unsigned char *p=packet+20+count++*6;put16(p,i->first);put16(p+2,i->second.base);put16(p+4,i->second.value);i=terrain_pending.erase(i);}
            put32(packet+16,count);if(s.host)terrain_handle(MAX_CHANNELS,packet,20+count*6);else send_packet(0,WORLD,packet,20+count*6);
        }
    }else terrain_ready=false;
    if(s.host)for(unsigned i=1;i<MAX_CHANNELS;i++){
        auto &q=terrain_outbox[i];if(!channels[i].hello){q.clear();continue;}if(q.empty()||channels[i].tx_size>8192)continue;
        unsigned char packet[20+128*8]{};auto first=q.begin()->first;put32(packet,TERRAIN_COMMIT);put32(packet+4,first.epoch);put32(packet+8,first.stage);unsigned count=0;
        while(!q.empty()&&count<128){auto e=q.begin();if(e->first.epoch!=first.epoch||e->first.stage!=first.stage)break;unsigned char *p=packet+20+count++*8;put16(p,e->first.address);put16(p+2,e->second.value);put32(p+4,e->second.revision);q.erase(e);}
        put32(packet+16,count);send_packet(i,WORLD,packet,20+count*8);
    }
}
bool terrain_handle(unsigned channel,const unsigned char *p,unsigned n){
    if(n<20)return false;unsigned type=get32(p),epoch=get32(p+4),stage=get32(p+8),count=get32(p+16);
    if(!valid_shared_stage(stage)||count>128||!count)return false;if(epoch!=s.epoch)return true;
    if(type==TERRAIN_REPORT){
        if(!s.host||n!=20+count*6)return false;unsigned who=get32(p+12);int sender=channel<MAX_CHANNELS?channels[channel].peer:s.local_id;
        if(who>=MAX_PLAYERS||int(who)!=sender)return false;
        if(!active_pose(who)||s.players[who].pose.stage!=(stage&65535)||s.players[who].pose.actual!=(stage>>16)||(s.players[who].pose.flags&1))return true;
        for(unsigned i=0;i<count;i++)if(!adventure::SharedTerrain::mask(get16(p+20+i*6)))return false;
        for(unsigned i=0;i<count;i++){const auto *v=p+20+i*6;adventure::TerrainKey key{epoch,stage,get16(v)};auto *cell=shared_terrain.change(key,get16(v+2),get16(v+4));if(cell){terrain_queue(key,*cell);terrain_changes++;}}
        return true;
    }
    if(type==TERRAIN_COMMIT){
        if(s.host||n!=20+count*8)return false;
        for(unsigned i=0;i<count;i++){const auto *v=p+20+i*8;if(!adventure::SharedTerrain::mask(get16(v))||!get32(v+4))return false;}
        for(unsigned i=0;i<count;i++){const auto *v=p+20+i*8;shared_terrain.import({epoch,stage,get16(v)},{get32(v+4),uint16_t(get16(v+2))});}
        return true;
    }
    return false;
}
