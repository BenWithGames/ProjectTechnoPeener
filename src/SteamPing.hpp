#pragma once
#include <cstdint>
#include <optional>
namespace sc6 {
// All times use the sender's monotonic clock. No cross-machine clock subtraction.
struct SteamPing {
    uint32_t sequence=0,pending=0;
    uint64_t sent_us=0,next_us=0,received_us=0;
    std::optional<unsigned> latest;
    bool due(uint64_t now)const{return now>=next_us;}
    uint32_t begin(uint64_t now){
        if(++sequence==0)++sequence;
        pending=sequence;sent_us=now;next_us=now+3000000;return pending;
    }
    bool pong(uint32_t id,uint64_t now){
        if(!pending||id!=pending||now<sent_us||now-sent_us>3000000)return false;
        latest=static_cast<unsigned>((now-sent_us+500)/1000);received_us=now;pending=0;return true;
    }
    std::optional<unsigned> value(uint64_t now)const{
        return latest && now>=received_us && now-received_us<=9000000?latest:std::nullopt;
    }
};
}
