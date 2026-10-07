// Session-wide emerald ownership, independent of each player's special-stage
// mode, camera, result animation and act-clear barrier. Native stage selection
// at $8606 skips the collected entries, so subsequent rings select the next
// uncompleted stage without changing the course of an existing visit.
struct SharedEmeralds {
    std::array<unsigned char,7> states{};
    bool converted=false;
} emeralds;
std::array<SDL_Texture*,14> emerald_art{};
bool emerald_art_attempted=false;
void emerald_reset(){
    emeralds=SharedEmeralds{};emerald_art_attempted=false;
    for(auto &t:emerald_art)if(t){SDL_DestroyTexture(t);t=nullptr;}
}
unsigned emerald_count(unsigned mask){unsigned count=0;for(;mask;mask>>=1)count+=mask&1;return count;}
void emerald_masks(unsigned &chaos,unsigned &super){
    chaos=super=0;
    for(unsigned i=0;i<7;i++){if(emeralds.states[i])chaos|=1u<<i;if(emeralds.states[i]==3)super|=1u<<i;}
}
void emerald_project(){
    unsigned chaos,super;emerald_masks(chaos,super);
    for(auto &v:s.players)if(v.present){v.emerald_state=emeralds.states;v.chaos_mask=chaos;v.super_mask=super;v.chaos_count=emerald_count(chaos);v.super_count=emerald_count(super);}
}
void emerald_apply(){
    unsigned chaos,super;emerald_masks(chaos,super);
    // Preserve a locally earned native award while its command is in flight.
    // Remote/UI inventory still comes exclusively from the committed image.
    for(unsigned i=0;i<7;i++){
        if(progress_wanted_gems[i])chaos|=1u<<i;
        if(progress_wanted_gems[i]==3)super|=1u<<i;
    }
    w8(0xffb0,emerald_count(chaos));w8(0xffb1,emerald_count(super));
    for(unsigned i=0;i<7;i++) {
        // A Chaos-stage award sets bit 0 of its native entry. Importing a
        // remote grey Super conversion into that entry mid-run would turn
        // that Chaos win into an unearned Super Emerald. Defer only that
        // entry until results; the complete shared ownership stays in UI.
        if(r8(0xf600)==0x34&&r8(0xfe16)==i)continue;
        w8(0xffb2+i,(std::max)(emeralds.states[i],progress_wanted_gems[i]));
    }
    w8(0xffba,(emeralds.converted||progress_wanted_conversion)?0xff:0);emerald_project();
}
void emerald_frame(){
    if(s.phase!=PLAY||s.start_pending||s.local_id<0)return;
    unsigned mode=r8(0xf600);if(mode!=0x0c&&mode!=0x34&&mode!=0x48)return;
    progress_observe_emeralds(g_ram+0xffb2,r8(0xffba)!=0);
    emerald_apply();
}

// Reuse the game's Kosinski-M art, sprite mappings and seven award palettes.
// $9C28 loads the original palettes at $9D1E and art at $AE3EC/$AE99E.
// The mappings are entries 11 and 13 of the native MapPtr_A10A table.
void emerald_textures(SDL_Renderer *renderer){
    if(emerald_art_attempted||s.phase!=PLAY)return;emerald_art_attempted=true;
    for(unsigned type=0;type<2;type++){
        unsigned address=type?0xae99e:0xae3ec;
        std::array<unsigned char,4096> tiles{};
        size_t length=s3r_kosinskim_decode(g_rom+address,sizeof(g_rom)-address,tiles.data(),tiles.size(),nullptr);
        if(!length)continue;
        unsigned mapping=get32(g_rom+0xa10a+(type?13:11)*8);
        unsigned frame=mapping+get16(g_rom+mapping+(type?2:0)); // 32x32 Chaos / 40x32 Super
        if(frame+2>=sizeof(g_rom))continue;unsigned pieces=get16(g_rom+frame);
        if(pieces>16||frame+2+pieces*6>sizeof(g_rom))continue;
        for(unsigned gem=0;gem<7;gem++){
            std::array<uint32_t,48*40> pixels{};uint32_t palette[16]{};
            palette[1]=0xffffffff;
            for(unsigned c=2;c<6;c++){
                unsigned color=get16(g_rom+0x9d1e+gem*8+(c-2)*2);
                unsigned red=((color>>1)&7)*255/7,green=((color>>5)&7)*255/7,blue=((color>>9)&7)*255/7;
                palette[c]=0xff000000|(red<<16)|(green<<8)|blue;
            }
            for(unsigned piece=0;piece<pieces;piece++){
                const auto *q=g_rom+frame+2+piece*6;
                int x=int(int16_t(get16(q+4)))+24,y=int(int8_t(q[0]))+20;
                unsigned width=((q[1]>>2)&3)+1,height=(q[1]&3)+1,attr=get16(q+2);
                for(unsigned py=0;py<height*8;py++)for(unsigned px=0;px<width*8;px++){
                    int dx=x+int(px),dy=y+int(py);if(dx<0||dx>=48||dy<0||dy>=40)continue;
                    unsigned sx=(attr&0x800)?width*8-1-px:px,sy=(attr&0x1000)?height*8-1-py:py;
                    unsigned tile=(attr&0x7ff)+(sx/8)*height+sy/8,offset=tile*32+(sy%8)*4+(sx%8)/2;
                    if(offset>=length)continue;unsigned color=(tiles[offset]>>((sx&1)?0:4))&15;
                    if(color)pixels[dy*48+dx]=palette[color];
                }
            }
            auto &texture=emerald_art[type*7+gem];texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,48,40);
            if(texture){SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture,SDL_ScaleModeNearest);SDL_UpdateTexture(texture,nullptr,pixels.data(),48*4);}
        }
    }
}
unsigned emerald_art_count(){unsigned count=0;for(auto t:emerald_art)if(t)count++;return count;}
void emerald_draw_tracker(){
    unsigned chaos,super;emerald_masks(chaos,super);
    ImGui::TextUnformatted("Shared emeralds");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(4,4));
    for(unsigned type=0;type<2;type++){
        unsigned mask=type?super:chaos;
        ImGui::Text("%s Emeralds  %u / 7",type?"Super":"Chaos",emerald_count(mask));
        for(unsigned gem=0;gem<7;gem++){
            if(gem)ImGui::SameLine();bool owned=(mask&(1u<<gem))!=0;
            auto texture=emerald_art[type*7+gem];
            if(texture)ImGui::ImageWithBg((ImTextureID)(intptr_t)texture,ImVec2(48,40),ImVec2(0,0),ImVec2(1,1),ImVec4(0,0,0,0),owned?ImVec4(1,1,1,1):ImVec4(.22f,.22f,.25f,1));
            else ImGui::Dummy(ImVec2(48,40));
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s Emerald %u: %s",type?"Super":"Chaos",gem+1,owned?"collected by the team":"not collected");
        }
    }
    ImGui::PopStyleVar();ImGui::Separator();
}
