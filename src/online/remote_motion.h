#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

namespace adventure {
inline float wrapped_delta(float value,float period){return period>0?std::remainder(value,period):value;}
inline float wrapped_position(float value,float period){if(period<=0)return value;value=std::fmod(value,period);return value<0?value+period:value;}
// Presentation only: snapshots retain their sender's frame time, even when
// several packets arrive together. Native physics and progress stay authoritative.
template<class Image> class RemoteMotion {
public:
    static constexpr double frame_ms=1000.0/59.94006, delay_ms=3*frame_ms;
    struct Frame {
        uint32_t tick=0,epoch=0,stage=0,warp=0;
        bool visible=false;
        float x=0,y=0,y_period=0;
        Image image{};
        double time=0;
    };
    struct View {float x=0,y=0;const Frame *frame=nullptr;bool extrapolated=false;};
    void clear(){frames.clear();started=false;}
    void push(Frame frame,double now) {
        bool reset=frames.empty();
        if(!reset) {
            const auto &last=frames.back();
            int32_t ticks=int32_t(frame.tick-last.tick);
            if(ticks<=0)return; // Duplicate or old packet; never rewind the view.
            reset=frame.epoch!=last.epoch||frame.stage!=last.stage||frame.warp!=last.warp||
                frame.visible!=last.visible||frame.y_period!=last.y_period||now-arrival>1000||ticks>120;
            if(!reset&&frame.y_period>0)frame.y=last.y+wrapped_delta(frame.y-last.y,frame.y_period);
            reset=reset||std::abs(frame.x-last.x)>256||std::abs(frame.y-last.y)>256;
            frame.time=last.time+ticks*frame_ms;
        }
        if(reset) {
            frames.clear();frame.time=0;offset=now;cursor=-delay_ms;clock=now;started=true;
        } else {
            // Track clock offset through the least delayed packets. Slow upward
            // drift accommodates clock skew without feeding packet jitter into motion.
            offset=(std::min)(offset+(now-arrival)*.001,now-frame.time);
        }
        arrival=now;frames.push_back(std::move(frame));
        while(frames.size()>32)frames.pop_front();
    }
    View sample(double now) {
        if(frames.empty())return {};
        if(started&&now>clock) {
            double elapsed=now-clock,target=now-offset-delay_ms;
            // Correct drift gently. A delayed packet must not create a fast-forward burst.
            double error=target-(cursor+elapsed);
            cursor+=elapsed*(1.0+std::clamp(error/250.0,-.10,.10));clock=now;
        }
        const Frame *a=&frames.front(),*b=a;
        for(const auto &f:frames){if(f.time<=cursor)a=&f;else{b=&f;break;}b=&f;}
        if(cursor<=frames.front().time)return {frames.front().x,frames.front().y,&frames.front(),false};
        if(b->time>a->time) {
            float t=float(std::clamp((cursor-a->time)/(b->time-a->time),0.0,1.0));
            return {a->x+(b->x-a->x)*t,a->y+(b->y-a->y)*t,a,false};
        }
        // Bridge a brief missed update, then stop. Do not fly offscreen during
        // a stalled connection, paused game, special stage, or level transition.
        double extra=std::clamp(cursor-a->time,0.0,2*frame_ms);
        if(frames.size()>1&&extra>0&&a->visible) {
            const auto &previous=frames[frames.size()-2];double dt=a->time-previous.time;
            if(dt>0){float vx=float((a->x-previous.x)/dt),vy=float((a->y-previous.y)/dt);
                vx=std::clamp(vx,-1.5f,1.5f);vy=std::clamp(vy,-1.5f,1.5f);
                return {a->x+vx*float(extra),a->y+vy*float(extra),a,true};}
        }
        return {a->x,a->y,a,false};
    }
    size_t size() const{return frames.size();}
private:
    std::deque<Frame> frames;
    double offset=0,arrival=0,cursor=0,clock=0;
    bool started=false;
};
}
