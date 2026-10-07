// The native score screen signals a seamless act-layout replacement before
// its end-of-results callback. Keep that signal private until all players have
// reached the confirmed clear barrier, otherwise stragglers retain the old map.
unsigned shared_result_epoch=~0u,shared_result_stage=~0u;
int shared_result_signal_hook(){
    if(s.phase!=PLAY||side_trip())return 0;
    shared_result_epoch=s.epoch;shared_result_stage=shared_stage_id();
    online_host->tail_call(0x2dc34);return 1;
}
void shared_release_result_signal(unsigned cleared_epoch){
    if(shared_result_epoch==cleared_epoch&&shared_result_stage==shared_stage_id())w8(0xeec6,0xff);
    shared_result_epoch=shared_result_stage=~0u;
}
// Seamless layout replacement must obey the same confirmed party barrier as
// an act clear. AIZ's fire sequence rebases every object/player coordinate;
// allowing one client to cross early makes two incompatible maps share an act.
unsigned shared_fire_epoch=~0u;
bool shared_phase_waiting=false;
bool shared_fire_respawn=false;
unsigned shared_fire_committed=~0u;
SafeAnchor shared_fire_anchor{};
unsigned shared_ice_committed=~0u,shared_ice_control=0;
bool shared_ice_respawn=false,shared_ice_holding=false;
SafeAnchor shared_ice_anchor{};
void shared_transition_reset();
void shared_phase_reset(){shared_transition_reset();shared_result_epoch=shared_result_stage=~0u;shared_fire_epoch=shared_fire_committed=shared_ice_committed=~0u;shared_phase_waiting=shared_fire_respawn=shared_ice_respawn=shared_ice_holding=false;shared_fire_anchor=shared_ice_anchor={};}
void shared_ice_restore_anchor(){
    s.restore_anchor=shared_ice_anchor;s.restore_actual=0x501;s.restore_epoch=s.epoch;
    for(unsigned who=0;who<MAX_PLAYERS;who++){
        const auto &v=s.players[who];const auto &p=v.pose;
        if(int(who)!=s.local_id&&v.connected&&v.pose_valid&&p.epoch==s.epoch&&p.actual==0x501&&p.stage==0x500&&p.routine==2&&!(p.flags&3)){
            s.restore_anchor=(p.safe.valid&&p.safe.stage==0x500&&p.safe.actual==0x501)?p.safe:SafeAnchor{true,0x500,p.x,p.y,0x501};break;
        }
    }
    s.restore_pending=true;s.restore_loading=false;
}
int shared_section_load_hook(){
    if(s.phase!=PLAY||side_trip())return 0;
    if(shared_ice_committed==s.epoch&&(r16(0xfe10)==0x500||r16(0xfe10)==0x501))shared_ice_restore_anchor();
    if(s.restore_pending&&s.restore_anchor.valid&&s.restore_actual==0x501&&s.restore_anchor.stage==0x500){
        // This precedes art selection and Get_LevelSizeStart. A late warp
        // would already have spawned the act-2 entrance boss/camera lock.
        w16(0xfe10,0x501);w16(0xee4e,0x500);w8(0xfe2a,0);w8(0xfe48,0);
    }
    return 0;
}
int shared_section_start_hook(){
    if(s.phase!=PLAY||!s.restore_pending||!s.restore_anchor.valid||s.restore_actual!=0x501||s.restore_anchor.stage!=0x500||r16(0xfe10)!=0x501)return 0;
    w16(0xee4e,0x500);w16(0xb010,s.restore_anchor.x);w16(0xb014,s.restore_anchor.y);
    g_cpu.D[1]=s.restore_anchor.x;g_cpu.D[0]=s.restore_anchor.y;
    w16(0xee14,0);w16(0xee0c,0);w16(0xee16,0x7000);w16(0xee0e,0x7000);
    w16(0xee18,0);w16(0xee10,0);w16(0xee1a,0xb20);w16(0xee12,0xb20);
    return 0;
}
void shared_restore_section(){
    if(s.restore_actual==0x501&&s.restore_anchor.stage==0x500&&s.restore_anchor.valid){
        shared_ice_committed=s.epoch;shared_ice_anchor=s.restore_anchor;shared_ice_respawn=false;
        // Act 1 uses the beginning of the native act-2 map after the indoor
        // transition. Reloading that map must not use the act-2 start bounds.
        w8(0xee33,0);w16(0xee14,0);w16(0xee0c,0);w16(0xee16,0x7000);w16(0xee0e,0x7000);
        w16(0xee18,0);w16(0xee10,0);w16(0xee1a,0xb20);w16(0xee12,0xb20);
        return;
    }
    if(s.restore_actual!=1||s.restore_anchor.stage!=0||!s.restore_anchor.valid)return;
    shared_fire_committed=s.epoch;shared_fire_anchor=s.restore_anchor;shared_fire_respawn=false;
    // A plain act-2 reload may already have chosen the post-miniboss camera
    // limits. The shared burned act-1 section starts before that arena.
    w8(0xee33,0);w16(0xee14,0x10);w16(0xee0c,0x10);
    w16(0xee18,0);w16(0xee10,0);w16(0xee1a,0x260);w16(0xee12,0x260);
}
void shared_phase_recover(){
    // A death while the flames are rising can reload the pre-fire checkpoint.
    // The party already committed the replacement layout; keep that decision
    // and respawn this player in the rebased section without another barrier.
    if(s.phase!=PLAY||side_trip()||s.start_pending)return;
    if(shared_ice_committed==s.epoch){
        if(r8(0xf600)==0x8c||(r8(0xf600)==0x0c&&r8(0xb005)>=6&&r8(0xb005)<=8))shared_ice_respawn=true;
        if(s.restore_pending||r8(0xf600)!=0x0c||!r8(0xf711)||r8(0xb005)!=2)return;
        if(shared_ice_respawn||r16(0xfe10)==0x500||(r16(0xfe10)==0x501&&r16(0xee4e)!=0x500)){
            shared_ice_restore_anchor();
        }
        return;
    }
    if(shared_fire_committed!=s.epoch)return;
    if(r8(0xf600)==0x8c||(r8(0xf600)==0x0c&&r8(0xb005)>=6&&r8(0xb005)<=8))shared_fire_respawn=true;
    if(s.restore_pending||r8(0xf600)!=0x0c||!r8(0xf711)||r8(0xb005)!=2)return;
    if(shared_fire_respawn||(r16(0xfe10)==0&&r16(0xee4e)==0&&r16(0xeec2)<=8)||(r16(0xfe10)==1&&r16(0xee4e)!=0)){
        s.restore_anchor=shared_fire_anchor;s.restore_actual=1;s.restore_epoch=s.epoch;
        // The dying player's transition coordinates may be inside a hazard.
        // Prefer a teammate's saved safe point in the new section, falling
        // back to that living teammate rather than the position that killed us.
        for(unsigned who=0;who<MAX_PLAYERS;who++){
            const auto &v=s.players[who];const auto &p=v.pose;
            if(int(who)!=s.local_id&&v.connected&&v.pose_valid&&p.epoch==s.epoch&&p.actual==1&&p.stage==0&&p.routine==2&&!(p.flags&3)){
                s.restore_anchor=(p.safe.valid&&p.safe.stage==0&&p.safe.actual==1)?p.safe:SafeAnchor{true,0,p.x,p.y,1};break;
            }
        }
        s.restore_pending=true;s.restore_loading=false;
    }
}
int shared_ice_hook(){
    if(s.phase!=PLAY||side_trip()||r16(0xfe10)!=0x500||r16(0xee4e)!=0x500)return 0;
    // The native branch waits for queued art before replacing the layout.
    // Preserve that prerequisite and keep every camera in the old coordinate
    // space until all participants can safely subtract $6880.
    if(!r16(0xff0e)&&r16(0xee78)>=0x6880&&r16(0xb010)>=0x6880){
        if(!shared_ice_holding){shared_ice_control=r8(0xb02e);shared_ice_holding=true;}
        shared_phase_waiting=true;w8(0xb02e,0x81);w32(0xb018,0);w16(0xb01c,0);w8(0xb02c,30);
        if(barrier(0x500)){
            shared_phase_waiting=shared_ice_holding=false;w8(0xb02e,shared_ice_control);
            shared_ice_committed=s.epoch;
            shared_ice_anchor={true,0x500,unsigned(r16(0xb010)-0x6880),unsigned(r16(0xb014)+0x100),0x501};
            return 0;
        }
    }
    online_host->tail_call(0x53938);return 1;
}
void shared_phase_packet(int channel=-1){
    unsigned char p[20]{};put32(p,12);put32(p+4,shared_fire_epoch);put32(p+8,0);
    put32(p+12,unsigned(s.local_id));put32(p+16,0x4fc0e);
    if(channel>=0)send_packet(unsigned(channel),WORLD,p,sizeof p);
    else if(s.host)broadcast(WORLD,p,sizeof p);else send_packet(0,WORLD,p,sizeof p);
}
bool shared_phase_handle(unsigned channel,const unsigned char *p,unsigned n){
    if(n!=20||get32(p+8)!=0||get32(p+16)!=0x4fc0e)return false;
    if(get32(p+4)!=s.epoch)return true;
    if(s.host){
        unsigned who=get32(p+12);int sender=channel<MAX_CHANNELS?channels[channel].peer:s.local_id;
        if(who>=MAX_PLAYERS||int(who)!=sender)return false;
        if(!active_pose(who)||s.players[who].pose.stage!=0||(s.players[who].pose.flags&1)||s.players[who].pose.x<0x2d80)return true;
    }else if(channel!=0)return false;
    if(shared_fire_epoch!=s.epoch){shared_fire_epoch=s.epoch;if(s.host)shared_phase_packet();}
    return true;
}
void shared_phase_pre(){
    // A distant player keeps playing the original layout and can reach the
    // shared boss area even after its owner has completed the encounter.
    if(shared_fire_epoch==s.epoch&&world_play()&&shared_stage_id()==0&&r16(0xeec2)==8&&r16(0xee78)>=0x2f00)
        w16(0xeec6,0xffff);
}
int shared_fire_hook(){
    if(s.phase!=PLAY||side_trip()||r16(0xfe10)!=0||r16(0xee4e)!=0)return 0;
    if(shared_fire_epoch!=s.epoch){shared_fire_epoch=s.epoch;sample_pose();
            if(!s.host){unsigned char pose[80];encode_pose(unsigned(s.local_id),pose);send_packet(0,POSE,pose,sizeof pose);}
            shared_phase_packet();}
    // The native sequence subtracts $2F00 from the camera as well as players.
    // A straggler must reach that boundary first, or the unsigned native ring
    // manager would see a camera wrapped to $FFxx after rebasing.
    if(r16(0xee78)>=0x2f00){
        shared_phase_waiting=true;
        if(barrier(0)){
            shared_phase_waiting=false;shared_fire_committed=s.epoch;
            shared_fire_anchor={true,0,unsigned((std::max)(16,int(r16(0xb010))-0x2f00)),unsigned((std::max)(0,int(r16(0xb014))-0x80)),1};
            return 0;
        }
    }
    // Re-enter the native no-transition branch so its JSR owns the emulated
    // return slot and all deformation registers retain their native contract.
    // The shared intent reasserts this signal on the next ready frame.
    w16(0xeec6,0);online_host->tail_call(0x4fbea);return 1;
}
// Native StartNewLevel also changes level without an act results screen: the
// act-3 boss layouts of Lava Reef ($1600) and the Death Egg ($1700), Sky
// Sanctuary into the Death Egg, and the final endings. Nothing held the party
// there, so the first player through loaded the next level alone. Hold that
// player like a results screen (no control, motion or contact; timer stopped)
// until everyone arrives, then perform StartNewLevel's own writes. A change
// after this layout's confirmed results barrier proceeds natively: the whole
// party has already cleared the act.
unsigned shared_transition_target=~0u,shared_transition_layout=~0u,shared_transition_entry=~0u;
bool shared_doomsday_spectator=false;
unsigned test_start_level=~0u; // private test control only
void shared_transition_reset(){shared_transition_target=shared_transition_layout=shared_transition_entry=~0u;shared_doomsday_spectator=false;}
void shared_transition_observe(){
    if(s.phase!=PLAY||side_trip()||r8(0xf600)!=0x0c||!r8(0xf711))return;
    if(r16(0xfe10)!=shared_transition_layout){shared_transition_layout=r16(0xfe10);shared_transition_entry=s.epoch;}
}
// After the Death Egg robot, native code sends Sonic (Player_mode 0/1) with
// all seven Chaos Emeralds to Doomsday ($C00) and everyone else to the ending
// ($D01). The party crosses this barrier together towards one target: Doomsday
// when a Sonic player is present with seven emeralds, otherwise the ending.
// Doomsday itself is played only by Sonic players; the others wait at the
// ending's barrier watching a Sonic player, and everyone enters the ending
// together once Doomsday's own StartNewLevel($D01) brings Sonic there.
unsigned shared_party_ending(){
    bool sonic=false;for(const auto &p:s.players)if(p.present&&p.character==0)sonic=true;
    return sonic&&r8(0xffb0)>=7?0xc00:0xd01;
}
int shared_transition_hook(){
    if(s.phase!=PLAY||side_trip()||r8(0xf600)!=0x0c)return 0;
    unsigned target=g_cpu.D[0]&65535;
    if(r16(0xfe10)==0x1700&&(target==0xc00||target==0xd01))target=shared_party_ending();
    if(shared_transition_target==~0u){
        if(target==r16(0xfe10))return 0;
        shared_transition_observe();
        if(shared_transition_layout!=r16(0xfe10)||shared_transition_entry!=s.epoch)return 0;
        shared_transition_target=target;
    }
    return 1; // Return without the writes; the caller's own state is unchanged.
}
void shared_transition_pre(){
    shared_transition_observe();
#if OA_TEST_CONTROL
    if(test_start_level!=~0u){M68KState saved=g_cpu;g_cpu.D[0]=(g_cpu.D[0]&0xffff0000u)|test_start_level;test_start_level=~0u;recomp_call_addr(0x85e3a);g_cpu=saved;}
#endif
    if(shared_transition_target==~0u||s.phase!=PLAY||r8(0xf600)!=0x0c)return;
    shared_phase_waiting=true;
    w8(0xb02e,0x81);w32(0xb018,0);w16(0xb01c,0);w8(0xb02c,30);w8(0xfe1e,0);
    if(!barrier(shared_transition_target)){
        // The host accepts a watch only once it has recorded this player's
        // arrival, so keep asking until its watcher table confirms one.
        static unsigned spectator_retry=0;
        if(shared_doomsday_spectator&&(team.watch<0||team.watchers[s.local_id]!=team.watch)&&++spectator_retry%60==1){
            int target=team.watch>=0&&s.players[team.watch].connected?team.watch:-1;
            for(unsigned who=0;who<MAX_PLAYERS&&target<0;who++)
                if(int(who)!=s.local_id&&s.players[who].present&&s.players[who].connected&&s.players[who].character==0)target=int(who);
            if(target>=0)watch_request(target);
        }
        return;
    }
    unsigned target=shared_transition_target;shared_transition_target=~0u;shared_phase_waiting=false;
    if(target==0xc00&&s.character!=0){
        // Not Sonic: stay held here, now at the ending's barrier, and watch.
        shared_doomsday_spectator=true;shared_transition_target=0xd01;return;
    }
    if(shared_doomsday_spectator){shared_doomsday_spectator=false;if(team.watch>=0)watch_request(-1);}
    w16(0xfe10,target);w16(0xee4e,target);w16(0xfe02,1);w8(0xfe2a,0);w8(0xfe48,0);
}
