#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
namespace sc6::research {
// Ghidra/machine-code audit: 334960 reads the stage emitter list and mutates
// only pending/count +50/+54 and countdown +5C. Never restore links or config.
struct WindEmitterSnapshot {
    struct Entry {uintptr_t node{},next{},object{};std::array<std::byte,0xA8> bytes{};};
    std::array<Entry,128> entries{};
    uintptr_t root{};unsigned count{},check{},exception{};bool valid{};
    static uintptr_t ptr(uintptr_t p){uintptr_t v{};std::memcpy(&v,reinterpret_cast<void*>(p),8);return v;}
    bool capture_root(uintptr_t sentinel) noexcept {
        root=sentinel;count=check=exception=0;valid=false;
        __try {
            check=1;if(!root)return false;
            auto node=ptr(root);
            while(node!=root){
                check=2;if(!node || count==entries.size())return false;
                for(unsigned i=0;i<count;++i)if(entries[i].node==node)return false;
                auto& e=entries[count];e.node=node;e.next=ptr(node);e.object=ptr(node+16);
                check=3;if(!e.object || e.object>UINTPTR_MAX-e.bytes.size())return false;
                std::memcpy(e.bytes.data(),reinterpret_cast<void*>(e.object),e.bytes.size());
                ++count;node=e.next;
            }
            check=4;if(ptr(root)!=(count?entries[0].node:root))return false;
            for(unsigned i=0;i<count;++i){const auto& e=entries[i];
                if(ptr(e.node)!=e.next || ptr(e.node+16)!=e.object ||
                   std::memcmp(e.bytes.data(),reinterpret_cast<void*>(e.object),e.bytes.size()))return false;
            }
            valid=true;check=0;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();return false;}
    }
    bool capture(uintptr_t module) noexcept {
        __try {return capture_root(ptr(module+0x470F1C0));}
        __except(EXCEPTION_EXECUTE_HANDLER){valid=false;check=1;exception=GetExceptionCode();return false;}
    }
    static bool mutable_byte(unsigned i){return (i>=0x50 && i<0x58) || (i>=0x5C && i<0x60);}
    bool compatible(const WindEmitterSnapshot& live,bool exact=false)const noexcept {
        if(!valid || !live.valid || count>entries.size() || live.count>entries.size() || root!=live.root || count!=live.count)return false;
        for(unsigned i=0;i<count;++i){const auto& a=entries[i];const auto& b=live.entries[i];
            if(a.node!=b.node || a.next!=b.next || a.object!=b.object)return false;
            for(unsigned j=0;j<a.bytes.size();++j)if((exact || !mutable_byte(j)) && a.bytes[j]!=b.bytes[j])return false;
        }
        return true;
    }
    bool restore(uintptr_t module,WindEmitterSnapshot& live)const noexcept {
        if(!live.capture(module) || !compatible(live))return false;
        __try {
            // Complete identity/configuration validation precedes any write.
            for(unsigned i=0;i<count;++i){const auto& e=entries[i];
                std::memcpy(reinterpret_cast<void*>(e.object+0x50),e.bytes.data()+0x50,8);
                std::memcpy(reinterpret_cast<void*>(e.object+0x5C),e.bytes.data()+0x5C,4);
            }
        }__except(EXCEPTION_EXECUTE_HANDLER){live.exception=GetExceptionCode();return false;}
        return live.capture(module) && compatible(live,true);
    }
};
}
