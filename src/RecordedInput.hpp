#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace sc6::research {
// Owned copies consumed synchronously by the pinned update entry; never write
// into the game's input buffers. Not copyable: header pointers refer to bytes.
struct RecordedInput {
    std::array<std::byte,40> bytes{};
    std::array<uintptr_t,3> pointers{};
    RecordedInput()=default;
    RecordedInput(const RecordedInput&)=delete;
    RecordedInput& operator=(const RecordedInput&)=delete;
    uintptr_t assign(const std::array<std::byte,40>& value) noexcept {
        bytes=value;
        pointers={reinterpret_cast<uintptr_t>(bytes.data()),reinterpret_cast<uintptr_t>(bytes.data()+8),reinterpret_cast<uintptr_t>(bytes.data()+16)};
        return reinterpret_cast<uintptr_t>(pointers.data());
    }
    // Captured release graph: (807,807)->(807,7)->(7,0)->(0,0),
    // plus the observed asynchronous (807,807)->(807,0)->(0,0) branch.
    static int charge_state(const std::array<std::byte,40>& value) noexcept {
        unsigned player{},aux{};std::memcpy(&player,value.data(),4);std::memcpy(&aux,value.data()+16,4);
        constexpr unsigned players[]={0x807,0x807,7,0,0x807},flags[]={0x807,7,0,0,0};
        for(int i=0;i<5;++i)if(player==players[i] && aux==flags[i])return i;
        return -1;
    }
    static bool charge_check(const std::array<std::byte,40>& a,const std::array<std::byte,40>& b,bool reachable) noexcept {
        // Preserve each recorded value; only finite normalized auxiliary floats.
        for(unsigned off=24;off<40;off+=4){unsigned x{},y{};
            std::memcpy(&x,a.data()+off,4);std::memcpy(&y,b.data()+off,4);
            if((x&0x7FFFFFFFu)>0x3F800000u || (y&0x7FFFFFFFu)>0x3F800000u)return false;}
        if(a==b)return true;
        if(std::memcmp(a.data()+4,b.data()+4,12) || std::memcmp(a.data()+20,b.data()+20,4))return false;
        int from=charge_state(a),to=charge_state(b);if(from<0 || to<0)return false;
        constexpr unsigned adjacent[]={0x13,0x6,0xC,0x8,0x18};
        constexpr unsigned paths[]={0x1F,0xE,0xC,0x8,0x18};
        return ((reachable?paths[from]:adjacent[from])&(1u<<to))!=0;
    }
    static bool charge_compatible(const std::array<std::byte,40>& a,const std::array<std::byte,40>& b) noexcept {
        return charge_check(a,b,false);
    }
    // Reference/live envelopes are compared to frame0, not the previous frame.
    static bool charge_reachable(const std::array<std::byte,40>& a,const std::array<std::byte,40>& b) noexcept {
        return charge_check(a,b,true);
    }
    static bool compatible(const std::array<std::byte,40>& a,const std::array<std::byte,40>& b) noexcept {
        if(std::memcmp(a.data(),b.data(),24))return false;
        for(unsigned off=24;off<40;off+=4){unsigned x{},y{};
            std::memcpy(&x,a.data()+off,4);std::memcpy(&y,b.data()+off,4);
            if((x&0x7FFFFFFFu)>0x3F800000u || (y&0x7FFFFFFFu)>0x3F800000u)return false;
        }
        return true;
    }
};
}
