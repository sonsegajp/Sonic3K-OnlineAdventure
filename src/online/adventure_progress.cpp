#include "adventure_progress.h"
#include <recomp_net/rb_driver.h>
#include <retcomm_rbengine/snap_ring.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <deque>

namespace adventure {
namespace {
uint32_t now() {return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct Codec {
    Image &a; unsigned at=0;
    void put(uint32_t v,unsigned n=4){while(n)a[at++]=uint8_t(v>>(--n*8));}
    uint32_t get(unsigned n=4){uint32_t v=0;while(n--)v=(v<<8)|a[at++];return v;}
};
uint32_t hash(const uint8_t *p,unsigned n){uint32_t h=2166136261u;while(n--){h^=*p++;h*=16777619u;}return h;}
}
State initial(uint32_t mask,uint32_t epoch){State s;s.occupied=mask;s.epoch=epoch;s.arrival_epoch.fill(~0u);s.award_owner.fill(255);return s;}
Image encode(const State &s){
    Image a{};Codec c{a};c.put(0x4f415032);c.put(s.tick);c.put(s.epoch);c.put(s.occupied);c.put(s.arrived);c.put(s.fault);
    for(auto v:s.rings)c.put(v);
    for(auto v:s.arrival_epoch)c.put(v);
    for(auto v:s.stage)c.put(v);
    for(auto v:s.last_command)c.put(v,2);
    for(auto v:s.emeralds)c.put(v,1);
    for(auto v:s.award_owner)c.put(v,1);
    c.put(s.converted,1);
    return a;
}
bool decode(const Image &src,State &s){
    Image a=src;Codec c{a};State t;
    if(c.get()!=0x4f415032)return false;
    t.tick=c.get();t.epoch=c.get();t.occupied=c.get();t.arrived=c.get();t.fault=c.get();
    if(!t.occupied||(t.occupied&~255u)||(t.arrived&~t.occupied)||t.fault>1)return false;
    for(auto &v:t.rings)v=c.get();
    for(auto &v:t.arrival_epoch)v=c.get();
    for(auto &v:t.stage)v=c.get();
    for(auto &v:t.last_command)v=uint16_t(c.get(2));
    for(auto &v:t.emeralds){v=uint8_t(c.get(1));if(v>3)return false;}
    for(auto &v:t.award_owner){v=uint8_t(c.get(1));if(v>=seats&&v!=255)return false;}
    t.converted=uint8_t(c.get(1));if(t.converted>1)return false;
    while(c.at<a.size())if(c.get(1))return false;
    s=t;return true;
}
uint32_t digest(const State &s){auto a=encode(s);return hash(a.data(),unsigned(a.size()));}
bool reduce(State &s,const std::array<uint16_t,seats> &rows){
    // Stable seat order settles simultaneous awards consistently on all peers.
    for(unsigned who=0;who<seats;who++){
        if(!(s.occupied&(1u<<who)))continue;
        unsigned value=rows[who],op=(value>>12)&7,payload=value&4095;
        if(value==s.last_command[who])continue;
        s.last_command[who]=uint16_t(value);
        switch(Command(op)){
        case Command::arrive:
            if(s.arrived&(1u<<who))break;
            if(payload!=arrival_any&&payload>=arrival_zones*2)break;
            s.arrived|=1u<<who;s.arrival_epoch[who]=s.epoch;
            s.stage[who]=payload==arrival_any?~0u:((payload>>1)<<8)|(payload&1);
            break;
        case Command::ring:
            if(payload<zones*32)s.rings[payload/32]|=1u<<(payload%32);
            break;
        case Command::emerald:{
            unsigned gem=payload&7,value=(payload>>3)&3;
            if(payload>31||gem>=7||!value)break;
            if(!s.emeralds[gem]&&value==1)s.award_owner[gem]=uint8_t(who);
            s.emeralds[gem]=uint8_t(std::max(unsigned(s.emeralds[gem]),value));
            break;
        }
        case Command::convert:if(!payload)s.converted=1;break;
        default:break;
        }
    }
    if(s.arrived==s.occupied){
        uint32_t stage=0;bool first=true;
        for(unsigned i=0;i<seats;i++)if(s.occupied&(1u<<i)){
            if(!first&&s.stage[i]!=stage)s.fault=1;
            stage=s.stage[i];first=false;
        }
        if(!s.fault){s.epoch++;s.arrived=0;}
    }
    s.tick++;return !s.fault;
}

struct Progress::Impl {
    RNetSession *session=nullptr;
    RNetRbDriver *driver=nullptr;
    RbeSnapRing *snap=nullptr;
    State sim{},committed{};
    struct History {uint32_t key=~0u;State state{};};
    std::array<History,512> history{};
    std::array<uint16_t,seats> rows{};
    std::deque<uint16_t> queue;
    int local=0,slots=8,delay=2,prediction=6;
    uint32_t content=0,next_ms=0,committed_tick=0;
    uint16_t toggle=0;
    bool started=false,replaying=false,have_commit=false;
    char error[160]{};
    static Impl &self(void *ctx){return *static_cast<Impl*>(ctx);}
    static int save(void *ctx,uint32_t tick){
        auto &p=self(ctx);auto image=encode(p.sim);
        auto *data=static_cast<uint8_t*>(malloc(image.size()));if(!data)return 0;
        memcpy(data,image.data(),image.size());
        if(!rbe_snap_ring_store(p.snap,tick,data,image.size())){free(data);return 0;}return 1;
    }
    static int load(void *ctx,uint32_t tick){
        auto &p=self(ctx);size_t size=0;const void *data=rbe_snap_ring_peek(p.snap,tick,&size);
        if(!data||size!=state_bytes)return 0;
        Image image;memcpy(image.data(),data,size);return decode(image,p.sim)?1:0;
    }
    static int has(void *ctx,uint32_t tick){return rbe_snap_ring_has(self(ctx).snap,tick);}
    static int oldest(void *ctx,uint32_t *tick){auto *ring=self(ctx).snap;if(!rbe_snap_ring_count(ring))return 0;*tick=rbe_snap_ring_oldest_tick(ring);return 1;}
    static void drop(void *ctx,uint32_t tick){rbe_snap_ring_drop_after(self(ctx).snap,tick);}
    static void publish(void *ctx,uint32_t tick,const RNetRbFrame *rows,int count,int){
        auto &p=self(ctx);p.rows.fill(0);
        if(tick!=p.sim.tick)snprintf(p.error,sizeof(p.error),"Shared progress tick mismatch.");
        for(int i=0;i<count;i++)p.rows[i]=rows[i].buttons;
    }
    static int run(void *ctx,uint32_t tick){
        auto &p=self(ctx);if(tick!=p.sim.tick)return 0;
        reduce(p.sim,p.rows);p.history[tick%p.history.size()]={tick,p.sim};return 1;
    }
    static void replay_begin(void *ctx){self(ctx).replaying=true;}
    static void replay_end(void *ctx){self(ctx).replaying=false;}
    static uint32_t master(void *ctx){return digest(self(ctx).sim);}
    static void parts(void *ctx,RNetRbDigestParts *out){
        auto a=encode(self(ctx).sim);out->master=hash(a.data(),unsigned(a.size()));
        out->part[0]=hash(a.data()+4,20);out->part[1]=hash(a.data()+24,56);out->part[2]=hash(a.data()+80,unsigned(a.size()-80));
    }
    static void decode_sample(void*,int,const RNetInputSample *in,RNetRbFrame *out){out->buttons=in->size==2?uint16_t((in->bytes[0]<<8)|in->bytes[1]):0;out->stick_x=out->stick_y=out->analog=0;}
    static void neutral(void*,int,RNetRbFrame *out){out->buttons=0;out->stick_x=out->stick_y=out->analog=0;}
    static void refuse(void *ctx){auto &p=self(ctx);snprintf(p.error,sizeof(p.error),"Shared progress refused: %s",rnet_rb_driver_refusal(p.driver));}
    static uint32_t clock(void*){return now();}
    static void sample(uint32_t,RNetInputSample *out,void *ctx){
        auto &p=self(ctx);uint16_t command=0;
        if(!p.queue.empty()){command=p.queue.front();p.queue.pop_front();}
        out->size=2;out->valid=1;out->bytes[0]=uint8_t(command>>8);out->bytes[1]=uint8_t(command);
    }
    static void unused(uint32_t,const RNetInputSample*,int,void*){}
    static int check_mods(const char *want,char *reason,uint32_t cap){
        if(want&&!strcmp(want,"online-adventure progress-v1\n"))return 0;
        if(cap)snprintf(reason,cap,"Different shared adventure rules.");return -1;
    }
    bool start_driver(){
        RNetRbDriverConfig c{};c.session=&session;c.local_slot=&local;c.slot_count=&slots;c.input_delay=&delay;c.input_prediction=&prediction;
        c.occupied_mask=sim.occupied;c.replay_mode=RNET_RB_REPLAY_INLINE;c.snap_depth=128;
        c.part_names[0]="barrier";c.part_names[1]="big-rings";c.part_names[2]="inventory";
        c.log_prefix="adventure_rb";c.env_alias="ADVENTURE_RB";
        RNetRbHost h{};h.ctx=this;h.snap_save=save;h.snap_load=load;h.snap_has=has;h.snap_oldest=oldest;h.snap_drop_after=drop;
        h.publish=publish;h.run_tick=run;h.resim_begin=replay_begin;h.resim_end=replay_end;h.digest_master=master;h.digest_parts=parts;
        h.decode_sample=decode_sample;h.neutral_row=neutral;h.request_return_to_lobby=refuse;h.now_ms=clock;
        driver=rnet_rb_driver_create();if(!driver)return false;
        rnet_rb_driver_set_identity(driver,0x4f415032,content);
        rnet_rb_driver_set_modset(driver,"online-adventure progress-v1\n",check_mods,nullptr);
        started=rnet_rb_driver_start(driver,&c,&h)!=0;return started;
    }
};
Progress::Progress():p(new Impl){}
Progress::~Progress(){stop();}
bool Progress::start(const Config &c){
    stop();p.reset(new Impl);p->local=c.local;p->content=c.content;p->sim=p->committed=c.seed;
    // Every network generation starts at tick zero, preserving adventure progress.
    p->sim.tick=p->committed.tick=0;p->sim.last_command.fill(0);p->committed.last_command.fill(0);
    if(c.local<0||c.local>=int(seats)||!(c.seed.occupied&(1u<<c.local))||!(c.seed.occupied&1)||!c.session){snprintf(p->error,sizeof(p->error),"Invalid shared adventure configuration.");return false;}
    p->snap=rbe_snap_ring_create(128);if(!p->snap)return false;
    RNetConfig cfg;rnet_config_init_defaults(&cfg);cfg.slot_count=8;cfg.local_slot=uint8_t(c.local);cfg.occupied_mask=c.seed.occupied;cfg.session_id=c.session;
    RNetHostVTable h{};h.sample_local=Impl::sample;h.publish=Impl::unused;h.ctx=p.get();
    p->session=rnet_session_create(&cfg,&h);
    if(!p->session|| (c.local==0?rnet_session_start_lan_hub(p->session,c.bind):rnet_session_start_lan(p->session,c.bind,c.peer))!=0){snprintf(p->error,sizeof(p->error),"Could not open the shared progress UDP connection.");return false;}
    p->next_ms=now();return true;
}
void Progress::stop(){if(p->driver){rnet_rb_driver_destroy(p->driver);p->driver=nullptr;}if(p->session){rnet_session_destroy(p->session);p->session=nullptr;}if(p->snap){rbe_snap_ring_destroy(p->snap);p->snap=nullptr;}p->started=false;}
void Progress::poll(){
    if(!p->session||p->error[0])return;
    rnet_session_pump(p->session);
    if(!rnet_session_is_running(p->session))return;
    if(!p->started&&!p->start_driver()){snprintf(p->error,sizeof(p->error),"Shared progress driver could not start.");return;}
    uint32_t t=now();if(int32_t(t-p->next_ms)<0)return;p->next_ms=t+16;
    auto result=rnet_rb_driver_poll_admit(p->driver);
    if(result!=RNET_RB_ADMIT_STALL){if(!Impl::run(p.get(),rnet_rb_driver_sim_tick(p->driver))){snprintf(p->error,sizeof(p->error),"Shared progress replay failed.");return;}rnet_rb_driver_finish_frame(p->driver);}
    // Side effects only observe the agreed historical state. Tentative/replayed
    // awards cannot despawn a native ring, release an act or display a toast.
    uint32_t through=rnet_rb_driver_confirmed_through(p->driver);
    if(through&&(!p->have_commit||through>p->committed_tick)){
        const auto &h=p->history[through%p->history.size()];
        if(h.key!=through){snprintf(p->error,sizeof(p->error),"Shared progress commit history expired.");return;}
        p->committed=h.state;p->committed_tick=through;p->have_commit=true;
    }
    if(p->committed.fault)snprintf(p->error,sizeof(p->error),"Players reached different acts.");
}
bool Progress::submit(Command op,unsigned payload){
    if(!active()||payload>4095||p->queue.size()>=256)return false;
    p->toggle^=0x8000;p->queue.push_back(uint16_t(p->toggle|(unsigned(op)<<12)|payload));return true;
}
const State &Progress::committed()const{return p->committed;}
Stats Progress::stats()const{Stats s;s.linked=p->session&&rnet_session_is_running(p->session);s.started=p->started;if(p->driver){s.tick=rnet_rb_driver_sim_tick(p->driver);s.confirmed=rnet_rb_driver_confirmed_through(p->driver);s.episodes=rnet_rb_driver_episode_count(p->driver);s.desyncs=rnet_rb_driver_desync_count(p->driver);s.replayed=rnet_rb_driver_resim_ticks(p->driver);}return s;}
const char *Progress::error()const{return p->error;}
bool Progress::active()const{return p->session&&!p->error[0];}
void Progress::drain(){if(p->driver)rnet_rb_driver_request_quiesce(p->driver);}
bool Progress::drained()const{return p->driver&&rnet_rb_driver_quiesce_state(p->driver)==RNET_RB_QUIESCE_DRAINED;}
}
