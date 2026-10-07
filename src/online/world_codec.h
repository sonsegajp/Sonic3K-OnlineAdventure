#pragma once
#include "shared_world.h"
#include <cstring>
namespace adventure {
// Sparse byte masks keep complete, independently decodable snapshots small.
// No dependency on a previous packet, so reconnect/coalescing cannot lose data.
inline size_t encode_world_image(uint8_t *p,const ObjectImage &image){
    size_t n=0;
    auto bytes=[&](const std::array<uint8_t,74>& b){size_t mask=n;n+=10;std::memset(p+mask,0,10);for(unsigned i=0;i<74;i++)if(b[i]){p[mask+i/8]|=uint8_t(1u<<(i%8));p[n++]=b[i];}};
    auto refs=[&](uint16_t mask,const std::array<EntityId,14>& a){p[n++]=uint8_t(mask>>8);p[n++]=uint8_t(mask);for(unsigned i=0;i<14;i++)if(mask&(1u<<i))for(int j=7;j>=0;j--)p[n++]=uint8_t(a[i]>>(j*8));};
    bytes(image.bytes);refs(image.reference_mask,image.references);
    bytes(image.initial);refs(image.initial_reference_mask,image.initial_references);
    for(int j=3;j>=0;j--)p[n++]=uint8_t(image.children>>(j*8));
    for(int j=7;j>=0;j--)p[n++]=uint8_t(image.parent>>(j*8));
    return n;
}
inline bool decode_world_image(const uint8_t *p,size_t length,ObjectImage &image){
    image={};size_t n=0;
    auto bytes=[&](std::array<uint8_t,74>& b){if(n+10>length||(p[n+9]&0xfc))return false;size_t mask=n;n+=10;for(unsigned i=0;i<74;i++)if(p[mask+i/8]&(1u<<(i%8))){if(n==length)return false;b[i]=p[n++];}return true;};
    auto refs=[&](uint16_t &mask,std::array<EntityId,14>& a){if(n+2>length)return false;mask=uint16_t((p[n]<<8)|p[n+1]);n+=2;if(mask&~0x3fff)return false;for(unsigned i=0;i<14;i++)if(mask&(1u<<i)){if(n+8>length)return false;for(unsigned j=0;j<8;j++)a[i]=(a[i]<<8)|p[n++];}return true;};
    if(!bytes(image.bytes)||!refs(image.reference_mask,image.references)||!bytes(image.initial)||!refs(image.initial_reference_mask,image.initial_references)||n+12!=length)return false;
    for(unsigned j=0;j<4;j++)image.children=(image.children<<8)|p[n++];
    for(unsigned j=0;j<8;j++)image.parent=(image.parent<<8)|p[n++];return true;
}
}
