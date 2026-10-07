#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <map>
#include <limits>
#include <vector>

namespace adventure {
// One canonical object registry per session. Native allocation slots are local
// addresses, so every object reference travels as an entity identity instead.
using EntityId=uint64_t;
struct ObjectImage {
    std::array<uint8_t,74> bytes{};
    std::array<EntityId,14> references{}; // word fields $2E through $48 (including multipart platform parents)
    uint16_t reference_mask=0;
    uint32_t children=0;
    EntityId parent=0;
    std::array<uint8_t,74> initial{};
    std::array<EntityId,14> initial_references{};
    uint16_t initial_reference_mask=0;
    bool operator==(const ObjectImage &b) const {
        return bytes==b.bytes&&references==b.references&&reference_mask==b.reference_mask&&children==b.children&&parent==b.parent&&initial==b.initial&&initial_references==b.initial_references&&initial_reference_mask==b.initial_reference_mask;
    }
};
struct EntityKey {
    uint32_t epoch=0,stage=0;EntityId id=0;
    bool operator<(const EntityKey &b) const {
        return epoch!=b.epoch?epoch<b.epoch:stage!=b.stage?stage<b.stage:id<b.id;
    }
};
struct WorldEntity {
    EntityKey key{};ObjectImage image{};
    uint32_t revision=0,generation=0,tick=0;
    int owner=-1,collector=-1;
    bool consumed=false,removed=false,goal_open=false;
    uint16_t goal_x=0,goal_y=0;
    std::array<uint32_t,8> seen{};
    std::array<uint32_t,8> distance{};
    std::array<bool,8> touching{};
    uint32_t touched=0; // local ms of the last change or report; never transmitted
};
enum class WorldClaim { denied, granted, already_granted };
class SharedWorld {
public:
    // Roots and parentless effects keep a record for the whole act.
    static constexpr size_t capacity=8192;
    const std::map<EntityKey,WorldEntity> &entities() const{return objects;}
    WorldEntity *find(const EntityKey &key){auto it=objects.find(key);return it==objects.end()?nullptr:&it->second;}
    void clear(){objects.clear();active=0;}
    void discard_before(uint32_t epoch){for(auto it=objects.begin();it!=objects.end();)if(it->first.epoch<epoch)it=objects.erase(it);else ++it;}
    void set_active(uint32_t mask){active=mask&255;}
    uint32_t active_players() const{return active;}
    // Observing never overwrites an existing world object with a freshly
    // loaded private copy. This is also the late-join/reconnect rule.
    WorldEntity *observe(unsigned who,const EntityKey &key,const ObjectImage &initial,uint32_t now,uint32_t distance,bool touching=false) {
        if(who>=8||!(active&(1u<<who))||!key.id)return nullptr;
        auto *entity=find(key);
        if(!entity) {
            if(objects.size()>=capacity)return nullptr;
            entity=&objects[key];entity->key=key;entity->image=initial;
            entity->distance.fill((std::numeric_limits<uint32_t>::max)());
            entity->owner=int(who);entity->generation=1;entity->revision=1;
        }
        entity->seen[who]=now;entity->distance[who]=distance;entity->touching[who]=touching;entity->touched=now;
        if(entity->owner<0&&!entity->removed){entity->owner=int(who);entity->generation++;entity->revision++;entity->tick=0;}
        return entity;
    }
    bool update(unsigned who,const EntityKey &key,uint32_t generation,uint32_t tick,const ObjectImage &image) {
        auto *entity=find(key);
        if(!entity||entity->removed||who>=8||!(active&(1u<<who))||entity->owner!=int(who)||entity->generation!=generation)return false;
        if(entity->tick&&int32_t(tick-entity->tick)<=0)return false;
        entity->tick=tick;
        ObjectImage next=image;
        // A returning player may have restored an already-running native
        // object. Keep the original initializer for future camera/area loads.
        next.initial=entity->image.initial;next.initial_references=entity->image.initial_references;
        next.initial_reference_mask=entity->image.initial_reference_mask;next.parent=entity->image.parent;
        if(entity->image==next)return false;
        entity->image=next;entity->revision++;return true;
    }
    // All consumers contend for this single committed transition. Retries by
    // the winning player are idempotent; another player never receives its reward.
    WorldClaim consume(unsigned who,const EntityKey &key) {
        auto *entity=find(key);if(!entity||who>=8||!(active&(1u<<who))||entity->removed)return WorldClaim::denied;
        if(entity->consumed)return entity->collector==int(who)?WorldClaim::already_granted:WorldClaim::denied;
        entity->consumed=true;entity->collector=int(who);entity->revision++;return WorldClaim::granted;
    }
    bool retire(unsigned who,const EntityKey &key,uint32_t generation,bool destroyed) {
        auto *entity=find(key);if(!entity||entity->owner!=int(who)||entity->generation!=generation)return false;
        entity->seen[who]=0;entity->touching[who]=false;entity->owner=-1;entity->tick=0;entity->generation++;entity->revision++;
        // Going offscreen is not destruction. Keep the last canonical state
        // available to a player approaching from another part of the level.
        if(destroyed||entity->consumed)entity->removed=true;
        return true;
    }
    bool handoff(WorldEntity &entity,uint32_t now) {
        if(entity.removed)return false;
        auto available=[&](unsigned who){return (active&(1u<<who))&&entity.seen[who]&&uint32_t(now-entity.seen[who])<600;};
        int next=-1;uint32_t distance=(std::numeric_limits<uint32_t>::max)();
        for(unsigned who=0;who<8;who++)if(available(who)&&entity.distance[who]<distance){next=int(who);distance=entity.distance[who];}
        int contact=-1;
        for(unsigned who=0;who<8;who++)if(available(who)&&entity.touching[who]&&entity.distance[who]<128u*128u){contact=int(who);break;}
        if(entity.owner>=0&&available(unsigned(entity.owner))){
            if(entity.consumed||entity.touching[unsigned(entity.owner)])return false;
            if(contact>=0&&contact!=entity.owner){entity.owner=contact;entity.generation++;entity.revision++;entity.tick=0;return true;}
            // Give the interacting nearby player authority before contact.
            // A 64-pixel advantage and a 96-pixel acquisition radius prevent
            // equally close players from transferring it every network tick.
            uint32_t current=entity.distance[unsigned(entity.owner)];
            if(distance>96u*96u||current<distance+64u*64u)return false;
        }
        if(next==entity.owner)return false;
        entity.owner=next;entity.generation++;entity.revision++;entity.tick=0;return true;
    }
    std::vector<EntityKey> handoff_all(uint32_t now){
        // Attached parts write their parents' native state (buttons, hands,
        // weak points). Running them on separate owners loses those writes.
        // Group live nearby parent links; detached/distant projectiles retain
        // their own authority instead of following their original creator.
        std::map<EntityKey,std::vector<WorldEntity*>> groups;
        auto word=[](const ObjectImage &i,unsigned a){return unsigned(i.bytes[a])<<8|i.bytes[a+1];};
        auto linked_to=[&](WorldEntity *part,EntityId creator){
            std::vector<WorldEntity*> links{part};
            for(size_t next=0;next<links.size()&&next<93;next++){
                const auto &image=links[next]->image;
                for(unsigned i=0;i<14;i++)if(image.reference_mask&(1u<<i)){
                    auto id=image.references[i];if(id==creator)return true;
                    auto *peer=find({part->key.epoch,part->key.stage,id});
                    // Native linked lists record their allocator as parent,
                    // while each segment points to its immediate neighbour.
                    if(peer&&!peer->removed&&peer->image.parent==creator&&
                       std::find(links.begin(),links.end(),peer)==links.end())links.push_back(peer);
                }
            }
            return false;
        };
        for(auto &entry:objects){
            auto *part=&entry.second;if(part->removed)continue;auto *root=part;
            for(unsigned depth=0;depth<93&&root->image.parent;depth++){
                bool attached=linked_to(root,root->image.parent);
                auto *parent=find({root->key.epoch,root->key.stage,root->image.parent});
                if(!attached||!parent||parent->removed||parent==part)break;
                int dx=int(word(part->image,16))-int(word(parent->image,16));
                int dy=int(word(part->image,20))-int(word(parent->image,20));
                if(dx>512||dx< -512||dy>512||dy< -512)break;
                root=parent;
            }
            groups[root->key].push_back(part);
        }
        std::vector<EntityKey> changed;
        for(auto &group:groups){
            auto *root=find(group.first);WorldEntity choice=*root;
            for(unsigned who=0;who<8;who++){
                // A candidate must have the parent loaded before it can run
                // the assembly. Contact with any linked part takes priority.
                if(!choice.seen[who]||uint32_t(now-choice.seen[who])>=600)continue;
                for(auto *part:group.second)if(part->seen[who]&&uint32_t(now-part->seen[who])<600){
                    choice.distance[who]=(std::min)(choice.distance[who],part->distance[who]);
                    choice.touching[who]=choice.touching[who]||part->touching[who];
                }
            }
            handoff(choice,now);
            for(auto *part:group.second)if(part->owner!=choice.owner){
                part->owner=choice.owner;part->generation++;part->revision++;part->tick=0;changed.push_back(part->key);
            }
        }
        return changed;
    }
    void erase(const EntityKey &key){objects.erase(key);}
    bool import(const WorldEntity &entity) {
        auto *previous=find(entity.key);
        if(previous&&int32_t(entity.revision-previous->revision)<=0)return false;
        if(!previous&&objects.size()>=capacity)return false;
        objects[entity.key]=entity;return true;
    }
private:
    std::map<EntityKey,WorldEntity> objects;
    uint32_t active=0;
};
}
