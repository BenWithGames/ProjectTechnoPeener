#pragma once
#include "FullStateProbe.hpp"
namespace sc6::research {
// Observation only. Ignore the rotating history alias but require all captured
// blend weights/blocks and both owners to remain identical for ten boundaries.
struct IdleBlendSettle {
    using Bytes=std::array<std::array<std::byte,1372>,2>;
    Bytes previous{};std::array<uintptr_t,2> owners{};
    unsigned samples{},stable{};bool ready{},fault{};
    bool observe(bool idle,const std::array<uintptr_t,2>& current_owners,const Bytes& bytes){
        if(fault)return false;if(ready)return true;
        if(++samples>180){fault=true;return false;}
        if(!idle){stable=0;return true;}
        if(stable && owners==current_owners && previous==bytes)++stable;
        else stable=1;
        owners=current_owners;previous=bytes;ready=stable>=10;return true;
    }
    bool step(uintptr_t base) noexcept {
        __try {
            if(fault || ready)return !fault;
            Bytes bytes{};std::array<uintptr_t,2> current_owners{};
            const bool idle=FullStateProbe::idle(base);
            if(idle)for(unsigned side=0;side<2;++side){
                auto player=FullStateProbe::ptr(base+0x470DE90+side*8);
                if(player<0x10000 || player>UINTPTR_MAX-0x97200){fault=true;return false;}
                auto owner=FullStateProbe::ptr(player+0x971F8);
                if(owner<0x10000 || owner>UINTPTR_MAX-0x22A8){fault=true;return false;}
                current_owners[side]=owner;
                std::memcpy(bytes[side].data(),reinterpret_cast<void*>(owner+0x4BC),140);
                std::memcpy(bytes[side].data()+140,reinterpret_cast<void*>(owner+0x1DD8),1232);
            }
            return observe(idle,current_owners,bytes);
        }__except(EXCEPTION_EXECUTE_HANDLER){fault=true;return false;}
    }
};
}
