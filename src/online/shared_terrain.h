#pragma once
#include <cstdint>
#include <map>
namespace adventure {
struct TerrainKey {
    uint32_t epoch=0,stage=0,address=0;
    bool operator<(const TerrainKey &b)const{return epoch!=b.epoch?epoch<b.epoch:stage!=b.stage?stage<b.stage:address<b.address;}
};
struct TerrainCell {uint32_t revision=0;uint16_t value=0;};
// Compare-and-set changes prevent an old private copy from undoing a remote
// destroyed wall, opened route or collected object's persistent respawn bits.
class SharedTerrain {
    std::map<TerrainKey,TerrainCell> cells_;
public:
    inline static constexpr unsigned environment[]={0xf648,0xf64a,0xf64c,0xf7c2,0xf7c6};
    static uint16_t mask(unsigned address){
        if(address&1)return 0;
        if(address<0xa800)return 0xffff; // chunks, layout and collision blocks
        if(address>=0xeb00&&address<0xee00)return 0x7f7f; // loaded bits are local
        if(address==0xf648||address==0xf64a)return 0xffff; // mean/target water height
        if(address==0xf64c||address==0xf7c6)return 0xff00; // water speed/reverse gravity
        if(address==0xf7c2)return 0x00ff; // Sandopolis darkness
        return 0;
    }
    void clear(){cells_.clear();}
    void discard_before(uint32_t epoch){for(auto i=cells_.begin();i!=cells_.end();)if(i->first.epoch<epoch)i=cells_.erase(i);else ++i;}
    const auto &cells()const{return cells_;}
    const TerrainCell *find(const TerrainKey &key)const{auto i=cells_.find(key);return i==cells_.end()?nullptr:&i->second;}
    const TerrainCell *change(const TerrainKey &key,uint16_t base,uint16_t value){
        uint16_t m=mask(key.address);if(!m)return nullptr;base&=m;value&=m;
        auto &cell=cells_[key];if(!cell.revision){cell.value=base;cell.revision=1;}
        if(key.address>=0xeb00&&key.address<0xee00){
            uint16_t changed=base^value;
            // Independent object bits can share a word in the native table.
            // Merge only the bits changed by this report, preserving a
            // simultaneous interaction with its neighbouring object.
            uint16_t merged=(cell.value&~changed)|(value&changed);
            if(cell.value!=merged){cell.value=merged;cell.revision++;}
        }else if(cell.value==base&&cell.value!=value){cell.value=value;cell.revision++;}
        return &cell;
    }
    bool import(const TerrainKey &key,TerrainCell cell){
        auto m=mask(key.address);if(!m||!cell.revision||(cell.value&~m))return false;
        auto i=cells_.find(key);if(i!=cells_.end()&&int32_t(cell.revision-i->second.revision)<=0)return false;
        cells_[key]=cell;return true;
    }
};
}
