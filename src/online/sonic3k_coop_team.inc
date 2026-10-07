// Private implementation included by sonic3k_coop.cpp. Wire integers are big endian.
struct TeamPing {unsigned epoch=0,stage=0,x=0,y=0,kind=0,when=0,sequence=0;};
struct VideoFrame {
    std::array<unsigned char,192*128*2> incoming{};
    std::vector<uint32_t> pixels;
    SDL_Texture *texture=nullptr;
    unsigned sequence=0,received=0,width=0,height=0,when=0,complete=0,texture_width=0,texture_height=0;
};
struct TeamState {
    bool open=false,rescue_enabled=true,carry_owned=false,carry_blocked=false;
    std::array<ImVec2,MAX_PLAYERS> labels{};std::array<bool,MAX_PLAYERS> label_visible{};unsigned label_count=0;
    unsigned input=0,previous_input=0,sequence=0,carry_revision=0,carry_started=0;
    int carry_vx=0,carry_vy=0;bool carry_prepared=false;
    unsigned last_ping=0,last_video=0,video_sequence=0,last_rescue=0,last_notice=0;
    int ping_kind=0,watch=-1;
    std::array<int,MAX_PLAYERS> carrier{},cargo{},watchers{};
    std::array<unsigned,MAX_PLAYERS> pickup_after{},action_sequence{},rescue_after{};
    std::array<TeamPing,MAX_PLAYERS> pings{};
    std::array<VideoFrame,MAX_PLAYERS> video;
    char notice[180]{};
} team;
void world_reset();void world_frame();void world_pre_frame();void world_idle_frame();void world_send_snapshot(unsigned);
void shared_phase_recover();void shared_restore_section();
bool world_handle(unsigned,const unsigned char*,unsigned);bool world_debug(int,const char*);
bool active_pose(unsigned who) {
    if(who>=MAX_PLAYERS)return false;
    auto &p=s.players[who];return p.present&&p.connected&&p.pose_valid&&p.pose.epoch==s.epoch&&!(p.pose.flags&3)&&p.pose.routine==2;
}
void team_toggle(){team.open=!team.open;if(team.open){level_map.open=false;palette_editor=false;}}
void resume_ack(bool fresh){unsigned char data[8];put32(data,unsigned(s.local_id));put32(data+4,fresh?1:0);send_packet(0,RESUME,data,sizeof(data));}
void team_notice(const char *text) {snprintf(team.notice,sizeof(team.notice),"%s",text);team.last_notice=SDL_GetTicks();}
void send_reply(unsigned who,const char *message) {
    if(int(who)==s.local_id){team_notice(message);return;}
    unsigned char data[164]{};put32(data,who);snprintf((char*)data+4,160,"%s",message);send_to_player(who,TEAM_REPLY,data,sizeof(data));
}
void team_reset() {
    for(auto &v:team.video)if(v.texture)SDL_DestroyTexture(v.texture);
    for(auto &motion:remote_motion)motion.clear();
    team=TeamState{};team.carrier.fill(-1);team.cargo.fill(-1);team.watchers.fill(-1);world_reset();emerald_reset();
}
void encode_pose(unsigned who,unsigned char *p) {
    auto &v=s.players[who];const auto &a=v.pose;memset(p,0,80);
    put32(p,who);put32(p+4,a.epoch);put16(p+8,a.stage);put16(p+10,a.x);put16(p+12,a.y);
    put16(p+14,unsigned(a.vx));put16(p+16,unsigned(a.vy));put16(p+18,a.buttons);
    p[20]=uint8_t(a.routine);p[21]=uint8_t(a.status);p[22]=uint8_t(a.anim);p[23]=uint8_t(a.flight);p[24]=uint8_t(a.control);p[25]=uint8_t(a.flags);
    p[26]=uint8_t(v.chaos_count);p[27]=uint8_t(v.super_count);memcpy(p+28,v.emerald_state.data(),7);
    p[35]=uint8_t(v.chaos_mask);p[36]=uint8_t(v.super_mask);
    p[40]=a.safe.valid;put16(p+42,a.safe.stage);put16(p+44,a.safe.x);put16(p+46,a.safe.y);
    p[48]=a.checkpoint.valid;put16(p+50,a.checkpoint.stage);put16(p+52,a.checkpoint.x);put16(p+54,a.checkpoint.y);
    put16(p+56,a.actual);put16(p+58,a.safe.actual);put16(p+60,a.checkpoint.actual);put16(p+62,a.wrap_y);
}
bool decode_pose(const unsigned char *p,unsigned n) {
    for(unsigned i=28;i<35&&i<n;i++)if(p[i]>3)return false;
    if(n!=80||get32(p)>=MAX_PLAYERS||p[25]>7||p[26]>7||p[27]>7||p[35]>127||p[36]>127||p[40]>1||p[48]>1||get16(p+56)>0x1701||get16(p+58)>0x1701||get16(p+60)>0x1701)return false;
    unsigned who=get32(p);auto &v=s.players[who];auto &a=v.pose;
    a.epoch=get32(p+4);a.stage=get16(p+8);a.x=get16(p+10);a.y=get16(p+12);a.vx=int16_t(get16(p+14));a.vy=int16_t(get16(p+16));a.buttons=get16(p+18);
    a.routine=p[20];a.status=p[21];a.anim=p[22];a.flight=p[23];a.control=p[24];a.flags=p[25];
    unsigned period=get16(p+62);if(period!=0&&period!=2048&&period!=4096)return false;
    a.actual=get16(p+56);a.wrap_y=period;
    a.safe={p[40]!=0,get16(p+42),get16(p+44),get16(p+46),get16(p+58)};
    a.checkpoint={p[48]!=0,get16(p+50),get16(p+52),get16(p+54),get16(p+60)};
    v.chaos_count=p[26];v.super_count=p[27];memcpy(v.emerald_state.data(),p+28,7);
    emerald_project();v.pose_valid=true;v.pose_time=SDL_GetTicks();return true;
}
void sample_pose() {
    if(s.local_id<0)return;auto &v=s.players[s.local_id];auto &a=v.pose;
    a.epoch=s.epoch;a.stage=r16(0xee4e);a.actual=r16(0xfe10);a.wrap_y=local_y_period();a.x=r16(0xb010);a.y=r16(0xb014);a.vx=int16_t(r16(0xb018));a.vy=int16_t(r16(0xb01a));
    a.routine=r8(0xb005);a.status=r8(0xb02a);a.anim=r8(0xb020);a.flight=r8(0xb02f);a.control=r8(0xb02e);a.buttons=team.input;
    a.flags=(side_trip()?1u:0u)|(s.waiting?2u:0u)|((r8(0xf7c6)!=0)?4u:0u);
    bool normal=s.phase==PLAY&&!s.start_pending&&!title_card_active()&&r8(0xf600)==0x0c&&!side_trip();
    if(normal&&a.routine==2&&!(a.status&2)&&!a.control&&!s.waiting) a.safe={true,a.stage,a.x,a.y,a.actual};
    if(normal) {
        if(r8(0xfe2a)&&!(r8(0xfe2a)&0x80)&&r16(0xfe2c)==a.actual)a.checkpoint={true,a.stage,r16(0xfe2e),r16(0xfe30),a.actual};
        else if(a.checkpoint.stage!=a.stage||a.checkpoint.actual!=a.actual)a.checkpoint.valid=false;
    }
    emerald_project();v.pose_valid=true;v.pose_time=SDL_GetTicks();
}
void send_resume(unsigned channel) {
    if(s.phase==PLAY&&(r8(0xf600)!=0x0c||!r8(0xf711)||title_card_active()||r16(0xfe02))){channels[channel].resume_waiting=true;return;}
    channels[channel].resume_waiting=false;
    unsigned who=unsigned(channels[channel].peer);auto &v=s.players[who];
    unsigned char data[240]{};put32(data,s.epoch);put32(data+4,s.last_release);put32(data+8,s.shared_intro?1:0);
    put32(data+12,v.arrival_epoch);put32(data+16,v.stage);put32(data+20,team.rescue_enabled?1:0);
    for(unsigned i=0;i<14;i++)put32(data+24+i*4,s.used_rings[i]);
    encode_pose(who,data+80);
    // A process restarted after a released barrier joins the team's current
    // act, retaining shared inventory instead of reviving the previous act.
    if(s.phase==PLAY&&(v.pose.epoch!=s.epoch||v.pose.actual!=r16(0xfe10)||v.pose.stage!=r16(0xee4e)||!v.pose.safe.valid||v.pose.safe.stage!=v.pose.stage||v.pose.safe.actual!=v.pose.actual)) {
        auto anchor=s.players[0].pose.safe;unsigned stage=r16(0xee4e);
        put32(data+84,s.epoch);put16(data+88,stage);
        data[120]=anchor.valid&&anchor.stage==stage&&anchor.actual==r16(0xfe10);put16(data+122,stage);put16(data+124,anchor.x);put16(data+126,anchor.y);
        put16(data+136,r16(0xfe10));put16(data+138,r16(0xfe10));
    }
    put32(data+232,unsigned(s.phase));
    for(unsigned i=0;i<MAX_PLAYERS;i++){put32(data+160+i*8,s.players[i].arrival_epoch);put32(data+164+i*8,s.players[i].stage);}
    send_packet(channel,RESUME,data,sizeof(data));
    for(unsigned i=0;i<MAX_PLAYERS;i++)if(s.players[i].present&&s.players[i].pose_valid){unsigned char pose[80];encode_pose(i,pose);send_packet(channel,POSE,pose,sizeof(pose));}
    world_send_snapshot(channel);
}
void reconnect_now() {
    if(s.host||!s.reconnecting)return;
    close_socket(channels[0].fd);channels[0]=Channel{};channels[0].peer=0;
    CoopSocket fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(fd==BAD_SOCKET||!nonblocking(fd)){close_socket(fd);s.retry_at=SDL_GetTicks()+2000;return;}
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(uint16_t(port_number));
    if(inet_pton(AF_INET,peer_address,&address.sin_addr)!=1){close_socket(fd);fail("Saved host address is invalid.");return;}
    channels[0].fd=fd;s.connect_at=SDL_GetTicks();s.connecting_socket=true;
    int result=connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address));
    if(result==0){s.connecting_socket=false;connected(0);}
    else if(!would_block())disconnect_channel(0);
}
void resume_ticket() {
    if(r8(0xf600)!=0x4c||!ticket.room||!ticket.token)return;
#ifdef _WIN32
    if(!socket_ready){WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)){team_notice("Networking could not start.");return;}socket_ready=true;}
#endif
    ResumeTicket saved=ticket;leave(false);s.host=false;s.room=saved.room;s.token=saved.token;s.character=saved.character;
    port_number=saved.port;snprintf(peer_address,sizeof(peer_address),"%s",saved.address);
    s.resume_menu=true;s.reconnecting=true;s.resume_phase=PLAY;s.phase=RECONNECTING;s.saved_pause=r16(0xf63a);s.retry_at=0;
    reconnect_now();
}
void restore_emeralds() { emerald_apply(); }

void shared_redraw_foreground();
bool warp_local(unsigned stage,unsigned x,unsigned y,bool restoring=false) {
    if(!restoring&&(s.phase!=PLAY||s.waiting||side_trip()||title_card_active()||r8(0xb005)!=2||r8(0xb02e)))return false;
    if(r16(0xee4e)!=stage||x>65535||y>4095)return false;
    if(!restoring&&(x<r16(0xee14)||x>unsigned(r16(0xee16))+320))return false;
    motion_warp++;
    w16(0xb010,x);w16(0xb012,0);w16(0xb014,y);w16(0xb016,0);w32(0xb018,0);w16(0xb01c,0);
    w8(0xb02e,0);w8(0xb02f,0);w8(0xb03d,0);w8(0xb040,0);w8(0xb020,0);w8(0xb026,0);
    w8(0xb02a,(r8(0xb02a)&0x61)|2);w8(0xb01e,r8(0xb044));w8(0xb01f,r8(0xb045));w8(0xb034,60);
    unsigned cx=(std::max)(unsigned(r16(0xee14)),(std::min)(unsigned(r16(0xee16)),x>160?x-160:0));
    unsigned cy=local_y_period()?unsigned(adventure::wrapped_position(float(int(y)-96),float(local_y_period()))):unsigned(std::clamp(int(y)-96,int(int16_t(r16(0xee18))),int(r16(0xee1a))));
    w32(0xee78,cx<<16);w32(0xee80,cx<<16);w32(0xee7c,cy<<16);w32(0xee84,cy<<16);
    // Rebuild native placement cursors without clearing persistent respawn
    // bits. A large backwards camera jump can otherwise walk their old range
    // past the placement table and write respawn flags into camera memory.
    M68KState saved=g_cpu;
    recomp_call_addr(0x1b6e8);
    recomp_call_addr(0x4f2da);
    g_cpu=saved;shared_redraw_foreground();
    return true;
}
void restore_frame() {
    // Restore as soon as the native player is initialized. Waiting for the
    // title card to leave lets physics run at the default entrance first.
    if(!s.restore_pending||s.start_pending||r8(0xf600)!=0x0c||!r8(0xf711))return;
    if(r16(0xfe10)!=s.restore_actual) {
        if(s.restore_loading)return;s.restore_loading=true;
        w8(0xfe2a,0);w8(0xfe48,0);w16(0xfe10,s.restore_actual);w16(0xee4e,s.restore_actual);w16(0xfe02,1);return;
    }
    if(r16(0xfe02)||r8(0xb005)!=2)return;
    w16(0xee4e,s.restore_anchor.stage);
    shared_restore_section();
    restore_emeralds();if(s.restore_anchor.valid)warp_local(s.restore_anchor.stage,s.restore_anchor.x,s.restore_anchor.y,true);
    s.restore_pending=s.restore_loading=false;
    if(s.resume_menu){s.resume_menu=false;resume_ack(true);team_notice("Rejoined your adventure.");}
}
void apply_carry(unsigned carrier,unsigned passenger,bool attach,unsigned revision,bool jump,bool preserve_motion=false) {
    if(revision<=team.carry_revision)return;team.carry_revision=revision;
    if(attach){team.cargo[carrier]=int(passenger);team.carrier[passenger]=int(carrier);}
    else {if(team.cargo[carrier]==int(passenger))team.cargo[carrier]=-1;if(team.carrier[passenger]==int(carrier))team.carrier[passenger]=-1;team.pickup_after[passenger]=SDL_GetTicks()+1000;}
    if(int(passenger)==s.local_id) {
        team.carry_blocked=false;
        if(attach){team.carry_started=SDL_GetTicks();team.carry_owned=true;w8(0xb02e,1);w8(0xb020,s.character==2?0x14:0x11);w8(0xb03d,0);w8(0xb02f,0);}
        else if(team.carry_owned){team.carry_owned=false;if(r8(0xb02e)==1)w8(0xb02e,0);if(preserve_motion)return;w8(0xb02a,(r8(0xb02a)&0x61)|2);w8(0xb020,jump?2:0);w16(0xb01a,jump?uint16_t(-0x380):0x100);w8(0xb040,jump?1:0);if(jump){w8(0xb01e,14);w8(0xb01f,7);w8(0xb02a,r8(0xb02a)|4);}w8(0xb034,30);}
    }
}
void carry_event(unsigned carrier,unsigned passenger,bool attach,bool jump=false,bool preserve_motion=false) {
    if(!s.host||carrier>=MAX_PLAYERS||passenger>=MAX_PLAYERS)return;
    unsigned char p[28]{};put32(p,2);put32(p+4,carrier);put32(p+8,passenger);put32(p+12,attach?1:0);put32(p+16,team.carry_revision+1);put32(p+20,(jump?1u:0)|(preserve_motion?2u:0));put32(p+24,s.epoch);
    apply_carry(carrier,passenger,attach,get32(p+16),jump,preserve_motion);broadcast(CARRY,p,sizeof(p));
}
bool can_carry(unsigned carrier,unsigned passenger) {
    if(carrier==passenger||!active_pose(carrier)||!active_pose(passenger)||s.players[carrier].character!=1||team.cargo[carrier]>=0||team.carrier[carrier]>=0||team.carrier[passenger]>=0||team.cargo[passenger]>=0)return false;
    auto &a=s.players[carrier].pose;auto &b=s.players[passenger].pose;
    if(a.stage!=b.stage||a.actual!=b.actual||!a.flight||!(a.status&2)||a.anim==0x24||a.control||b.control||(a.flags&4)!=(b.flags&4)||int32_t(SDL_GetTicks()-team.pickup_after[passenger])<0)return false;
    int dx=int(a.x)-int(b.x),dy=int(adventure::wrapped_delta(float((a.flags&4)?int(a.y)-int(b.y):int(b.y)-int(a.y)),float(a.wrap_y)));
    return dx>=-16&&dx<=16&&dy>=24&&dy<=48;
}
void carry_host_frame() {
    if(!s.host||s.phase!=PLAY)return;
    for(unsigned c=0;c<MAX_PLAYERS;c++)if(team.cargo[c]>=0) {
        unsigned p=unsigned(team.cargo[c]);auto &a=s.players[c].pose;auto &b=s.players[p].pose;
        if(!active_pose(c)||!active_pose(p)||a.stage!=b.stage||a.actual!=b.actual||!a.flight||!(a.status&2)||a.anim==0x24||a.control)carry_event(c,p,false,false,b.routine>=4);
    }
    for(unsigned c=0;c<MAX_PLAYERS;c++)for(unsigned p=0;p<MAX_PLAYERS;p++)if(can_carry(c,p)){carry_event(c,p,true);break;}
}
void request_drop(bool jump,bool preserve_motion=false) {
    if(s.local_id<0)return;unsigned who=unsigned(s.local_id);int c=team.carrier[who],p=int(who);
    if(c<0){c=int(who);p=team.cargo[who];}if(p<0)return;
    if(s.host)carry_event(unsigned(c),unsigned(p),false,jump,preserve_motion);
    else {unsigned char data[20];put32(data,1);put32(data+4,who);put32(data+8,unsigned(c));put32(data+12,unsigned(p));put32(data+16,(jump?1u:0)|(preserve_motion?2u:0));send_packet(0,CARRY,data,sizeof(data));}
}
// Knuckles uses FBZ Chain Link hanging animation $14 (mapping frame $91),
// matching the native overhead bars. Sonic keeps his existing carry pose.
void carry_native_release() {
    team.carry_blocked=true;team.carry_prepared=false;
    if(r8(0xb02e)==1)w8(0xb02e,0);
    request_drop(false,true);
}
void carry_after_frame() {
    if(!team.carry_prepared||team.carry_blocked||!team.carry_owned)return;
    team.carry_prepared=false;
    if(r8(0xb005)!=2||r8(0xb02e)!=1||!(r8(0xb02a)&2)||
       int16_t(r16(0xb018))!=team.carry_vx||int16_t(r16(0xb01a))!=team.carry_vy)
        carry_native_release();
}
void carry_local_frame() {
    team.carry_prepared=false;
    if(s.local_id<0||s.phase!=PLAY)return;int carrier=team.carrier[s.local_id];
    if(team.carry_blocked||carrier<0||!active_pose(unsigned(carrier))||side_trip()||r8(0xb005)!=2)return;
    auto &a=s.players[carrier].pose;
    auto anchor=remote_motion[carrier].sample(double(SDL_GetTicks64()));
    unsigned flags=anchor.frame?(anchor.frame->image[20]&3):((a.status&1)|((a.flags&4)?2:0));
    auto grip=carry_grip(unsigned(s.local_id),flags);
    float x=(anchor.frame?anchor.x:float(a.x))+grip.x,y=adventure::wrapped_position((anchor.frame?anchor.y:float(a.y))+grip.y,float(a.wrap_y));
    w32(0xb010,uint32_t(int64_t(std::llround(double(x)*65536))));
    w32(0xb014,uint32_t(int64_t(std::llround(double(y)*65536))));
    w16(0xb018,unsigned(a.vx));w16(0xb01a,unsigned(a.vy));w16(0xb01c,0);
    w8(0xb02e,1);w8(0xb020,s.character==2?0x14:0x11);w8(0xb02a,(r8(0xb02a)&0x60)|(flags&1)|2);w8(0xb004,(r8(0xb004)&~3)|flags);
    w8(0xb01e,r8(0xb044));w8(0xb01f,r8(0xb045));w8(0xb02f,0);w8(0xb03d,0);w8(0xb040,0);
    // Bit 0 locks locomotion; bit 7 would also disable native enemy/ring
    // collision. Keep collision enabled while the carrier supplies movement.
    team.carry_vx=int16_t(r16(0xb018));team.carry_vy=int16_t(r16(0xb01a));team.carry_prepared=true;
    M68KState saved=g_cpu;unsigned px=r16(0xb010),py=r16(0xb014);
    g_cpu.A[0]=0xffb000;recomp_call_addr(0x11eec);g_cpu=saved;
    if(r16(0xb010)!=px||r16(0xb014)!=py||!(r8(0xb02a)&2))carry_native_release();
}
int carry_collision_hook() {
    if(s.phase!=PLAY||!team.carry_owned||team.carry_blocked||s.local_id<0||team.carrier[s.local_id]<0||(g_cpu.A[0]&65535)!=0xb000)return 0;
    M68KState saved=g_cpu;unsigned x=r16(0xb010),y=r16(0xb014);
    recomp_call_addr(0x11eec);g_cpu=saved;
    if(r16(0xb010)!=x||r16(0xb014)!=y||!(r8(0xb02a)&2)) {
        carry_native_release();
    }
    return 0;
}
void team_disconnect(unsigned who) {
    if(who>=MAX_PLAYERS)return;
    remote_motion[who].clear();
    if(s.host) {
        if(team.carrier[who]>=0)carry_event(unsigned(team.carrier[who]),who,false);
        if(team.cargo[who]>=0)carry_event(who,unsigned(team.cargo[who]),false);
        team.watchers[who]=-1;
    } else {if(team.carry_owned){team.carry_owned=false;if(r8(0xb02e)==1)w8(0xb02e,0);}team.carrier.fill(-1);team.cargo.fill(-1);}
}
void send_name() {
    char cleaned[25];clean_name(cleaned,(unsigned char*)local_name,24);snprintf(local_name,25,"%s",cleaned);save_identity();
    if(s.local_id<0)return;snprintf(s.players[s.local_id].name,25,"%s",local_name);
    if(s.host)roster();else {unsigned char data[28]{};put32(data,unsigned(s.local_id));memcpy(data+4,local_name,24);send_packet(0,NAME,data,sizeof(data));}
}
void send_ping(unsigned kind,unsigned x,unsigned y) {
    if(s.phase!=PLAY||s.local_id<0||kind>3||level_map.epoch!=s.epoch||int32_t(SDL_GetTicks()-team.last_ping)<750)return;
    team.last_ping=SDL_GetTicks();unsigned char data[28];put32(data,unsigned(s.local_id));put32(data+4,s.epoch);put32(data+8,level_map.stage);put32(data+12,x);put32(data+16,y);put32(data+20,kind);put32(data+24,++team.sequence);
    auto &ping=team.pings[s.local_id];ping={s.epoch,level_map.stage,x,y,kind,SDL_GetTicks(),get32(data+24)};
    if(s.host)broadcast(MAP_PING,data,sizeof(data));else send_packet(0,MAP_PING,data,sizeof(data));
}
void rescue_request(int target) {
    if(s.local_id<0)return;unsigned char data[16];put32(data,0);put32(data+4,unsigned(s.local_id));put32(data+8,unsigned(target));put32(data+12,++team.sequence);
    if(s.host)team_handle(MAX_CHANNELS,RESCUE,data,sizeof(data));else send_packet(0,RESCUE,data,sizeof(data));
}
void watch_request(int target) {
    if(s.local_id<0)return;
    if(target>=0&&(!s.waiting||target==s.local_id||target>=int(MAX_PLAYERS)||!s.players[target].connected))return;
    team.watch=target;
    unsigned char data[12];put32(data,0);put32(data+4,unsigned(s.local_id));put32(data+8,unsigned(target));
    if(s.host)team_handle(MAX_CHANNELS,WATCH,data,sizeof(data));else send_packet(0,WATCH,data,sizeof(data));
}
void team_map_click(unsigned x,unsigned y){send_ping(unsigned(team.ping_kind),x,y);}
void draw_team_map_tools() {
    const char *labels[]={"Ping","Help","Big ring","Wait here"};
    ImGui::TextDisabled("Click map or X (center). LB/RB: type");
    ImGui::TextDisabled("Ping type:");ImGui::SameLine();
    for(int i=0;i<4;i++){if(i)ImGui::SameLine();if(ImGui::RadioButton(labels[i],team.ping_kind==i))team.ping_kind=i;}
}
void draw_team_pings(ImDrawList *draw,ImVec2 origin,float scale,unsigned shrink,unsigned stage,unsigned epoch) {
    const char *labels[]={"Here","Help!","Big ring","Wait here"};unsigned now=SDL_GetTicks();
    for(unsigned who=0;who<MAX_PLAYERS;who++){auto &p=team.pings[who];if(!p.when||p.stage!=stage||p.epoch!=epoch||int32_t(now-p.when)>12000)continue;
        ImVec2 pos(origin.x+p.x*scale/shrink,origin.y+p.y*scale/shrink);
        ImU32 color=p.kind==1?IM_COL32(255,110,95,255):IM_COL32(255,218,70,255);
        draw->AddCircle(pos,8+float((now-p.when)%800)/100,color,16,2);
        char text[80];snprintf(text,sizeof(text),"%s: %s",player_name(who),labels[p.kind]);draw->AddText(ImVec2(pos.x+10,pos.y-10),color,text);
    }
}
bool team_event(const SDL_Event *event) {
    if(!in_game())return false;
    if(level_map.open&&event->type==SDL_CONTROLLERBUTTONDOWN) {
        unsigned button=event->cbutton.button;
        if(button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER||button==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER){team.ping_kind=(team.ping_kind+(button==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER?1:3))%4;return true;}
        if(button==SDL_CONTROLLER_BUTTON_X){send_ping(unsigned(team.ping_kind),unsigned(level_map.center_x*level_map.world_width),unsigned(level_map.center_y*level_map.world_height));return true;}
    }
    bool pressed=event->type==SDL_KEYDOWN&&!event->key.repeat;
    if((pressed&&event->key.keysym.sym==SDLK_F2)||(event->type==SDL_CONTROLLERBUTTONDOWN&&event->cbutton.button==SDL_CONTROLLER_BUTTON_Y)) {
        team_toggle();return true;
    }
    return false;
}
uint16_t team_pad(uint16_t buttons) {
    team.input=buttons;unsigned pressed=buttons&~team.previous_input;team.previous_input=buttons;
    if(s.local_id>=0&&s.phase==PLAY) {
        if(team.carrier[s.local_id]>=0&&(pressed&0x70)&&int32_t(SDL_GetTicks()-team.carry_started)>300)request_drop(true);
        if(team.cargo[s.local_id]>=0&&(buttons&2)&&(pressed&0x70))request_drop(false);
        if(team.carrier[s.local_id]>=0)return 0;
    }
    return team.open?0:buttons;
}
int resume_pause_hook() {
    // Pause_ChkStart is reached inside the native pause loop. Merely clearing
    // Game_paused cannot return from it; let its own Start branch resume music
    // and gameplay. Never inject Start into normal gameplay or Data Select.
    if(native_unpause&&s.phase!=RECONNECTING){w8(0xf605,r8(0xf605)|0x80);native_unpause=false;}
    return 0;
}
void team_pre_frame() {
    if(s.phase==RECONNECTING){if(!s.resume_menu)w16(0xf63a,1);return;}
}
void team_frame(uint64_t frame) {
    if(s.local_id<0||s.phase==RECONNECTING)return;
    if(s.host)for(unsigned i=1;i<MAX_CHANNELS;i++)if(channels[i].hello&&channels[i].resume_waiting)send_resume(i);
    shared_phase_recover();restore_frame();if(s.restore_pending)return;
    // Carry position is applied before native animation/collision/SAT generation.
    // Rewriting it afterwards would offset the transmitted sprite against its center.
    sample_pose();carry_host_frame();world_idle_frame();
    if(s.phase==PLAY&&s.hello) {
        unsigned char data[80];encode_pose(unsigned(s.local_id),data);
        if(s.host)broadcast(POSE,data,sizeof(data));else send_packet(0,POSE,data,sizeof(data));
    }
    if(team.watch>=0&&(!s.waiting||!s.players[team.watch].connected))watch_request(-1);
}
bool team_handle(unsigned channel,unsigned kind,const unsigned char *p,unsigned n) {
    int sender=channel<MAX_CHANNELS?channels[channel].peer:s.local_id;
    auto valid_who=[&](unsigned who){return who<MAX_PLAYERS&&s.players[who].present&&(!s.host||int(who)==sender);};
    if(kind==TEAM_EMERALDS)return false;
    if(kind==NAME){if(!s.host||n!=28||!valid_who(get32(p)))return false;clean_name(s.players[get32(p)].name,p+4,24);roster();return true;}
    if(kind==POSE) {
        if(n!=80||!valid_who(get32(p))||!in_game())return false;unsigned who=get32(p);
        if(int(who)==s.local_id)return !s.host; // Resume snapshots include the owner's last acknowledged pose.
        if(!decode_pose(p,n))return false;if(s.host)broadcast(POSE,p,n,int(who));return true;
    }
    if(kind==RESUME) {
        if(s.host) {
            if(n!=8||!valid_who(get32(p))||get32(p+4)>1)return false;
            auto &v=s.players[get32(p)];v.connected=true;
            if(get32(p+4)){v.arrival_epoch=~0u;v.state_valid=false;}
            roster();
            bool all_connected=true;for(auto &player:s.players)if(player.present&&!player.connected)all_connected=false;
            if(all_connected)progress_restart(true);
            maybe_release();return true;
        }
        if(s.host||n!=240||!s.reconnecting||s.local_id<0||get32(p+20)>1)return false;
        unsigned server_epoch=get32(p),last_release=get32(p+4);
        for(unsigned i=0;i<14;i++)s.used_rings[i]|=get32(p+24+i*4);
        for(unsigned i=0;i<MAX_PLAYERS;i++){s.players[i].arrival_epoch=get32(p+160+i*8);s.players[i].stage=get32(p+164+i*8);}
        team.rescue_enabled=get32(p+20)!=0;
        if(s.resume_menu&&get32(p+232)==INTRO) {
            begin_game();s.resume_menu=false;resume_ack(true);team_notice("Rejoined. Watching the opening before starting together.");
        } else if(s.resume_menu) {
            if(!decode_pose(p+80,80))return false;auto &v=s.players[s.local_id];
            s.restore_actual=v.pose.actual;s.restore_anchor=v.pose.safe;
            if(!s.restore_anchor.valid||s.restore_anchor.stage!=v.pose.stage||s.restore_anchor.actual!=v.pose.actual)s.restore_anchor={false,v.pose.stage,0,0,v.pose.actual};
            s.restore_pending=true;s.restore_epoch=server_epoch;s.epoch=server_epoch;s.phase=PLAY;
            s.start_pending=true;s.start_frames=0;s.shared_intro=false;w8(0xef4b,0);w16(0xef4c,s.character+1);
            s.players[s.local_id].arrival_epoch=~0u;s.waiting=false;w16(0xf63a,0);
        } else {
            s.phase=s.resume_phase;
            if(s.waiting&&last_release!=~0u&&s.epoch<=last_release)s.released=true;
            else if(s.epoch!=server_epoch){fail("Session advanced beyond this reconnect point.");return true;}
            if(!s.saved_pause&&r16(0xf63a))native_unpause=true;team_notice("Reconnected. Your place was kept.");resume_ack(false);
        }
        s.reconnecting=s.connecting_socket=false;s.error[0]=0;s.hello=true;save_ticket();
        // Local intents survive a reconnect and are resubmitted after the
        // host starts the next shared-progress generation.
        return true;
    }
    if(kind==MAP_PING) {
        if(n!=28||!valid_who(get32(p))||get32(p+20)>3||get32(p+12)>65535||get32(p+16)>4095)return false;
        unsigned who=get32(p),sequence=get32(p+24);if(get32(p+4)!=s.epoch)return true;
        auto &ping=team.pings[who];if(sequence<=ping.sequence)return true;
        ping={get32(p+4),get32(p+8),get32(p+12),get32(p+16),get32(p+20),SDL_GetTicks(),sequence};
        if(s.host)broadcast(MAP_PING,p,n,int(who));return true;
    }
    if(kind==OPTIONS){if(s.host||n!=4||get32(p)>1)return false;team.rescue_enabled=get32(p)!=0;return true;}
    if(kind==CARRY) {
        if(n<4)return false;
        if(get32(p)==1) {
            if(!s.host||n!=20||!valid_who(get32(p+4))||get32(p+8)>=MAX_PLAYERS||get32(p+12)>=MAX_PLAYERS||get32(p+16)>3)return false;
            unsigned who=get32(p+4),c=get32(p+8),passenger=get32(p+12);
            if((who==c||who==passenger)&&team.cargo[c]==int(passenger))carry_event(c,passenger,false,(get32(p+16)&1)!=0,(get32(p+16)&2)!=0);return true;
        }
        if(get32(p)==2) {
            if(s.host||n!=28||get32(p+4)>=MAX_PLAYERS||get32(p+8)>=MAX_PLAYERS||get32(p+12)>1||get32(p+20)>3)return false;
            if(get32(p+24)==s.epoch)apply_carry(get32(p+4),get32(p+8),get32(p+12)!=0,get32(p+16),(get32(p+20)&1)!=0,(get32(p+20)&2)!=0);return true;
        }
        return false;
    }
    if(kind==RESCUE) {
        if(n<4)return false;
        if(get32(p)==0) {
            if(!s.host||n!=16||!valid_who(get32(p+4)))return false;unsigned who=get32(p+4);int target=int32_t(get32(p+8));
            if(!team.rescue_enabled||!active_pose(who)||team.carrier[who]>=0||s.players[who].pose.control){send_reply(who,"Rescue is unavailable right now.");return true;}
            if(int32_t(SDL_GetTicks()-team.rescue_after[who])<0){send_reply(who,"Wait a few seconds before using rescue again.");return true;}
            SafeAnchor anchor=target<0?s.players[who].pose.checkpoint:(target<int(MAX_PLAYERS)&&active_pose(unsigned(target))?s.players[target].pose.safe:SafeAnchor{});
            if(target<0&&!anchor.valid)anchor=s.players[who].pose.safe;
            if(!anchor.valid||anchor.stage!=s.players[who].pose.stage||anchor.actual!=s.players[who].pose.actual){send_reply(who,"No safe position is available in your current section.");return true;}
            team.rescue_after[who]=SDL_GetTicks()+10000;
            unsigned char data[24];put32(data,1);put32(data+4,who);put32(data+8,s.epoch);put32(data+12,anchor.stage);put32(data+16,anchor.x);put32(data+20,anchor.y);
            if(int(who)==s.local_id){if(!warp_local(anchor.stage,anchor.x,anchor.y))team_notice("That position is outside your current section.");else team_notice("Rescued to a safe position.");}
            else send_to_player(who,RESCUE,data,sizeof(data));return true;
        }
        if(get32(p)==1) {
            if(s.host||n!=24||get32(p+4)!=unsigned(s.local_id))return false;
            if(get32(p+8)==s.epoch){if(warp_local(get32(p+12),get32(p+16),get32(p+20)))team_notice("Rescued to a safe position.");else team_notice("That position is outside your current section.");}return true;
        }
        return false;
    }
    if(kind==WORLD)return world_handle(channel,p,n);
    if(kind==TEAM_REPLY) {if(s.host||n!=164||get32(p)!=unsigned(s.local_id)||p[163])return false;team_notice((const char*)p+4);return true;}
    if(kind==WATCH) {
        if(n!=12||get32(p)>1||get32(p+4)>=MAX_PLAYERS)return false;
        unsigned who=get32(p+4);int target=int32_t(get32(p+8));
        if(s.host) {
            if(get32(p)!=0||!valid_who(who)||target>=int(MAX_PLAYERS)||target<-1)return false;
            if(target>=0&&(target==int(who)||!s.players[target].connected||s.players[who].arrival_epoch!=s.epoch)){send_reply(who,"Spectating is available after you finish the act.");return true;}
            team.watchers[who]=target;unsigned char data[12];put32(data,1);put32(data+4,who);put32(data+8,unsigned(target));broadcast(WATCH,data,sizeof(data));return true;
        }
        if(get32(p)!=1||target>=int(MAX_PLAYERS)||target<-1)return false;team.watchers[who]=target;return true;
    }
    if(kind==VIDEO) {
        if(n<24||n>8024)return false;unsigned who=get32(p),sequence=get32(p+4),offset=get32(p+8),width=get32(p+12),height=get32(p+16),epoch=get32(p+20);
        if(!valid_who(who)||width!=192||height<1||height>128||offset+(n-24)>width*height*2||epoch!=s.epoch)return false;
        if(s.host) {
            for(unsigned i=1;i<MAX_PLAYERS;i++)if(team.watchers[i]==int(who))send_to_player(i,VIDEO,p,n);
            if(team.watchers[0]!=int(who))return true;
        } else if(team.watch!=int(who))return true;
        auto &v=team.video[who];if(offset==0){if(sequence<=v.sequence)return true;v.sequence=sequence;v.received=0;v.width=width;v.height=height;}
        if(sequence!=v.sequence||offset!=v.received)return true;
        memcpy(v.incoming.data()+offset,p+24,n-24);v.received+=n-24;
        if(v.received==width*height*2){v.pixels.resize(width*height);for(unsigned i=0;i<width*height;i++){unsigned rgb=get16(v.incoming.data()+i*2);v.pixels[i]=0xff000000|(((rgb>>11)&31)*255/31<<16)|(((rgb>>5)&63)*255/63<<8)|((rgb&31)*255/31);}v.complete=sequence;v.when=SDL_GetTicks();}return true;
    }
    return false;
}
void team_capture(SDL_Renderer *renderer) {
    unsigned now=SDL_GetTicks();if(s.phase!=PLAY||s.local_id<0||int32_t(now-team.last_video)<200)return;
    bool requested=false;for(unsigned i=0;i<MAX_PLAYERS;i++)if(s.players[i].connected&&team.watchers[i]==s.local_id)requested=true;
    if(!requested)return;team.last_video=now;
    if(!s.host&&channels[0].tx_size>8000)return;
    int width=0,height=0;SDL_GetRendererOutputSize(renderer,&width,&height);if(width<1||height<1||width>8192||height>8192)return;
    unsigned small_h=(std::min)(128u,unsigned(height*192/width));if(!small_h)return;
    // Read the physical target, not SDL's letterboxed logical viewport.
    // Otherwise widescreen spectators lose the top rows and receive a black tail.
    int logical_w=0,logical_h=0;SDL_RenderGetLogicalSize(renderer,&logical_w,&logical_h);
    SDL_RenderSetLogicalSize(renderer,0,0);
    std::vector<uint32_t> full(size_t(width)*height);
    int captured=SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,full.data(),width*4);
    SDL_RenderSetLogicalSize(renderer,logical_w,logical_h);if(captured)return;
    std::vector<unsigned char> rgb(192*small_h*2);
    for(unsigned y=0;y<small_h;y++)for(unsigned x=0;x<192;x++){unsigned color=full[size_t(y)*height/small_h*width+x*width/192];unsigned r=(color>>16)&255,g=(color>>8)&255,b=color&255;put16(rgb.data()+(y*192+x)*2,((r*31/255)<<11)|((g*63/255)<<5)|(b*31/255));}
    unsigned sequence=++team.video_sequence;
    for(unsigned offset=0;offset<rgb.size();) {
        unsigned count=(std::min)(8000u,unsigned(rgb.size())-offset);std::array<unsigned char,8024> data;
        put32(data.data(),unsigned(s.local_id));put32(data.data()+4,sequence);put32(data.data()+8,offset);put32(data.data()+12,192);put32(data.data()+16,small_h);put32(data.data()+20,s.epoch);memcpy(data.data()+24,rgb.data()+offset,count);
        if(s.host){for(unsigned i=1;i<MAX_PLAYERS;i++)if(team.watchers[i]==s.local_id)send_to_player(i,VIDEO,data.data(),count+24);}
        else send_packet(0,VIDEO,data.data(),count+24);offset+=count;
    }
}
void team_nameplate(SDL_Renderer *renderer,unsigned who,int x,int y) {
    if(!show_names||s.phase!=PLAY||title_card_active()||side_trip()||who>=MAX_PLAYERS)return;
    int wx=0,wy=0;SDL_RenderLogicalToWindow(renderer,float(x),float(y-30),&wx,&wy);
    team.labels[who]=ImVec2(float(wx),float(wy));team.label_visible[who]=true;
}
void draw_team_names() {
    team.label_count=0;auto *draw=ImGui::GetBackgroundDrawList();
    for(unsigned who=0;who<MAX_PLAYERS;who++) {
        bool visible=team.label_visible[who];team.label_visible[who]=false;
        if(!visible||!show_names||s.phase!=PLAY||title_card_active()||side_trip())continue;
        const char *name=player_name(who);ImVec2 size=ImGui::CalcTextSize(name),pos(team.labels[who].x-size.x*.5f,team.labels[who].y-size.y);
        if(pos.x+size.x<0||pos.x>ImGui::GetMainViewport()->Size.x||pos.y<0)continue;
        draw->AddRectFilled(ImVec2(pos.x-3,pos.y-2),ImVec2(pos.x+size.x+3,pos.y+size.y+2),IM_COL32(6,12,25,190),3);
        draw->AddText(pos,IM_COL32(255,255,255,255),name);team.label_count++;
    }
}
void draw_team_ui() {
    draw_team_names();
    if(team.notice[0]&&int32_t(SDL_GetTicks()-team.last_notice)<5000){ImGui::SetNextWindowPos(ImVec2(12,60),ImGuiCond_Always);ImGui::SetNextWindowBgAlpha(.9f);if(ImGui::Begin("##teamnotice",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings))ImGui::TextUnformatted(team.notice);ImGui::End();}
    if(team.watch>=0&&s.waiting) {
        auto &v=team.video[team.watch];ImGui::SetNextWindowSize(ImVec2(520,350),ImGuiCond_FirstUseEver);
        bool open=true;if(ImGui::Begin("Spectating",&open,ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("Watching %s",player_name(unsigned(team.watch)));ImGui::SameLine();if(ImGui::Button("Stop watching"))open=false;
            if(v.complete&&!v.pixels.empty()) {
                // Renderer texture updates are performed at the start of native drawing.
                if(v.texture){ImVec2 area=ImGui::GetContentRegionAvail();float scale=(std::min)(area.x/v.width,area.y/v.height);ImGui::Image((ImTextureID)(intptr_t)v.texture,ImVec2(v.width*scale,v.height*scale));}
            } else ImGui::TextWrapped("Waiting for the player's live view...");
            if(v.when&&int32_t(SDL_GetTicks()-v.when)>3000)ImGui::TextDisabled("Waiting for a new frame...");
        }ImGui::End();if(!open)watch_request(-1);
    }
    if(!team.open)return;
    ImGuiViewport *viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x+viewport->WorkSize.x*.5f,viewport->WorkPos.y+viewport->WorkSize.y*.5f),ImGuiCond_FirstUseEver,ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2((std::min)(420.0f,viewport->WorkSize.x-20),(std::min)(650.0f,viewport->WorkSize.y-35)),ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Team",&team.open,ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("%u players | F2 / Y: close",player_count());
        if(ImGui::InputText("Your name",local_name,sizeof(local_name),ImGuiInputTextFlags_EnterReturnsTrue))send_name();ImGui::SameLine();if(ImGui::Button("Apply"))send_name();
        if(ImGui::Checkbox("Show names above players",&show_names))save_identity();
        if(s.host&&ImGui::Checkbox("Allow rescue teleport",&team.rescue_enabled)){unsigned char p[4];put32(p,team.rescue_enabled?1:0);broadcast(OPTIONS,p,4);}
        ImGui::Separator();emerald_draw_tracker();
        for(unsigned i=0;i<MAX_PLAYERS;i++)if(s.players[i].present) {
            auto &v=s.players[i];ImGui::PushID(int(i));ImGui::Text("%s (%s)%s",player_name(i),characters[v.character],v.connected?"":" - reconnecting");
            if(int(i)!=s.local_id&&v.connected&&s.phase==PLAY) {
                if(s.waiting){if(ImGui::Button("Watch"))watch_request(int(i));}
                else if(team.rescue_enabled){ImGui::BeginDisabled(!active_pose(i)||!v.pose.safe.valid);if(ImGui::Button("Rescue to teammate"))rescue_request(int(i));ImGui::EndDisabled();}
            }
            if(team.carrier[i]>=0)ImGui::TextDisabled("Carried by %s",player_name(unsigned(team.carrier[i])));
            ImGui::PopID();ImGui::Separator();
        }
        if(team.rescue_enabled&&!s.waiting&&ImGui::Button("Rescue to my checkpoint"))rescue_request(-1);
        if(s.local_id>=0&&(team.carrier[s.local_id]>=0||team.cargo[s.local_id]>=0)){ImGui::SameLine();if(ImGui::Button("Let go"))request_drop(false);}
        ImGui::TextWrapped("Jump underneath a flying Tails to grab on. Jump again to let go. Tails can press Down + Jump to drop a passenger.");
    }ImGui::End();
}
void team_video_textures(SDL_Renderer *renderer) {
    for(auto &v:team.video)if(v.complete&&!v.pixels.empty()) {
        if(v.texture&&(v.texture_width!=v.width||v.texture_height!=v.height)){SDL_DestroyTexture(v.texture);v.texture=nullptr;}
        v.texture_width=v.width;v.texture_height=v.height;
        if(!v.texture){v.texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,int(v.width),int(v.height));if(v.texture)SDL_SetTextureScaleMode(v.texture,SDL_ScaleModeNearest);}
        if(v.texture)SDL_UpdateTexture(v.texture,nullptr,v.pixels.data(),int(v.width*4));
    }
}
bool team_debug(int id,const char *action,const char *json) {
    auto number=[&](const char *key,int fallback=0){char needle[48];snprintf(needle,sizeof(needle),"\"%s\"",key);const char *p=strstr(json,needle);if(!p||(p=strchr(p,':'))==nullptr)return fallback;return atoi(p+1);};
    if(!strcmp(action,"name")){const char *p=strstr(json,"\"name\"");if(p&&(p=strchr(p,':'))&&(p=strchr(p,'"'))){char text[25]{};unsigned i=0;for(p++;p[i]&&p[i]!='"'&&i<24;i++)text[i]=p[i];clean_name(local_name,(unsigned char*)text,24);send_name();}}
    else if(!strcmp(action,"team"))team_toggle();
    else if(!strcmp(action,"ping"))send_ping(unsigned(number("value")),unsigned(number("x",int(level_map.local_x))),unsigned(number("y",int(level_map.local_y))));
    else if(!strcmp(action,"rescue"))rescue_request(number("value",-1));
    else if(!strcmp(action,"drop"))request_drop(number("value")!=0);
    else if(!strcmp(action,"watch"))watch_request(number("value",-1));
    else if(!strcmp(action,"resume"))resume_ticket();
    else if(!strcmp(action,"disconnect")){if(s.host){int who=number("value",1);for(unsigned i=1;i<MAX_CHANNELS;i++)if(channels[i].peer==who){disconnect_channel(i);break;}}else disconnect_channel(0);}
    else if(!strcmp(action,"rescue_option")){if(s.host){team.rescue_enabled=number("value")!=0;unsigned char data[4];put32(data,team.rescue_enabled);broadcast(OPTIONS,data,4);}}
    else if(!strcmp(action,"team_status")){}
    else if(world_debug(id,action))return true;
    else return false;
    char result[4000];unsigned pos=unsigned(snprintf(result,sizeof(result),"{\"id\":%d,\"ok\":true,\"labels\":%u,\"emerald_art\":%u,\"watch\":%d,\"carry_owned\":%d,\"reconnecting\":%d,\"players\":[",id,team.label_count,emerald_art_count(),team.watch,team.carry_owned,s.reconnecting));
    bool first=true;for(unsigned i=0;i<MAX_PLAYERS;i++)if(s.players[i].present){auto &v=s.players[i];pos+=unsigned(snprintf(result+pos,sizeof(result)-pos,"%s{\"id\":%u,\"name\":\"%s\",\"connected\":%d,\"carrier\":%d,\"cargo\":%d,\"chaos\":%u,\"super\":%u,\"ping\":%u,\"video\":%u,\"x\":%u,\"y\":%u,\"flight\":%u,\"control\":%u}",first?"":",",i,player_name(i),v.connected,team.carrier[i],team.cargo[i],v.chaos_mask,v.super_mask,team.pings[i].sequence,team.video[i].complete,v.pose.x,v.pose.y,v.pose.flight,v.pose.control));first=false;}
    snprintf(result+pos,sizeof(result)-pos,"]}");cmd_send_response(result);return true;
}
#include "sonic3k_coop_world.inc"

void team_reset_peer(unsigned who){if(who<MAX_PLAYERS){remote_motion[who].clear();world.sequences[who]=0;team.pings[who]=TeamPing{};team.video[who].sequence=0;team.action_sequence[who]=0;}}
