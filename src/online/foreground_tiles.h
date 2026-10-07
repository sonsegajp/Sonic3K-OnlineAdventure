#pragma once
#include <cstdint>
namespace adventure {
// Equivalent to the normal 64x32-cell foreground's screen refresh, without
// running guest code on a frame suspended inside a level/event routine. Only
// the foreground name table is written; RAM, tile art and VDP latches stay put.
inline void redraw_foreground(const uint8_t *ram,uint8_t *vram) {
    auto word=[&](unsigned a){return unsigned(ram[a])<<8|ram[a+1];};
    unsigned cols=word(0x8000),rows=word(0x8004),row_mask=word(0xeeae);
    if(!cols||cols>512||!rows||rows>32||(row_mask!=0x3c&&row_mask!=0x7c))return;
    unsigned left=word(0xee80)&0xfff0,top=word(0xee84)&0xfff0;
    for(unsigned by=0;by<15;by++)for(unsigned bx=0;bx<21;bx++) {
        unsigned x=(left+bx*16)&65535,y=(top+by*16)&65535;
        unsigned row=word(0x8008+((y>>5)&row_mask)),column=x>>7;
        if(row<0x8088||column>=cols||row+column>=0x9000)continue;
        unsigned chunk=ram[row+column];
        unsigned block=word(chunk*128+((y>>4)&7)*16+((x>>4)&7)*2),id=block&0x3ff;
        if(id>=0x300)continue;
        for(unsigned ty=0;ty<2;ty++)for(unsigned tx=0;tx<2;tx++) {
            unsigned sx=tx^((block>>10)&1),sy=ty^((block>>11)&1);
            unsigned attr=word(0x9000+id*8+sy*4+sx*2)^((block&0xc00)<<1);
            unsigned address=0xc000+((((y>>3)+ty)&31)*64+(((x>>3)+tx)&63))*2;
            vram[address]=uint8_t(attr>>8);vram[address+1]=uint8_t(attr);
        }
    }
}
}
