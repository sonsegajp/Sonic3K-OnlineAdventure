#include "remote_motion.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>
using Track=adventure::RemoteMotion<int>;
static void check(bool yes,const char *why){if(!yes){fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static Track::Frame frame(unsigned tick,float x,float y=100){Track::Frame f;f.tick=tick;f.visible=true;f.x=x;f.y=y;return f;}
int main(){
    // Eight clocks with different link delay and deterministic packet jitter.
    // Include dropped snapshots and coalesced deliveries. Sender frame times
    // must yield continuous motion rather than repeating network arrival steps.
    for(int peer=0;peer<8;peer++){
        Track track;struct Packet{double when;Track::Frame f;};std::vector<Packet> packets;
        double last_delivery=0;
        for(unsigned tick=0;tick<480;tick++){
            if(tick%29==17)continue;
            double jitter=((tick*17+peer*7)%19)*1.2;
            last_delivery=(std::max)(last_delivery,tick*Track::frame_ms+peer*12+jitter);
            packets.push_back({last_delivery,frame(tick,100+tick*2.0f)});
        }
        size_t next=0;float prev=0;unsigned repeats=0;double max_step=0,error=0;
        for(unsigned step=0;step<465;step++){
            double now=step*Track::frame_ms;
            while(next<packets.size()&&packets[next].when<=now){track.push(packets[next].f,packets[next].when);next++;}
            auto view=track.sample(now);
            if(step>35){double dx=view.x-prev;check(dx>=-.001,"jitter reversed a moving player");max_step=(std::max)(max_step,dx);if(std::abs(dx)<.01)repeats++;error+=std::abs(dx-2);}
            prev=view.x;
        }
        check(repeats==0,"movement stalled between delivered updates");check(max_step<2.25,"arrival jitter produced fast-forward movement");
        printf("peer %d: max step %.4f, mean speed error %.4f, repeated frames %u\n",peer,max_step,error/429,repeats);
    }
    Track track;track.push(frame(0,0),0);track.push(frame(1,2),Track::frame_ms);
    auto held=track.sample(3000);check(held.x<=6.01,"missing updates extrapolated without a bound");
    auto warp=frame(2,4000);warp.warp=1;track.push(warp,3010);check(track.sample(3010).x==4000,"teleport was smeared through the level");
    auto stage=frame(3,100);stage.stage=1;track.push(stage,3020);check(track.sample(3020).x==100,"stage change retained previous position");
    track.push(stage,3030);check(track.size()==1,"duplicate frame was accepted");
    track.clear();track.push(frame(0xfffffffeu,100),0);track.push(frame(1,106),3*Track::frame_ms);check(track.size()==2,"sender frame wrap dropped new samples");
    auto special=frame(2,200);special.visible=false;track.push(special,4*Track::frame_ms);check(track.sample(1000).x==200,"hidden special-stage player extrapolated");
    for(float period:{2048.0f,4096.0f}){
        Track loop;float previous=0;
        for(unsigned tick=0;tick<40;tick++){
            auto f=frame(tick,100,adventure::wrapped_position(period-16+tick*2,period));f.y_period=period;
            loop.push(f,tick*Track::frame_ms);auto v=loop.sample(tick*Track::frame_ms);
            if(tick>5){check(v.y>=previous,"vertical loop reversed interpolation");check(v.y-previous<2.25f,"vertical loop caused a teleport");}
            previous=v.y;
        }
        check(loop.size()>1,"vertical loop discarded the interpolation history");
        check(adventure::wrapped_delta(32-(period-96),period)==128,"wrapped viewport rejected a visible object");
        check(adventure::wrapped_delta(16-(period-16),period)==32,"wrapped carry contact missed the passenger");
    }
    puts("PASS: native 2048/4096 vertical loops, wrapped viewport and carry distances");
    puts("PASS: eight jittered peers, loss, bounded stall, warp, level change, duplicates, frame wrap, hidden player");
}
