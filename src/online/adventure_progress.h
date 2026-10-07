// Shared adventure state only. Native per-player worlds are deliberately absent.
#pragma once
#include <array>
#include <cstdint>
#include <memory>

namespace adventure {
constexpr unsigned seats = 8, zones = 14;
constexpr unsigned state_bytes = 196;
struct State {
    uint32_t tick=0, epoch=0, occupied=0, arrived=0, fault=0;
    std::array<uint32_t,zones> rings{};
    std::array<uint32_t,seats> arrival_epoch{}, stage{};
    std::array<uint16_t,seats> last_command{};
    std::array<uint8_t,7> emeralds{}, award_owner{};
    uint8_t converted=0;
};
using Image=std::array<uint8_t,state_bytes>;
State initial(uint32_t mask,uint32_t epoch);
Image encode(const State &);
bool decode(const Image &,State &);
uint32_t digest(const State &);
// Buttons are an opaque 16-bit command: toggle | opcode(3) | payload(12).
// Holding/predicting the last command is idempotent. All command data travels
// in the driver's sealed rows; none is hidden in live-only side bytes.
enum class Command : uint16_t { arrive=1, ring=2, emerald=3, convert=4 };
// Arrival names the act being cleared. A native zone/act word does not fit the
// 12-bit payload past zone $0F (Lava Reef boss $1600, Death Egg boss $1700),
// so arrivals carry zone*2+act. 4095 is the opening barrier with no act.
constexpr unsigned arrival_any = 4095, arrival_zones = 0x18;
inline unsigned arrival_code(uint32_t stage){
    if(stage==~0u)return arrival_any;
    unsigned zone=(stage>>8)&255,act=stage&255;
    return zone<arrival_zones&&act<=1?zone*2+act:arrival_any;
}
bool reduce(State &,const std::array<uint16_t,seats> &);

struct Config {
    int local=0;
    uint32_t session=0, content=0;
    const char *bind=":0", *peer=nullptr;
    State seed{};
};
struct Stats {
    uint32_t tick=0, confirmed=0, episodes=0, desyncs=0;
    uint64_t replayed=0;
    bool linked=false, started=false;
};
class Progress {
public:
    Progress();
    ~Progress();
    Progress(const Progress &)=delete;
    Progress &operator=(const Progress &)=delete;
    bool start(const Config &);
    void stop();
    // Pump often; tick admission is paced separately at 60 Hz.
    void poll();
    bool submit(Command,unsigned payload);
    const State &committed() const;
    Stats stats() const;
    const char *error() const;
    bool active() const;
    void drain();
    bool drained() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
}
