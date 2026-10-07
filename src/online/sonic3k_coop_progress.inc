// The shared reducer may rewind; only its committed image reaches native RAM.
// Local worlds, camera, palette editor, sockets and SDL resources never rewind.
unsigned progress_mask(){unsigned mask=0;for(unsigned i=0;i<MAX_PLAYERS;i++)if(s.players[i].present)mask|=1u<<i;return mask;}
void progress_clear(){
    progress.stop();progress_generation=0;progress_arrival=~0u;
    progress_wanted_rings.fill(0);progress_sent_rings.fill(0);
    progress_wanted_gems.fill(0);progress_sent_gems.fill(0);
    progress_wanted_conversion=progress_sent_conversion=false;
}
bool progress_configure(unsigned generation,const adventure::State &seed){
    char bind[64],peer[96];snprintf(bind,sizeof(bind),":%d",s.host?port_number:0);
    snprintf(peer,sizeof(peer),"%s:%d",peer_address,port_number);
    adventure::Config config;config.local=s.local_id;config.seed=seed;config.bind=bind;config.peer=peer;config.content=rom_crc;
    config.session=unsigned(s.room)^unsigned(s.room>>32)^(generation*0x9e3779b9u);if(!config.session)config.session=1;
    if(!progress.start(config)){fail(progress.error()[0]?progress.error():"Could not start shared progress.");return false;}
    progress_generation=generation;progress_arrival=~0u;
    progress_sent_rings.fill(0);progress_sent_gems.fill(0);progress_sent_conversion=false;
    // Rejoining imports already announced awards without presenting them again.
    emeralds.states=seed.emeralds;emeralds.converted=seed.converted!=0;emerald_project();
    fprintf(stderr,"[coop] recomp-net progress generation=%u mask=%02x local=%d epoch=%u\n",generation,seed.occupied,s.local_id,seed.epoch);
    return true;
}
bool progress_restart(bool resume){
    if(!s.host)return false;
    auto seed=resume?progress.committed():adventure::initial(progress_mask(),has_character(0)?0:1);
    seed.occupied=progress_mask();seed.tick=0;seed.last_command.fill(0);
    unsigned generation=progress_generation+1;
    if(!progress_configure(generation,seed))return false;
    unsigned char data[4+adventure::state_bytes];put32(data,generation);auto image=adventure::encode(seed);memcpy(data+4,image.data(),image.size());
    broadcast(PROGRESS_CONFIG,data,sizeof(data));return true;
}
bool progress_handle(const unsigned char *data,unsigned length){
    if(s.host||length!=4+adventure::state_bytes||s.local_id<0)return false;
    unsigned generation=get32(data);if(!generation)return false;if(generation<=progress_generation)return true;
    adventure::Image image;memcpy(image.data(),data+4,image.size());adventure::State seed;
    if(!adventure::decode(image,seed)||seed.occupied!=progress_mask())return false;
    return progress_configure(generation,seed);
}
void progress_observe_emeralds(const unsigned char *states,bool converted){
    for(unsigned i=0;i<7;i++)if(states[i]<=3)progress_wanted_gems[i]=(std::max)(progress_wanted_gems[i],states[i]);
    progress_wanted_conversion|=converted;
}
void progress_poll(){
    if(!progress.active())return;
    progress.poll();if(progress.error()[0]){fail(progress.error());return;}
    const auto &state=progress.committed();
    if(!in_game())return;
    // Retry local intents after a reconnect generation. The command row is
    // acknowledged by the shared state, never by a presentation TCP packet.
    for(unsigned z=0;z<14;z++){
        unsigned pending=progress_wanted_rings[z]&~state.rings[z]&~progress_sent_rings[z];
        for(unsigned b=0;b<32;b++)if((pending&(1u<<b))&&progress.submit(adventure::Command::ring,z*32+b))progress_sent_rings[z]|=1u<<b;
        s.used_rings[z]=state.rings[z];
    }
    for(unsigned i=0;i<7;i++){
        unsigned desired=progress_wanted_gems[i];
        if(desired>state.emeralds[i]&&desired>progress_sent_gems[i]&&progress.submit(adventure::Command::emerald,i|(desired<<3)))progress_sent_gems[i]=uint8_t(desired);
    }
    if(progress_wanted_conversion&&!state.converted&&!progress_sent_conversion&&progress.submit(adventure::Command::convert,0))progress_sent_conversion=true;
    auto previous=emeralds.states;emeralds.states=state.emeralds;emeralds.converted=state.converted!=0;emerald_project();
    unsigned chaos,super;emerald_masks(chaos,super);
    for(unsigned i=0;i<7;i++)if(!previous[i]&&state.emeralds[i]&&state.award_owner[i]<MAX_PLAYERS)emerald_notice(state.award_owner[i],emerald_count(chaos));
    for(unsigned i=0;i<MAX_PLAYERS;i++)if(s.players[i].present){
        if(int(i)!=s.local_id||!s.waiting||(state.arrival_epoch[i]!=~0u&&state.arrival_epoch[i]>=s.epoch)){s.players[i].arrival_epoch=state.arrival_epoch[i];s.players[i].stage=state.stage[i];}
    }
    if(s.waiting){
        if(state.epoch>s.epoch){s.last_release=s.epoch;s.released=true;}
        else if(progress_arrival!=s.epoch&&progress.submit(adventure::Command::arrive,adventure::arrival_code(s.barrier_stage)))progress_arrival=s.epoch;
    }
}
