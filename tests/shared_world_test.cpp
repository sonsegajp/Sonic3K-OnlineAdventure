#include "shared_world.h"
#include "world_codec.h"
#include "shared_terrain.h"
#include "foreground_tiles.h"
#include <cstdio>
#include <cstdlib>
using namespace adventure;
static void check(bool yes,const char *message){if(!yes){fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    {
        SharedWorld assembly;assembly.set_active(7);ObjectImage body,arm,hand,shot;
        body.bytes[16]=arm.bytes[16]=hand.bytes[16]=1;
        body.children=7;arm.parent=100;arm.reference_mask=1u<<12;arm.references[12]=100;
        hand.parent=100;hand.reference_mask=1u<<12;hand.references[12]=101;
        EntityKey root{9,0x4000400,100},limb{9,0x4000400,101},tip{9,0x4000400,102};
        auto *a=assembly.observe(0,root,body,100,150*150);
        auto *b=assembly.observe(1,limb,arm,100,0);
        auto *c=assembly.observe(2,tip,hand,100,0,true);
        assembly.observe(2,root,body,101,100*100);
        auto old=b->generation;auto changes=assembly.handoff_all(102);
        check(a->owner==2&&b->owner==2&&c->owner==2,"contact with a hand split multipart authority");
        check(changes.size()==2&&a->image.children==7,"assembly handoff lost state or failed to publish every changed part");
        check(!assembly.update(1,limb,old,1,arm),"old limb owner overwrote the shared assembly");
        assembly.observe(1,root,body,103,0);assembly.observe(1,limb,arm,103,0,true);
        assembly.handoff_all(104);check(a->owner==2&&b->owner==2&&c->owner==2,"two contacting players oscillated assembly authority");
        assembly.set_active(3);assembly.handoff_all(105);
        check(a->owner==1&&b->owner==1&&c->owner==1,"disconnect did not transfer the complete assembly");
        shot=arm;shot.bytes[16]=8;EntityKey projectile{9,0x4000400,103};
        auto *p=assembly.observe(0,projectile,shot,106,0);assembly.handoff_all(107);
        check(p->owner==0,"distant projectile was tied to an unloaded parent");
        puts("PASS: multipart contact, simultaneous contacts, disconnect, stale limb updates and detached projectile authority");
    }
    {
        std::array<uint8_t,65536> ram{},vram{};vram.fill(0xa5);
        auto put=[&](unsigned a,unsigned v){ram[a]=uint8_t(v>>8);ram[a+1]=uint8_t(v);};
        put(0x8000,8);put(0x8004,16);put(0xeeae,0x3c);
        for(unsigned i=0;i<16;i++)put(0x8008+i*4,0x8108+i*8);
        for(unsigned i=0;i<128;i++)ram[0x8108+i]=1;
        for(unsigned i=0;i<64;i++)put(128+i*2,1);
        put(0x9008,0x2011);put(0x900a,0x2012);put(0x900c,0x2013);put(0x900e,0x2014);
        put(130,0x401);put(132,0x801);put(134,0xc01);
        const auto original=ram;redraw_foreground(ram.data(),vram.data());
        auto tile=[&](unsigned x,unsigned y){unsigned a=0xc000+y*128+x*2;return unsigned(vram[a])<<8|vram[a+1];};
        check(tile(0,0)==0x2011&&tile(1,1)==0x2014,"foreground native block order");
        check(tile(2,0)==0x2812&&tile(4,0)==0x3013&&tile(6,0)==0x3814,"foreground block flips");
        check(ram==original,"foreground redraw modified game RAM");
        for(unsigned a=0;a<65536;a++)if(a<0xc000||a>=0xd000)check(vram[a]==0xa5,"foreground redraw escaped name table");
        put(0xee80,0xfff0);put(0xee84,0xfff0);redraw_foreground(ram.data(),vram.data());
        puts("PASS: bounded foreground refresh preserves RAM and character art, including wrapped/invalid coordinates");
    }
    SharedTerrain terrain;
    TerrainKey wall{1,0,0x8040};auto cell=terrain.change(wall,12,0);check(cell&&cell->value==0,"wall removal");
    auto version=cell->revision;check(terrain.change(wall,12,14)->value==0,"stale wall copy restored terrain");
    check(terrain.change(wall,0,15)->value==15,"subsequent terrain change");
    SharedTerrain replica;check(replica.import(wall,*cell),"terrain snapshot");check(!replica.import(wall,{version,12}),"stale terrain snapshot");
    check(!terrain.change({1,0,0xb010},0,1),"player RAM accepted as terrain");
    check(terrain.change({1,0,0xeb00},0x8080,0x8182)->value==0x0102,"local allocation bits shared");
    check(!terrain.find({1,1,0x8040}),"geometry crossed level boundary");
    for(unsigned seed=0;seed<200;seed++){
        ObjectImage a;unsigned state=seed+1;auto next=[&](){state=state*1664525u+1013904223u;return state;};
        for(unsigned i=0;i<74;i++){a.bytes[i]=(next()%4)?0:uint8_t(next());a.initial[i]=(next()%7)?0:uint8_t(next());}
        a.reference_mask=uint16_t(next()&0x3fff);a.initial_reference_mask=uint16_t(next()&0x3fff);a.children=next();
        for(unsigned i=0;i<14;i++){if(a.reference_mask&(1u<<i))a.references[i]=(uint64_t(next())<<32)|next();if(a.initial_reference_mask&(1u<<i))a.initial_references[i]=(uint64_t(next())<<32)|next();}
        a.parent=(uint64_t(next())<<32)|next();uint8_t data[408];size_t size=encode_world_image(data,a);ObjectImage b;
        check(size<=sizeof data&&decode_world_image(data,size,b)&&a==b,"world image codec round trip");
        for(size_t n=0;n<size;n++)check(!decode_world_image(data,n,b),"truncated world image accepted");
        data[9]|=0x80;check(!decode_world_image(data,size,b),"invalid sparse mask accepted");
    }
    SharedWorld host;host.set_active(255);ObjectImage initial;initial.bytes[0x29]=8;
    EntityKey monitor{1,0,42};auto *m=host.observe(3,monitor,initial,100,0);check(m,"spawn");
    unsigned rewards=0;
    for(unsigned who:{3u,1u,5u,0u,7u,4u,2u,6u,3u})if(host.consume(who,monitor)==WorldClaim::granted)rewards++;
    check(rewards==1&&m->collector==3,"simultaneous monitor attempts duplicated the reward");
    host.observe(7,monitor,initial,120,0);check(m->consumed&&m->collector==3,"late observer restored unbroken monitor");
    std::array<SharedWorld,8> peers;
    for(auto &peer:peers){check(peer.import(*m),"snapshot import");check(peer.find(monitor)->consumed,"peer missed broken state");}
    EntityKey badnik{1,0,77};auto *enemy=host.observe(0,badnik,initial,200,200);
    host.observe(1,badnik,initial,210,10);uint32_t old_generation=enemy->generation;
    ObjectImage moved=initial;moved.bytes[0x11]=80;check(host.update(0,badnik,old_generation,20,moved),"authority advance");
    check(!host.update(1,badnik,old_generation,21,initial),"two worlds controlled the same badnik");
    check(!host.update(0,badnik,old_generation,19,initial),"stale packet rewound object");
    host.set_active(254);host.handoff_all(220);
    check(enemy->owner==1&&enemy->image==moved,"authority handoff lost canonical state");
    check(!host.update(0,badnik,old_generation,22,initial),"disconnected authority resurrected old state");
    moved.bytes[0x11]++;check(host.update(1,badnik,enemy->generation,1,moved),"new authority could not continue");
    host.retire(1,badnik,enemy->generation,false);check(!enemy->removed,"offscreen badnik was destroyed");
    host.observe(7,badnik,initial,500,20);check(enemy->image==moved,"offscreen re-entry reset shared state");
    check(host.consume(7,badnik)==WorldClaim::granted,"enemy destruction");
    host.retire(7,badnik,enemy->generation,true);host.observe(2,badnik,initial,510,0);check(enemy->removed,"destroyed enemy respawned for another player");
    check(!peers[0].import(*peers[0].find(monitor)),"duplicate snapshot accepted");
    auto newer=*m;newer.revision++;newer.image.bytes[0x20]=10;check(peers[0].import(newer),"newer update rejected");check(!peers[0].import(*m),"old update revived old animation");
    EntityKey next_act{2,1,42};check(host.observe(2,next_act,initial,520,0)&&!host.find(next_act)->consumed,"act transition reused prior object lifetime");
    EntityKey platform{2,1,99};auto *p=host.observe(2,platform,initial,530,200*200);
    host.observe(3,platform,initial,531,20*20);check(host.handoff(*p,532)&&p->owner==3,"nearby interacting player did not acquire authority");
    host.observe(2,platform,initial,533,19*19);check(!host.handoff(*p,534)&&p->owner==3,"nearby authority jittered between adjacent players");
    host.observe(2,platform,initial,535,19*19,true);check(host.handoff(*p,536)&&p->owner==2,"native solid contact failed to acquire object");
    host.observe(3,platform,initial,537,0,true);check(!host.handoff(*p,538)&&p->owner==2,"simultaneous solid contacts oscillated authority");
    ObjectImage loaded=initial;loaded.initial[0]=0x12;loaded.initial[5]=0;
    EntityKey seeded{3,0x10000,200};auto *seed=host.observe(2,seeded,loaded,600,0);
    ObjectImage resumed=loaded;resumed.initial[0]=0x34;resumed.initial[5]=4;resumed.bytes[5]=4;
    check(host.update(2,seeded,seed->generation,1,resumed),"resumed object update");
    check(seed->image.initial==loaded.initial&&seed->image.bytes[5]==4,"returning owner replaced the native art initializer");
    check(!host.find({3,0,200}),"internal layout transition reused a prior entity namespace");
    puts("PASS: eight-player monitor contention, late join, single authority, reordered updates, disconnect handoff, offscreen lifetime, tombstones, act isolation");
}
